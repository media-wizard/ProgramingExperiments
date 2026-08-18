#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <algorithm>
#include <random>
#include <string>
#include <vector>

#include <wayland-client.h>
#include <wayland-egl.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include "xdg-shell-client-protocol.h"

// Global Wayland variables
struct wl_display *display = NULL;
struct wl_registry *registry = NULL;
struct wl_compositor *compositor = NULL;
struct xdg_wm_base *wm_base = NULL;
struct wl_surface *surface = NULL;
struct xdg_surface *xdg_surface = NULL;
struct xdg_toplevel *xdg_toplevel = NULL;
struct wl_egl_window *egl_window = NULL;
struct wl_seat *seat = NULL;
struct wl_keyboard *keyboard = NULL;

// Global EGL variables
EGLDisplay egl_display;
EGLConfig egl_config;
EGLContext egl_context;
EGLSurface egl_surface;

static const int WIN_W = 640;
static const int WIN_H = 480;
static int TEX_COUNT = 192;
static int TEX_SIZE = 512;
static int SPRITE_COUNT = 1500;
static bool g_pauseRequested = false;
static bool g_quitRequested = false;
static bool g_paused = false;
static bool g_cachedTextures = false;
static const char *g_cachePath = "/tmp/waygl_texcache.bin";

static double getTimeSeconds() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static const char *kSpriteVS =
    "attribute vec2 aPos;\n"
    "attribute vec2 aUV;\n"
    "uniform mat4 uProj;\n"
    "uniform vec2 uCenter;\n"
    "uniform vec2 uHalfSize;\n"
    "uniform float uAngle;\n"
    "varying vec2 vUV;\n"
    "void main() {\n"
    "  vec2 p = aPos * uHalfSize;\n"
    "  float c = cos(uAngle); float s = sin(uAngle);\n"
    "  p = vec2(p.x * c - p.y * s, p.x * s + p.y * c) + uCenter;\n"
    "  gl_Position = uProj * vec4(p, 0.0, 1.0);\n"
    "  vUV = aUV;\n"
    "}\n";

static const char *kSpriteFS =
    "precision mediump float;\n"
    "varying vec2 vUV;\n"
    "uniform sampler2D uTex;\n"
    "uniform vec4 uTint;\n"
    "void main() {\n"
    "  gl_FragColor = texture2D(uTex, vUV) * uTint;\n"
    "}\n";

static const char *kTextVS =
    "attribute vec4 aVertex;\n"
    "uniform mat4 uProj;\n"
    "varying vec2 vUV;\n"
    "void main() {\n"
    "  gl_Position = uProj * vec4(aVertex.xy, 0.0, 1.0);\n"
    "  vUV = aVertex.zw;\n"
    "}\n";

static const char *kTextFS =
    "precision mediump float;\n"
    "varying vec2 vUV;\n"
    "uniform sampler2D uGlyph;\n"
    "uniform vec3 uColor;\n"
    "void main() {\n"
    "  float alpha = texture2D(uGlyph, vUV).a;\n"
    "  gl_FragColor = vec4(uColor, alpha);\n"
    "}\n";

static const char *kSolidVS =
    "attribute vec2 aPos;\n"
    "uniform mat4 uProj;\n"
    "void main() {\n"
    "  gl_Position = uProj * vec4(aPos, 0.0, 1.0);\n"
    "}\n";

static const char *kSolidFS =
    "precision mediump float;\n"
    "uniform vec4 uColor;\n"
    "void main() {\n"
    "  gl_FragColor = uColor;\n"
    "}\n";

static void makeOrtho(float *m, float l, float r, float b, float t) {
    for (int i = 0; i < 16; ++i) m[i] = 0.0f;
    m[0] = 2.0f / (r - l);
    m[5] = 2.0f / (t - b);
    m[10] = -1.0f;
    m[12] = -(r + l) / (r - l);
    m[13] = -(t + b) / (t - b);
    m[15] = 1.0f;
}

static GLuint compileShader(GLenum type, const char *src) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, NULL);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        fprintf(stderr, "Shader error: %s\n", log);
        exit(1);
    }
    return shader;
}

static GLuint linkProgram(const char *vsSrc, const char *fsSrc) {
    GLuint vs = compileShader(GL_VERTEX_SHADER, vsSrc);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fsSrc);
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(p, sizeof(log), NULL, log);
        fprintf(stderr, "Program link error: %s\n", log);
        exit(1);
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    return p;
}

struct Sprite {
    float x, y, vx, vy;
    float half;
    float angle, spin;
    float r, g, b, a;
    int tex;
};

// 7x7 column-major bitmap font definitions
static const uint8_t kNumericFont[13][7] = {
    {0x3E, 0x41, 0x41, 0x41, 0x41, 0x41, 0x3E}, // 0
    {0x00, 0x00, 0x42, 0x7F, 0x40, 0x00, 0x00}, // 1
    {0x62, 0x51, 0x49, 0x49, 0x49, 0x49, 0x46}, // 2
    {0x22, 0x41, 0x49, 0x49, 0x49, 0x49, 0x36}, // 3
    {0x18, 0x14, 0x12, 0x11, 0x7F, 0x10, 0x10}, // 4
    {0x27, 0x45, 0x45, 0x45, 0x45, 0x45, 0x39}, // 5
    {0x3E, 0x49, 0x49, 0x49, 0x49, 0x49, 0x32}, // 6
    {0x01, 0x01, 0x01, 0x79, 0x09, 0x09, 0x07}, // 7
    {0x36, 0x49, 0x49, 0x49, 0x49, 0x49, 0x36}, // 8
    {0x26, 0x49, 0x49, 0x49, 0x49, 0x49, 0x3E}, // 9
    {0x00, 0x00, 0x66, 0x66, 0x00, 0x00, 0x00}, // :
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // space
    {0x00, 0x00, 0x06, 0x06, 0x00, 0x00, 0x00}, // .
};

static GLuint createDigitAtlas() {
    const int cell = 8;
    const int atlasSize = 64;
    std::vector<unsigned char> pixels(atlasSize * atlasSize * 4, 0);

    for (int g = 0; g < 13; ++g) {
        int gx = (g % 8) * cell;
        int gy = (g / 8) * cell;
        for (int col = 0; col < 7; ++col) {
            unsigned char b = kNumericFont[g][col];
            for (int row = 0; row < 7; ++row) {
                bool on = (b & (1 << row)) != 0;
                int x = gx + col;
                int y = gy + row;
                if (x < atlasSize && y < atlasSize) {
                    size_t i = ((size_t)y * atlasSize + x) * 4;
                    unsigned char val = on ? 255 : 0;
                    pixels[i + 0] = 255;
                    pixels[i + 1] = 255;
                    pixels[i + 2] = 255;
                    pixels[i + 3] = val;
                }
            }
        }
    }

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, atlasSize, atlasSize, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

static int glyphIndex(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch == ':') return 10;
    if (ch == ' ') return 11;
    if (ch == '.') return 12;
    return 11;
}

static std::vector<GLuint> createTextureBank(int count, int size, size_t *bytesOut) {
    std::vector<GLuint> texs(count, 0);
    std::vector<unsigned char> pixels((size_t)size * size * 4);
    std::mt19937 rng(1337);
    glGenTextures(count, texs.data());
    for (int i = 0; i < count; ++i) {
        float hue = (float)i / (float)count * 6.2831853f;
        float r = 0.5f + 0.5f * sin(hue);
        float g = 0.5f + 0.5f * sin(hue + 2.094f);
        float b = 0.5f + 0.5f * sin(hue + 4.188f);
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                float fx = (float)x / (float)size;
                float fy = (float)y / (float)size;
                float pattern = 0.5f + 0.5f * sin((fx * 10.0f + i * 0.3f) * 3.14159f) * cos((fy * 10.0f + i * 0.2f) * 3.14159f);
                float noise = ((float)(rng() & 0xFF) / 255.0f) * 0.25f;
                size_t off = ((size_t)y * size + x) * 4;
                pixels[off + 0] = (unsigned char)(255.0f * std::min(1.0f, r * pattern + noise));
                pixels[off + 1] = (unsigned char)(255.0f * std::min(1.0f, g * pattern + noise));
                pixels[off + 2] = (unsigned char)(255.0f * std::min(1.0f, b * pattern + noise));
                pixels[off + 3] = 255;
            }
        }
        glBindTexture(GL_TEXTURE_2D, texs[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    size_t base = (size_t)count * size * size * 4;
    *bytesOut = base + base / 3;
    return texs;
}

static void uploadTexture(GLuint tex, int size, const unsigned char *pixels) {
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
}

static bool swapTexturesToDisk(std::vector<GLuint> &texs, int size, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "Cannot open cache file %s\n", path);
        return false;
    }
    std::vector<unsigned char> pixels((size_t)size * size * 4);
    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    for (GLuint tex : texs) {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
        glReadPixels(0, 0, size, size, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        if (fwrite(pixels.data(), 1, pixels.size(), f) != pixels.size()) {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glDeleteFramebuffers(1, &fbo);
            fclose(f);
            return false;
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);
    fclose(f);
    glDeleteTextures((GLsizei)texs.size(), texs.data());
    std::fill(texs.begin(), texs.end(), 0u);
    glFinish();
    return true;
}

static bool swapTexturesFromDisk(std::vector<GLuint> &texs, int size, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Cannot open cache file %s\n", path);
        return false;
    }
    std::vector<unsigned char> pixels((size_t)size * size * 4);
    glGenTextures((GLsizei)texs.size(), texs.data());
    for (size_t i = 0; i < texs.size(); ++i) {
        if (fread(pixels.data(), 1, pixels.size(), f) != pixels.size()) {
            fclose(f);
            return false;
        }
        uploadTexture(texs[i], size, pixels.data());
    }
    fclose(f);
    remove(path);
    glFinish();
    return true;
}

static void xdg_wm_base_ping(void *data, struct xdg_wm_base *wm_base, uint32_t serial) {
    xdg_wm_base_pong(wm_base, serial);
}

static const struct xdg_wm_base_listener xdg_wm_base_listener = {
    xdg_wm_base_ping,
};

static void xdg_surface_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial) {
    xdg_surface_ack_configure(xdg_surface, serial);
}

static const struct xdg_surface_listener xdg_surface_listener = {
    xdg_surface_configure,
};

static void keyboard_handle_keymap(void *data, struct wl_keyboard *keyboard, uint32_t format,
                                  int32_t fd, uint32_t size) {}
static void keyboard_handle_enter(void *data, struct wl_keyboard *keyboard, uint32_t serial,
                                 struct wl_surface *surface, struct wl_array *keys) {}
static void keyboard_handle_leave(void *data, struct wl_keyboard *keyboard, uint32_t serial,
                                 struct wl_surface *surface) {}
static void keyboard_handle_key(void *data, struct wl_keyboard *keyboard, uint32_t serial,
                               uint32_t time, uint32_t key, uint32_t state) {
    if (state == WL_KEYBOARD_KEY_STATE_PRESSED) {
     	if (key == 57 || key == 65) {
        	g_pauseRequested = true;
        } else if (key == 16) {
        	g_quitRequested = true;
        }
    }
}
static void keyboard_handle_modifiers(void *data, struct wl_keyboard *keyboard, uint32_t serial,
                                     uint32_t mods_depressed, uint32_t mods_latched,
                                     uint32_t mods_locked, uint32_t group) {}
static void keyboard_handle_repeat_info(void *data, struct wl_keyboard *keyboard,
                                       int32_t rate, int32_t delay) {}

static const struct wl_keyboard_listener keyboard_listener = {
    keyboard_handle_keymap,
    keyboard_handle_enter,
    keyboard_handle_leave,
    keyboard_handle_key,
    keyboard_handle_modifiers,
    keyboard_handle_repeat_info,
};

static void registry_handle_global(void *data, struct wl_registry *registry,
                                   uint32_t id, const char *interface, uint32_t version) {
    if (strcmp(interface, "wl_compositor") == 0) {
        compositor = static_cast<struct wl_compositor *>(wl_registry_bind(registry, id, &wl_compositor_interface, 1));
    } else if (strcmp(interface, "xdg_wm_base") == 0) {
        wm_base = static_cast<xdg_wm_base *>(wl_registry_bind(registry, id, &xdg_wm_base_interface, 1));
        xdg_wm_base_add_listener(wm_base, &xdg_wm_base_listener, NULL);
    } else if (strcmp(interface, "wl_seat") == 0) {
        seat = static_cast<struct wl_seat *>(wl_registry_bind(registry, id, &wl_seat_interface, 1));
        keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(keyboard, &keyboard_listener, NULL);
    }
}

static void registry_handle_global_remove(void *data, struct wl_registry *registry, uint32_t id) {}

static const struct wl_registry_listener registry_listener = {
    registry_handle_global,
    registry_handle_global_remove,
};

int main(int argc, char **argv) {
    display = wl_display_connect(NULL);
    if (!display) {
        fprintf(stderr, "Failed to connect to Wayland display.\n");
        return -1;
    }

    registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, NULL);
    wl_display_roundtrip(display);

    if (!compositor || !wm_base) {
        fprintf(stderr, "Failed to bind compositor or xdg_wm_base.\n");
        return -1;
    }

    egl_display = eglGetDisplay((EGLNativeDisplayType)display);
    if (egl_display == EGL_NO_DISPLAY || !eglInitialize(egl_display, NULL, NULL)) {
        fprintf(stderr, "Failed to initialize EGL.\n");
        return -1;
    }

    EGLint config_attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE
    };
    EGLint num_configs = 0;
    if (!eglChooseConfig(egl_display, config_attribs, &egl_config, 1, &num_configs) || num_configs == 0) {
        fprintf(stderr, "Failed to choose EGL config.\n");
        return -1;
    }

    EGLint context_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };
    egl_context = eglCreateContext(egl_display, egl_config, EGL_NO_CONTEXT, context_attribs);
    if (egl_context == EGL_NO_CONTEXT) {
        fprintf(stderr, "Failed to create EGL context.\n");
        return -1;
    }

    surface = wl_compositor_create_surface(compositor);
    xdg_surface = xdg_wm_base_get_xdg_surface(wm_base, surface);
    xdg_surface_add_listener(xdg_surface, &xdg_surface_listener, NULL);
    xdg_toplevel = xdg_surface_get_toplevel(xdg_surface);
    xdg_toplevel_set_title(xdg_toplevel, "Wayland EGL Window");
    wl_surface_commit(surface);
    wl_display_roundtrip(display);

    int width = WIN_W;
    int height = WIN_H;
    egl_window = wl_egl_window_create(surface, width, height);
    egl_surface = eglCreateWindowSurface(egl_display, egl_config, (EGLNativeWindowType)egl_window, NULL);
    if (!eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context)) {
        fprintf(stderr, "Failed to make EGL context current.\n");
        return -1;
    }

    glViewport(0, 0, width, height);
    glClearColor(0.05f, 0.06f, 0.09f, 1.0f);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    GLuint spriteProg = linkProgram(kSpriteVS, kSpriteFS);
    GLuint textProg = linkProgram(kTextVS, kTextFS);
    GLuint solidProg = linkProgram(kSolidVS, kSolidFS);

    GLint spritePosLoc = glGetAttribLocation(spriteProg, "aPos");
    GLint spriteUVLoc = glGetAttribLocation(spriteProg, "aUV");
    GLint spriteProj = glGetUniformLocation(spriteProg, "uProj");
    GLint spriteCenter = glGetUniformLocation(spriteProg, "uCenter");
    GLint spriteHalf = glGetUniformLocation(spriteProg, "uHalfSize");
    GLint spriteAngle = glGetUniformLocation(spriteProg, "uAngle");
    GLint spriteTint = glGetUniformLocation(spriteProg, "uTint");

    GLint textVertexLoc = glGetAttribLocation(textProg, "aVertex");
    GLint textProj = glGetUniformLocation(textProg, "uProj");
    GLint textColor = glGetUniformLocation(textProg, "uColor");
    GLint textTex = glGetUniformLocation(textProg, "uGlyph");

    GLint solidPosLoc = glGetAttribLocation(solidProg, "aPos");
    GLint solidProj = glGetUniformLocation(solidProg, "uProj");
    GLint solidColor = glGetUniformLocation(solidProg, "uColor");

    const float quad[] = {
        -1.0f, -1.0f, 0.0f, 0.0f,
         1.0f, -1.0f, 1.0f, 0.0f,
         1.0f,  1.0f, 1.0f, 1.0f,
        -1.0f, -1.0f, 0.0f, 0.0f,
         1.0f,  1.0f, 1.0f, 1.0f,
        -1.0f,  1.0f, 0.0f, 1.0f,
    };

    GLuint quadVbo = 0;
    glGenBuffers(1, &quadVbo);
    glBindBuffer(GL_ARRAY_BUFFER, quadVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    size_t texBytes = 0;
    std::vector<GLuint> textures = createTextureBank(TEX_COUNT, TEX_SIZE, &texBytes);
    printf("Texture bank: %d x %dx%d RGBA ~= %.1f MiB\n", TEX_COUNT, TEX_SIZE, TEX_SIZE, texBytes / (1024.0 * 1024.0));

    GLuint fontTex = createDigitAtlas();
    std::mt19937 rng(2024);
    std::uniform_real_distribution<float> u01(0.0f, 1.0f);
    std::vector<Sprite> sprites(SPRITE_COUNT);
    for (int i = 0; i < SPRITE_COUNT; ++i) {
        Sprite &s = sprites[i];
        s.x = u01(rng) * WIN_W;
        s.y = u01(rng) * WIN_H;
        s.vx = (u01(rng) - 0.5f) * 90.0f;
        s.vy = (u01(rng) - 0.5f) * 90.0f;
        s.half = 8.0f + u01(rng) * 28.0f;
        s.angle = u01(rng) * 6.2831853f;
        s.spin = (u01(rng) - 0.5f) * 2.5f;
        s.r = 0.55f + 0.45f * u01(rng);
        s.g = 0.55f + 0.45f * u01(rng);
        s.b = 0.55f + 0.45f * u01(rng);
        s.a = 0.65f + 0.35f * u01(rng);
        s.tex = i % TEX_COUNT;
    }

    float proj[16];
    makeOrtho(proj, 0.0f, (float)WIN_W, (float)WIN_H, 0.0f);
    double start = 0.0;
    double prev = 0.0;
    double pausedTotal = 0.0;
    double pauseStart = 0.0;
    time_t pauseWallTime = 0;

    printf("OpenGL/Wayland application initialized successfully.\n");

    while (wl_display_dispatch(display) != -1) {
        double now = getTimeSeconds();
        if (start == 0.0) {
            start = now;
            prev = now;
        }
        double dt = now - prev;
        prev = now;

	if (g_quitRequested) {
  	    printf("Exiting\n");
	    break;
	}

        if (g_pauseRequested) {
            g_pauseRequested = false;
            if (!g_paused) {
                pauseStart = now;
                pauseWallTime = time(NULL);
                if (swapTexturesToDisk(textures, TEX_SIZE, g_cachePath)) {
                    g_paused = true;
                    g_cachedTextures = true;
                    printf("%.1f MiB Texture data swapped to file %s.\n", texBytes / (1024.0 * 1024.0), g_cachePath);
                }
            } else {
                if (swapTexturesFromDisk(textures, TEX_SIZE, g_cachePath)) {
                    g_paused = false;
                    g_cachedTextures = false;
                    pausedTotal += now - pauseStart;
                    printf("Texture data restored from disk.\n");
                }
            }
            dt = 0.0;
        }

        glViewport(0, 0, width, height);
        glClearColor(0.05f, 0.06f, 0.09f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        if (!g_paused) {
            glUseProgram(spriteProg);
            glUniformMatrix4fv(spriteProj, 1, GL_FALSE, proj);
            glActiveTexture(GL_TEXTURE0);

            glBindBuffer(GL_ARRAY_BUFFER, quadVbo);
            glEnableVertexAttribArray(spritePosLoc);
            glVertexAttribPointer(spritePosLoc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
            glEnableVertexAttribArray(spriteUVLoc);
            glVertexAttribPointer(spriteUVLoc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)(2 * sizeof(float)));

            for (Sprite &s : sprites) {
                s.x += s.vx * (float)dt;
                s.y += s.vy * (float)dt;
                s.angle += s.spin * (float)dt;
                if (s.x < -s.half) s.x = WIN_W + s.half;
                if (s.x > WIN_W + s.half) s.x = -s.half;
                if (s.y < -s.half) s.y = WIN_H + s.half;
                if (s.y > WIN_H + s.half) s.y = -s.half;
                glBindTexture(GL_TEXTURE_2D, textures[s.tex]);
                glUniform2f(spriteCenter, s.x, s.y);
                glUniform2f(spriteHalf, s.half, s.half);
                glUniform1f(spriteAngle, s.angle);
                glUniform4f(spriteTint, s.r, s.g, s.b, s.a);
                glDrawArrays(GL_TRIANGLES, 0, 6);
            }
            glDisableVertexAttribArray(spritePosLoc);
            glDisableVertexAttribArray(spriteUVLoc);
        }

        // Format Clock and Elapsed Time text (freezes while paused)
        double currentRenderTime = g_paused ? pauseStart : now;
        double elapsed = currentRenderTime - start - pausedTotal;
        if (elapsed < 0.0) elapsed = 0.0;

        time_t render_t = g_paused ? pauseWallTime : time(NULL);
        struct tm tm_info;
        localtime_r(&render_t, &tm_info);

        char clockBuf[32];
        strftime(clockBuf, sizeof(clockBuf), "%H:%M:%S", &tm_info);
        char secondsBuf[32];
        snprintf(secondsBuf, sizeof(secondsBuf), "%.1f", elapsed);
        std::string timeText = std::string(clockBuf) + "  " + secondsBuf;

        float scale = 2.5f;
        float textWidth = timeText.size() * 7.0f * scale;
        float sx = (WIN_W - textWidth) * 0.5f;
        float sy = 15.0f;

        // 1. Draw semi-transparent background panel
        glUseProgram(solidProg);
        glUniformMatrix4fv(solidProj, 1, GL_FALSE, proj);
        glUniform4f(solidColor, 0.0f, 0.0f, 0.0f, 0.75f);
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        float padX = 12.0f;
        float padY = 8.0f;
        float bgRect[6][2] = {
            {sx - padX, sy - padY},
            {sx + textWidth + padX, sy - padY},
            {sx + textWidth + padX, sy + (8.0f * scale) + padY},
            {sx - padX, sy - padY},
            {sx + textWidth + padX, sy + (8.0f * scale) + padY},
            {sx - padX, sy + (8.0f * scale) + padY}
        };

        glEnableVertexAttribArray(solidPosLoc);
        glVertexAttribPointer(solidPosLoc, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), bgRect);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glDisableVertexAttribArray(solidPosLoc);

        // 2. Render Text Glyphs
        glUseProgram(textProg);
        glUniformMatrix4fv(textProj, 1, GL_FALSE, proj);
        glUniform1i(textTex, 0);
        glUniform3f(textColor, 1.0f, 1.0f, 1.0f);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, fontTex);
        glEnableVertexAttribArray(textVertexLoc);

        for (size_t i = 0; i < timeText.size(); ++i) {
            int idx = glyphIndex(timeText[i]);
            int gx = (idx % 8) * 8;
            int gy = (idx / 8) * 8;
            float u0 = gx / 64.0f;
            float v0 = gy / 64.0f;
            float u1 = (gx + 8) / 64.0f;
            float v1 = (gy + 8) / 64.0f;
            float w = 8.0f * scale;
            float h = 8.0f * scale;

            float verts[6][4] = {
                {sx,     sy,     u0, v0},
                {sx,     sy + h, u0, v1},
                {sx + w, sy + h, u1, v1},

                {sx,     sy,     u0, v0},
                {sx + w, sy + h, u1, v1},
                {sx + w, sy,     u1, v0}
            };
            glVertexAttribPointer(textVertexLoc, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), verts);
            glDrawArrays(GL_TRIANGLES, 0, 6);
            sx += 7.0f * scale;
        }
        glDisableVertexAttribArray(textVertexLoc);

        eglSwapBuffers(egl_display, egl_surface);
        wl_display_flush(display);
    }

    glDeleteTextures((GLsizei)textures.size(), textures.data());
    glDeleteTextures(1, &fontTex);
    glDeleteBuffers(1, &quadVbo);
    glDeleteProgram(spriteProg);
    glDeleteProgram(textProg);
    glDeleteProgram(solidProg);
    eglDestroySurface(egl_display, egl_surface);
    wl_egl_window_destroy(egl_window);
    xdg_toplevel_destroy(xdg_toplevel);
    xdg_surface_destroy(xdg_surface);
    wl_surface_destroy(surface);
    eglDestroyContext(egl_display, egl_context);
    eglTerminate(egl_display);
    xdg_wm_base_destroy(wm_base);
    wl_registry_destroy(registry);
    wl_display_disconnect(display);
    return 0;
}
