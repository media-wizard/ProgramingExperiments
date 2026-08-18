// OpenGL demo: 640x480 window, seconds counter + wall clock text,
// and a large number of textured sprites to keep a lot of texture data resident on the GPU.
//
// Build: make        (see Makefile for the required -dev packages)

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <random>
#include <string>
#include <vector>

static const int WIN_W = 640;
static const int WIN_H = 480;

// Texture bank: TEX_COUNT textures of TEX_SIZE^2 RGBA8.
// Default: 192 * 512 * 512 * 4 bytes ~= 192 MiB of GPU texture memory
// (~256 MiB with mipmaps). Override with the environment variables
// GLDEMO_TEX_COUNT / GLDEMO_TEX_SIZE.
static int TEX_COUNT = 192;
static int TEX_SIZE = 512;
static int SPRITE_COUNT = 1500;

// ---------------------------------------------------------------- shaders

static const char *kSpriteVS = R"(#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
uniform mat4 uProj;
uniform vec2 uCenter;
uniform vec2 uHalfSize;
uniform float uAngle;
out vec2 vUV;
void main() {
    float c = cos(uAngle), s = sin(uAngle);
    vec2 p = aPos * uHalfSize;
    p = vec2(p.x * c - p.y * s, p.x * s + p.y * c) + uCenter;
    gl_Position = uProj * vec4(p, 0.0, 1.0);
    vUV = aUV;
}
)";

static const char *kSpriteFS = R"(#version 330 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uTex;
uniform vec4 uTint;
void main() {
    FragColor = texture(uTex, vUV) * uTint;
}
)";

static const char *kTextVS = R"(#version 330 core
layout(location = 0) in vec4 aVertex; // xy = position, zw = uv
uniform mat4 uProj;
out vec2 vUV;
void main() {
    gl_Position = uProj * vec4(aVertex.xy, 0.0, 1.0);
    vUV = aVertex.zw;
}
)";

static const char *kTextFS = R"(#version 330 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uGlyph;
uniform vec3 uColor;
void main() {
    FragColor = vec4(uColor, texture(uGlyph, vUV).r);
}
)";

// ---------------------------------------------------------------- helpers

static GLuint compileShader(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        std::fprintf(stderr, "Shader compile error: %s\n", log);
        std::exit(EXIT_FAILURE);
    }
    return s;
}

static GLuint linkProgram(const char *vs, const char *fs) {
    GLuint v = compileShader(GL_VERTEX_SHADER, vs);
    GLuint f = compileShader(GL_FRAGMENT_SHADER, fs);
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        std::fprintf(stderr, "Program link error: %s\n", log);
        std::exit(EXIT_FAILURE);
    }
    glDeleteShader(v);
    glDeleteShader(f);
    return p;
}

// Column-major orthographic projection, top-left origin.
static void ortho(float l, float r, float b, float t, float *m) {
    std::memset(m, 0, 16 * sizeof(float));
    m[0] = 2.0f / (r - l);
    m[5] = 2.0f / (t - b);
    m[10] = -1.0f;
    m[12] = -(r + l) / (r - l);
    m[13] = -(t + b) / (t - b);
    m[15] = 1.0f;
}

static int envInt(const char *name, int fallback) {
    const char *v = std::getenv(name);
    if (!v) return fallback;
    int n = std::atoi(v);
    return n > 0 ? n : fallback;
}

static bool g_togglePause = false;

static void keyCallback(GLFWwindow *win, int key, int, int action, int) {
    if (action != GLFW_PRESS) return;
    if (key == GLFW_KEY_SPACE) g_togglePause = true;
    if (key == GLFW_KEY_ESCAPE) glfwSetWindowShouldClose(win, GLFW_TRUE);
}

// ---------------------------------------------------------------- text

struct Glyph {
    GLuint tex;
    int w, h;
    int bearingX, bearingY;
    long advance; // in pixels
};

class TextRenderer {
public:
    bool init(const char *fontPath, int pixelHeight) {
        FT_Library ft;
        if (FT_Init_FreeType(&ft)) {
            std::fprintf(stderr, "FreeType init failed\n");
            return false;
        }
        FT_Face face;
        if (FT_New_Face(ft, fontPath, 0, &face)) {
            std::fprintf(stderr, "Failed to load font: %s\n", fontPath);
            FT_Done_FreeType(ft);
            return false;
        }
        FT_Set_Pixel_Sizes(face, 0, pixelHeight);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

        for (unsigned char c = 32; c < 127; ++c) {
            if (FT_Load_Char(face, c, FT_LOAD_RENDER)) continue;
            GLuint tex = 0;
            glGenTextures(1, &tex);
            glBindTexture(GL_TEXTURE_2D, tex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, face->glyph->bitmap.width,
                         face->glyph->bitmap.rows, 0, GL_RED, GL_UNSIGNED_BYTE,
                         face->glyph->bitmap.buffer);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glyphs_[c] = Glyph{tex, (int)face->glyph->bitmap.width,
                               (int)face->glyph->bitmap.rows, face->glyph->bitmap_left,
                               face->glyph->bitmap_top, face->glyph->advance.x >> 6};
        }
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glBindTexture(GL_TEXTURE_2D, 0);
        FT_Done_Face(face);
        FT_Done_FreeType(ft);

        prog_ = linkProgram(kTextVS, kTextFS);
        glGenVertexArrays(1, &vao_);
        glGenBuffers(1, &vbo_);
        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6 * 4, nullptr, GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
        glBindVertexArray(0);
        return true;
    }

    // x, y is the text baseline start, in window pixels (y down).
    void draw(const std::string &text, float x, float y, float scale, float r, float g,
              float b, const float *proj) {
        glUseProgram(prog_);
        glUniformMatrix4fv(glGetUniformLocation(prog_, "uProj"), 1, GL_FALSE, proj);
        glUniform3f(glGetUniformLocation(prog_, "uColor"), r, g, b);
        glUniform1i(glGetUniformLocation(prog_, "uGlyph"), 0);
        glActiveTexture(GL_TEXTURE0);
        glBindVertexArray(vao_);

        for (char ch : text) {
            auto it = glyphs_.find((unsigned char)ch);
            if (it == glyphs_.end()) continue;
            const Glyph &gl = it->second;
            float xp = x + gl.bearingX * scale;
            float yp = y - gl.bearingY * scale;
            float w = gl.w * scale;
            float h = gl.h * scale;
            float verts[6][4] = {
                {xp, yp, 0.0f, 0.0f},        {xp, yp + h, 0.0f, 1.0f},
                {xp + w, yp + h, 1.0f, 1.0f}, {xp, yp, 0.0f, 0.0f},
                {xp + w, yp + h, 1.0f, 1.0f}, {xp + w, yp, 1.0f, 0.0f},
            };
            glBindTexture(GL_TEXTURE_2D, gl.tex);
            glBindBuffer(GL_ARRAY_BUFFER, vbo_);
            glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(verts), verts);
            glDrawArrays(GL_TRIANGLES, 0, 6);
            x += gl.advance * scale;
        }
        glBindVertexArray(0);
    }

    float width(const std::string &text, float scale) const {
        float w = 0.0f;
        for (char ch : text) {
            auto it = glyphs_.find((unsigned char)ch);
            if (it != glyphs_.end()) w += it->second.advance * scale;
        }
        return w;
    }

private:
    std::map<unsigned char, Glyph> glyphs_;
    GLuint prog_ = 0, vao_ = 0, vbo_ = 0;
};

// ---------------------------------------------------------------- textures

// Every texture gets unique pixel content so the driver cannot dedupe or
// alias them; this really does allocate TEX_COUNT * TEX_SIZE^2 * 4 bytes.
static std::vector<GLuint> createTextureBank(int count, int size, size_t *bytesOut) {
    std::vector<GLuint> texs(count, 0);
    std::vector<unsigned char> pixels((size_t)size * size * 4);
    std::mt19937 rng(1234);

    glGenTextures(count, texs.data());
    for (int i = 0; i < count; ++i) {
        float hue = (float)i / (float)count * 6.2831853f;
        float rr = 0.5f + 0.5f * std::sin(hue);
        float gg = 0.5f + 0.5f * std::sin(hue + 2.094f);
        float bb = 0.5f + 0.5f * std::sin(hue + 4.188f);
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                float fx = (float)x / size, fy = (float)y / size;
                float pat = 0.5f + 0.5f * std::sin((fx * 12.0f + i * 0.37f) * 3.14159f) *
                                       std::cos((fy * 12.0f + i * 0.11f) * 3.14159f);
                float noise = (rng() & 0xFF) / 255.0f * 0.25f;
                size_t o = ((size_t)y * size + x) * 4;
                pixels[o + 0] = (unsigned char)(255.0f * std::fmin(1.0f, rr * pat + noise));
                pixels[o + 1] = (unsigned char)(255.0f * std::fmin(1.0f, gg * pat + noise));
                pixels[o + 2] = (unsigned char)(255.0f * std::fmin(1.0f, bb * pat + noise));
                pixels[o + 3] = 255;
            }
        }
        glBindTexture(GL_TEXTURE_2D, texs[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, size, size, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, pixels.data());
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        if ((i + 1) % 16 == 0 || i + 1 == count)
            std::printf("Uploaded %d/%d textures\n", i + 1, count);
    }
    glBindTexture(GL_TEXTURE_2D, 0);

    // Base level plus the full mip chain (~1.333x).
    size_t base = (size_t)count * size * size * 4;
    *bytesOut = base + base / 3;
    return texs;
}

static void uploadTexture(GLuint tex, int size, const unsigned char *pixels) {
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 pixels);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
}

// Reads every texture back from the GPU into a cache file, then deletes the GL
// objects so the driver can release the VRAM.
static bool swapTexturesToDisk(std::vector<GLuint> &texs, int size, const char *path) {
    FILE *f = std::fopen(path, "wb");
    if (!f) {
        std::fprintf(stderr, "Cannot open cache file %s for writing\n", path);
        return false;
    }
    const size_t bytes = (size_t)size * size * 4;
    std::vector<unsigned char> buf(bytes);
    for (GLuint t : texs) {
        glBindTexture(GL_TEXTURE_2D, t);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, buf.data());
        if (std::fwrite(buf.data(), 1, bytes, f) != bytes) {
            std::fprintf(stderr, "Short write to %s\n", path);
            std::fclose(f);
            return false;
        }
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    std::fflush(f);
    std::fclose(f);

    glDeleteTextures((GLsizei)texs.size(), texs.data());
    std::fill(texs.begin(), texs.end(), 0u);
    glFinish();
    return true;
}

static bool swapTexturesFromDisk(std::vector<GLuint> &texs, int size, const char *path) {
    FILE *f = std::fopen(path, "rb");
    if (!f) {
        std::fprintf(stderr, "Cannot open cache file %s for reading\n", path);
        return false;
    }
    const size_t bytes = (size_t)size * size * 4;
    std::vector<unsigned char> buf(bytes);
    glGenTextures((GLsizei)texs.size(), texs.data());
    for (size_t i = 0; i < texs.size(); ++i) {
        if (std::fread(buf.data(), 1, bytes, f) != bytes) {
            std::fprintf(stderr, "Short read from %s\n", path);
            std::fclose(f);
            return false;
        }
        uploadTexture(texs[i], size, buf.data());
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    std::fclose(f);
    std::remove(path);
    glFinish();
    return true;
}

// ---------------------------------------------------------------- sprites

struct Sprite {
    float x, y, vx, vy;
    float half;
    float angle, spin;
    float r, g, b, a;
    int tex;
};

int main() {
    TEX_COUNT = envInt("GLDEMO_TEX_COUNT", TEX_COUNT);
    TEX_SIZE = envInt("GLDEMO_TEX_SIZE", TEX_SIZE);
    SPRITE_COUNT = envInt("GLDEMO_SPRITES", SPRITE_COUNT);

    if (!glfwInit()) {
        std::fprintf(stderr, "glfwInit failed\n");
        return EXIT_FAILURE;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    GLFWwindow *win = glfwCreateWindow(WIN_W, WIN_H, "OpenGL Clock + Texture Load", nullptr, nullptr);
    if (!win) {
        std::fprintf(stderr, "Failed to create window\n");
        glfwTerminate();
        return EXIT_FAILURE;
    }
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);
    glfwSetKeyCallback(win, keyCallback);

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        std::fprintf(stderr, "glewInit failed\n");
        glfwTerminate();
        return EXIT_FAILURE;
    }
    glGetError(); // GLEW can leave a stale INVALID_ENUM on core profiles

    std::printf("Renderer: %s\nGL version: %s\n", glGetString(GL_RENDERER),
                glGetString(GL_VERSION));

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    size_t texBytes = 0;
    std::vector<GLuint> textures = createTextureBank(TEX_COUNT, TEX_SIZE, &texBytes);
    double texMiB = texBytes / (1024.0 * 1024.0);
    std::printf("Texture bank: %d x %dx%d RGBA8 ~= %.1f MiB on the GPU\n", TEX_COUNT,
                TEX_SIZE, TEX_SIZE, texMiB);

    // Unit quad, centred on the origin.
    const float quad[] = {
        -1.f, -1.f, 0.f, 0.f, 1.f, -1.f, 1.f, 0.f, 1.f, 1.f, 1.f, 1.f,
        -1.f, -1.f, 0.f, 0.f, 1.f,  1.f, 1.f, 1.f, -1.f, 1.f, 0.f, 1.f,
    };
    GLuint quadVAO, quadVBO;
    glGenVertexArrays(1, &quadVAO);
    glGenBuffers(1, &quadVBO);
    glBindVertexArray(quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          (void *)(2 * sizeof(float)));
    glBindVertexArray(0);

    GLuint spriteProg = linkProgram(kSpriteVS, kSpriteFS);
    const GLint uProj = glGetUniformLocation(spriteProg, "uProj");
    const GLint uCenter = glGetUniformLocation(spriteProg, "uCenter");
    const GLint uHalfSize = glGetUniformLocation(spriteProg, "uHalfSize");
    const GLint uAngle = glGetUniformLocation(spriteProg, "uAngle");
    const GLint uTint = glGetUniformLocation(spriteProg, "uTint");
    glUseProgram(spriteProg);
    glUniform1i(glGetUniformLocation(spriteProg, "uTex"), 0);

    TextRenderer text;
    const char *fonts[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationMono-Bold.ttf",
    };
    bool fontOk = false;
    for (const char *f : fonts) {
        if (text.init(f, 48)) {
            fontOk = true;
            break;
        }
    }
    if (!fontOk) {
        std::fprintf(stderr, "No usable font found (install fonts-dejavu-core)\n");
        return EXIT_FAILURE;
    }

    std::mt19937 rng(2024);
    std::uniform_real_distribution<float> u01(0.0f, 1.0f);
    std::vector<Sprite> sprites(SPRITE_COUNT);
    for (int i = 0; i < SPRITE_COUNT; ++i) {
        Sprite &s = sprites[i];
        s.x = u01(rng) * WIN_W;
        s.y = u01(rng) * WIN_H;
        s.vx = (u01(rng) - 0.5f) * 90.0f;
        s.vy = (u01(rng) - 0.5f) * 90.0f;
        s.half = 8.0f + u01(rng) * 26.0f;
        s.angle = u01(rng) * 6.2831853f;
        s.spin = (u01(rng) - 0.5f) * 2.5f;
        s.r = 0.55f + 0.45f * u01(rng);
        s.g = 0.55f + 0.45f * u01(rng);
        s.b = 0.55f + 0.45f * u01(rng);
        s.a = 0.65f + 0.35f * u01(rng);
        s.tex = i % TEX_COUNT; // every texture stays in use every frame
    }

    float proj[16];
    ortho(0.0f, (float)WIN_W, (float)WIN_H, 0.0f, proj);

    const char *cachePath = std::getenv("GLDEMO_CACHE");
    if (!cachePath) cachePath = "/tmp/glclock_texcache.bin";

    const double start = glfwGetTime();
    double prev = start;
    double fpsAccum = 0.0;
    int fpsFrames = 0;
    double fps = 0.0;
    double pausedTotal = 0.0; // wall time spent paused, excluded from the counter
    double pauseStart = 0.0;
    bool paused = false;
    double swapSeconds = 0.0;

    while (!glfwWindowShouldClose(win)) {
        double now = glfwGetTime();
        float dt = (float)(now - prev);
        prev = now;

        if (g_togglePause) {
            g_togglePause = false;
            double t0 = glfwGetTime();
            if (!paused) {
                if (swapTexturesToDisk(textures, TEX_SIZE, cachePath)) {
                    paused = true;
                    pauseStart = t0;
                    swapSeconds = glfwGetTime() - t0;
                    std::printf("Swapped out %.1f MiB to %s in %.2f s\n", texMiB, cachePath,
                                swapSeconds);
                }
            } else {
                if (swapTexturesFromDisk(textures, TEX_SIZE, cachePath)) {
                    paused = false;
                    pausedTotal += t0 - pauseStart;
                    swapSeconds = glfwGetTime() - t0;
                    std::printf("Swapped in %.1f MiB from disk in %.2f s\n", texMiB,
                                swapSeconds);
                }
            }
            prev = glfwGetTime(); // drop the swap time so sprites do not jump
            dt = 0.0f;
        }

        if (!paused) {
            fpsAccum += dt;
            if (++fpsFrames >= 30) {
                fps = fpsFrames / fpsAccum;
                fpsFrames = 0;
                fpsAccum = 0.0;
            }
        }

        glClearColor(0.05f, 0.06f, 0.09f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        if (paused) {
            double elapsedPaused = pauseStart - start - pausedTotal;
            char secBuf[64];
            std::snprintf(secBuf, sizeof(secBuf), "Seconds: %.1f s", elapsedPaused);
            std::time_t t = std::time(nullptr);
            std::tm lt{};
            localtime_r(&t, &lt);
            char clockBuf[64];
            std::strftime(clockBuf, sizeof(clockBuf), "%H:%M:%S", &lt);
            char info[160];
            std::snprintf(info, sizeof(info),
                          "PAUSED - %.0f MiB of textures swapped to disk (%.2f s)", texMiB,
                          swapSeconds);
            std::string hint = "press SPACE to swap in and resume";

            text.draw(clockBuf, (WIN_W - text.width(clockBuf, 1.0f)) * 0.5f, 90.0f, 1.0f,
                      1.0f, 1.0f, 1.0f, proj);
            text.draw(secBuf, (WIN_W - text.width(secBuf, 0.5f)) * 0.5f, 130.0f, 0.5f,
                      1.0f, 0.85f, 0.25f, proj);
            text.draw(info, (WIN_W - text.width(info, 0.4f)) * 0.5f, 240.0f, 0.4f, 1.0f,
                      0.35f, 0.35f, proj);
            text.draw(hint, (WIN_W - text.width(hint, 0.32f)) * 0.5f, 275.0f, 0.32f, 0.7f,
                      0.7f, 0.7f, proj);

            glfwSwapBuffers(win);
            glfwWaitEventsTimeout(0.25);
            continue;
        }

        glUseProgram(spriteProg);
        glUniformMatrix4fv(uProj, 1, GL_FALSE, proj);
        glActiveTexture(GL_TEXTURE0);
        glBindVertexArray(quadVAO);
        for (Sprite &s : sprites) {
            s.x += s.vx * dt;
            s.y += s.vy * dt;
            s.angle += s.spin * dt;
            if (s.x < -s.half) s.x = WIN_W + s.half;
            if (s.x > WIN_W + s.half) s.x = -s.half;
            if (s.y < -s.half) s.y = WIN_H + s.half;
            if (s.y > WIN_H + s.half) s.y = -s.half;

            glBindTexture(GL_TEXTURE_2D, textures[s.tex]);
            glUniform2f(uCenter, s.x, s.y);
            glUniform2f(uHalfSize, s.half, s.half);
            glUniform1f(uAngle, s.angle);
            glUniform4f(uTint, s.r, s.g, s.b, s.a);
            glDrawArrays(GL_TRIANGLES, 0, 6);
        }
        glBindVertexArray(0);

        // Overlay text.
        double elapsed = now - start - pausedTotal;
        char secBuf[64];
        std::snprintf(secBuf, sizeof(secBuf), "%.1f s", elapsed);

        std::time_t t = std::time(nullptr);
        std::tm lt{};
        localtime_r(&t, &lt);
        char clockBuf[64];
        std::strftime(clockBuf, sizeof(clockBuf), "%H:%M:%S", &lt);

        char infoBuf[160];
        std::snprintf(infoBuf, sizeof(infoBuf),
                      "%d sprites | %d tex | %.0f MiB | %.0f fps | SPACE = swap out",
                      SPRITE_COUNT, TEX_COUNT, texMiB, fps);

        std::string secStr = std::string("Seconds: ") + secBuf;
        text.draw(clockBuf, (WIN_W - text.width(clockBuf, 1.0f)) * 0.5f, 90.0f, 1.0f,
                  1.0f, 1.0f, 1.0f, proj);
        text.draw(secStr, (WIN_W - text.width(secStr, 0.5f)) * 0.5f, 130.0f, 0.5f, 1.0f,
                  0.85f, 0.25f, proj);
        text.draw(infoBuf, 8.0f, WIN_H - 12.0f, 0.32f, 0.6f, 0.85f, 1.0f, proj);

        glfwSwapBuffers(win);
        glfwPollEvents();
    }

    if (paused)
        std::remove(cachePath);
    else
        glDeleteTextures((GLsizei)textures.size(), textures.data());
    glDeleteBuffers(1, &quadVBO);
    glDeleteVertexArrays(1, &quadVAO);
    glDeleteProgram(spriteProg);
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
