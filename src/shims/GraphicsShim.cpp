#include "kudroid/shims/GraphicsShim.h"
#include <dlfcn.h>
#include <cstdio>
#include <cstdint>
// This comes from kudroid_bridge.cpp
extern void* g_metalLayer;
extern int g_metalLayerWidth;
extern int g_metalLayerHeight;

namespace kudroid {

namespace {

extern "C" void* bionic_ANativeWindow_fromSurface(void* env, void* surface) {
    (void)env; (void)surface;
    // ANGLE on iOS uses CAMetalLayer/UIView as the native window!
    if (!g_metalLayer) {
        fprintf(stderr, "[GraphicsShim] ERROR: ANativeWindow_fromSurface called but g_metalLayer is NULL!\n");
    }
    return g_metalLayer;
}

extern "C" int bionic_ANativeWindow_getWidth(void* window) { (void)window; return g_metalLayerWidth; }
extern "C" int bionic_ANativeWindow_getHeight(void* window) { (void)window; return g_metalLayerHeight; }
extern "C" int bionic_ANativeWindow_setBuffersGeometry(void* window, int width, int height, int format) {
    (void)window; (void)width; (void)height; (void)format; return 0;
}
extern "C" void bionic_ANativeWindow_release(void* window) { (void)window; }
extern "C" void bionic_ANativeWindow_acquire(void* window) { (void)window; }

// ANativeWindow_Buffer — describes a locked buffer.
struct ANativeWindow_Buffer {
    int32_t width;
    int32_t height;
    int32_t stride;
    int32_t format;
    void* bits;
};

// ANativeWindow_lock — lock the window's buffer for drawing.
// For KuDroid, we return a dummy buffer so apps that draw via
// ANativeWindow_lock don't crash. Real blitting to Metal is a future
// enhancement (games typically use EGL/GLES instead).
extern "C" int bionic_ANativeWindow_lock(void* window, ANativeWindow_Buffer* outBuffer,
                                         void* inOutDirtyRect) {
    (void)window; (void)inOutDirtyRect;
    if (!outBuffer) return -1;
    static uint32_t dummyPixel = 0;
    outBuffer->width = g_metalLayerWidth;
    outBuffer->height = g_metalLayerHeight;
    outBuffer->stride = g_metalLayerWidth;
    outBuffer->format = 1; // WINDOW_FORMAT_RGBA_8888
    outBuffer->bits = &dummyPixel;
    return 0;
}

// ANativeWindow_unlockAndPost — unlock the buffer and post it.
extern "C" int bionic_ANativeWindow_unlockAndPost(void* window) {
    (void)window;
    return 0;
}

// --- EGL Overrides ---
typedef void* EGLDisplay;
typedef void* EGLNativeDisplayType;
typedef int EGLint;
typedef EGLDisplay (*PFN_eglGetPlatformDisplayEXT)(EGLint platform, void* native_display, const EGLint* attrib_list);
typedef void* (*PFN_eglGetProcAddress)(const char* procname);

extern "C" void* bionic_eglGetProcAddress(const char* procname) {
    if (!procname) return nullptr;
    fprintf(stdout, "[KuDroidGPU] eglGetProcAddress: requested %s\n", procname);
    auto host_func = (PFN_eglGetProcAddress) ::dlsym(RTLD_DEFAULT, "eglGetProcAddress");
    if (host_func) {
        void* addr = host_func(procname);
        fprintf(stdout, "[KuDroidGPU] eglGetProcAddress: %s returned %s\n", procname, addr ? "VALID" : "NULL");
        return addr;
    }
    fprintf(stdout, "[KuDroidGPU] eglGetProcAddress: host function not found in RTLD_DEFAULT\n");
    return nullptr;
}

static void* get_egl_func(const char* name) {
    void* func = ::dlsym(RTLD_DEFAULT, name);
    if (!func) {
        fprintf(stdout, "[KuDroidGPU] get_egl_func: %s not found via dlsym, trying eglGetProcAddress\n", name);
        auto host_get_proc = (PFN_eglGetProcAddress) ::dlsym(RTLD_DEFAULT, "eglGetProcAddress");
        if (host_get_proc) {
            func = host_get_proc(name);
        }
    }
    return func;
}

extern "C" EGLDisplay bionic_eglGetPlatformDisplayEXT(EGLint platform, void* native_display, const EGLint* attrib_list) {
    fprintf(stdout, "[KuDroidGPU] bionic_eglGetPlatformDisplayEXT called\n");
    auto host_func = (PFN_eglGetPlatformDisplayEXT) get_egl_func("eglGetPlatformDisplayEXT");
    if (host_func) {
        EGLDisplay dpy = host_func(platform, native_display, attrib_list);
        fprintf(stdout, "[KuDroidGPU] bionic_eglGetPlatformDisplayEXT returned %s\n", dpy ? "VALID" : "NULL");
        return dpy;
    }
    fprintf(stdout, "[KuDroidGPU] bionic_eglGetPlatformDisplayEXT: not found in host\n");
    return nullptr;
}

extern "C" EGLDisplay bionic_eglGetDisplay(EGLNativeDisplayType display_id) {
    fprintf(stdout, "[KuDroidGPU] bionic_eglGetDisplay called\n");
    auto host_func = (PFN_eglGetPlatformDisplayEXT) get_egl_func("eglGetPlatformDisplayEXT");
    if (host_func) {
        #define EGL_PLATFORM_ANGLE_ANGLE 0x3202
        #define EGL_PLATFORM_ANGLE_TYPE_ANGLE 0x3203
        #define EGL_PLATFORM_ANGLE_TYPE_VULKAN_ANGLE 0x3450
        #define EGL_PLATFORM_ANGLE_TYPE_METAL_ANGLE 0x3489
        #define EGL_PLATFORM_ANGLE_TYPE_DEFAULT_ANGLE 0x3204
        #define EGL_NONE 0x3038
        
        EGLint backends[] = {
            EGL_PLATFORM_ANGLE_TYPE_VULKAN_ANGLE,
            EGL_PLATFORM_ANGLE_TYPE_METAL_ANGLE,
            EGL_PLATFORM_ANGLE_TYPE_DEFAULT_ANGLE
        };
        
        for (int i = 0; i < 3; i++) {
            const EGLint attribs[] = {
                EGL_PLATFORM_ANGLE_TYPE_ANGLE, backends[i],
                EGL_NONE
            };
            EGLDisplay dpy = host_func(EGL_PLATFORM_ANGLE_ANGLE, display_id, attribs);
            if (dpy != nullptr) {
                fprintf(stdout, "[KuDroidGPU] bionic_eglGetDisplay: successfully got display via eglGetPlatformDisplayEXT\n");
                return dpy;
            }
        }
    }
    
    fprintf(stdout, "[KuDroidGPU] bionic_eglGetDisplay: falling back to eglGetDisplay\n");
    typedef EGLDisplay (*PFN_eglGetDisplay)(EGLNativeDisplayType);
    auto host_get_display = (PFN_eglGetDisplay) get_egl_func("eglGetDisplay");
    if (host_get_display) {
        EGLDisplay dpy = host_get_display(display_id);
        fprintf(stdout, "[KuDroidGPU] bionic_eglGetDisplay: fallback returned %s\n", dpy ? "VALID" : "NULL");
        return dpy;
    }
    
    fprintf(stdout, "[KuDroidGPU] bionic_eglGetDisplay: completely failed to find eglGetDisplay\n");
    return nullptr;
}

const SymbolEntry kGraphicsSymbols[] = {
    {"ANativeWindow_fromSurface", reinterpret_cast<void*>(&bionic_ANativeWindow_fromSurface)},
    {"ANativeWindow_getWidth", reinterpret_cast<void*>(&bionic_ANativeWindow_getWidth)},
    {"ANativeWindow_getHeight", reinterpret_cast<void*>(&bionic_ANativeWindow_getHeight)},
    {"ANativeWindow_setBuffersGeometry", reinterpret_cast<void*>(&bionic_ANativeWindow_setBuffersGeometry)},
    {"ANativeWindow_release", reinterpret_cast<void*>(&bionic_ANativeWindow_release)},
    {"ANativeWindow_acquire", reinterpret_cast<void*>(&bionic_ANativeWindow_acquire)},
    {"ANativeWindow_lock", reinterpret_cast<void*>(&bionic_ANativeWindow_lock)},
    {"ANativeWindow_unlockAndPost", reinterpret_cast<void*>(&bionic_ANativeWindow_unlockAndPost)},
    {"eglGetDisplay", reinterpret_cast<void*>(&bionic_eglGetDisplay)},
    {"eglGetPlatformDisplayEXT", reinterpret_cast<void*>(&bionic_eglGetPlatformDisplayEXT)},
    {"eglGetProcAddress", reinterpret_cast<void*>(&bionic_eglGetProcAddress)},
};

} // namespace

const SymbolEntry* get_graphics_symbols(size_t* count) {
    if (count) {
        *count = sizeof(kGraphicsSymbols) / sizeof(SymbolEntry);
    }
    return kGraphicsSymbols;
}

} // namespace kudroid
