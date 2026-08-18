#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

// Global EGL variables
EGLDisplay egl_display;
EGLConfig egl_config;
EGLContext egl_context;
EGLSurface egl_surface;

// XDG Shell WM Base Ping Handler
static void xdg_wm_base_ping(void *data, struct xdg_wm_base *wm_base, uint32_t serial) {
    xdg_wm_base_pong(wm_base, serial);
}

static const struct xdg_wm_base_listener xdg_wm_base_listener = {
    xdg_wm_base_ping,
};

// XDG Surface Configure Handler
static void xdg_surface_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial) {
    xdg_surface_ack_configure(xdg_surface, serial);
}

static const struct xdg_surface_listener xdg_surface_listener = {
    xdg_surface_configure,
};

// Registry listeners to detect Wayland globals
static void registry_handle_global(void *data, struct wl_registry *registry, 
                                   uint32_t id, const char *interface, uint32_t version) {
    if (strcmp(interface, "wl_compositor") == 0) {
        compositor = static_cast<struct wl_compositor*>(wl_registry_bind(registry, id, &wl_compositor_interface, 1));
    } else if (strcmp(interface, "xdg_wm_base") == 0) {
        wm_base = static_cast<xdg_wm_base *>(
            wl_registry_bind(registry, id, &xdg_wm_base_interface, 1));
        xdg_wm_base_add_listener(wm_base, &xdg_wm_base_listener, NULL);
    }
}

static void registry_handle_global_remove(void *data, struct wl_registry *registry, uint32_t id) {}

static const struct wl_registry_listener registry_listener = {
    registry_handle_global,
    registry_handle_global_remove
};

int main(int argc, char **argv) {
    // 1. Connect to Display
    display = wl_display_connect(NULL);
    if (!display) {
        fprintf(stderr, "Failed to connect to Wayland display.\n");
        return -1;
    }

    // 2. Get Registry and Bind Globals
    registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, NULL);
    wl_display_roundtrip(display); // Sync to ensure compositor is bound

    if (!compositor || !wm_base) {
        fprintf(stderr, "Failed to bind compositor or xdg_wm_base.\n");
        return -1;
    }

    // 3. Initialize EGL
    egl_display = eglGetDisplay((EGLNativeDisplayType)display);
    if (egl_display == EGL_NO_DISPLAY || !eglInitialize(egl_display, NULL, NULL)) {
        fprintf(stderr, "Failed to initialize EGL.\n");
        return -1;
    }

    // 4. EGL Config & Context
    EGLint config_attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE
    };
    EGLint num_configs;
    eglChooseConfig(egl_display, config_attribs, &egl_config, 1, &num_configs);

    EGLint context_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };
    egl_context = eglCreateContext(egl_display, egl_config, EGL_NO_CONTEXT, context_attribs);

    // 5. Create Wayland Surface and Assign XDG Toplevel Role
    surface = wl_compositor_create_surface(compositor);
    xdg_surface = xdg_wm_base_get_xdg_surface(wm_base, surface);
    xdg_surface_add_listener(xdg_surface, &xdg_surface_listener, NULL);

    xdg_toplevel = xdg_surface_get_toplevel(xdg_surface);
    xdg_toplevel_set_title(xdg_toplevel, "Wayland EGL Window");

    // Commit surface to trigger initial compositor configure sequence
    wl_surface_commit(surface);
    wl_display_roundtrip(display);

    // 6. Create Native Window Wrapper and EGL Surface
    int width = 800;
    int height = 600;
    egl_window = wl_egl_window_create(surface, width, height);
    egl_surface = eglCreateWindowSurface(egl_display, egl_config, (EGLNativeWindowType)egl_window, NULL);
    eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context);

    printf("OpenGL/Wayland application initialized successfully.\n");

    // 7. Event Loop & Rendering
    while (wl_display_dispatch(display) != -1) {
        glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        eglSwapBuffers(egl_display, egl_surface);
    }

    // Cleanup
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
