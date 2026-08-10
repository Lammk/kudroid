#include "kudroid/shims/GraphicsShim.h"
#include <dlfcn.h>
#include <cstdio>
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

// --- EGL Overrides ---
typedef void* EGLDisplay;
typedef void* EGLNativeDisplayType;
typedef int EGLint;
typedef EGLDisplay (*PFN_eglGetPlatformDisplayEXT)(EGLint platform, void* native_display, const EGLint* attrib_list);

extern "C" EGLDisplay bionic_eglGetPlatformDisplayEXT(EGLint platform, void* native_display, const EGLint* attrib_list) {
    auto host_func = (PFN_eglGetPlatformDisplayEXT) ::dlsym(RTLD_DEFAULT, "eglGetPlatformDisplayEXT");
    if (host_func) {
        return host_func(platform, native_display, attrib_list);
    }
    return nullptr;
}

extern "C" EGLDisplay bionic_eglGetDisplay(EGLNativeDisplayType display_id) {
    // Android apps often call eglGetDisplay(EGL_DEFAULT_DISPLAY).
    // On iOS with ANGLE, we must use eglGetPlatformDisplayEXT to specify the backend.
    auto host_func = (PFN_eglGetPlatformDisplayEXT) ::dlsym(RTLD_DEFAULT, "eglGetPlatformDisplayEXT");
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
                return dpy;
            }
        }
    }
    
    // Fallback to host eglGetDisplay if extension is missing (unlikely on ANGLE)
    typedef EGLDisplay (*PFN_eglGetDisplay)(EGLNativeDisplayType);
    auto host_get_display = (PFN_eglGetDisplay) ::dlsym(RTLD_DEFAULT, "eglGetDisplay");
    if (host_get_display) {
        return host_get_display(display_id);
    }
    
    return nullptr;
}

const SymbolEntry kGraphicsSymbols[] = {
    {"ANativeWindow_fromSurface", reinterpret_cast<void*>(&bionic_ANativeWindow_fromSurface)},
    {"ANativeWindow_getWidth", reinterpret_cast<void*>(&bionic_ANativeWindow_getWidth)},
    {"ANativeWindow_getHeight", reinterpret_cast<void*>(&bionic_ANativeWindow_getHeight)},
    {"ANativeWindow_setBuffersGeometry", reinterpret_cast<void*>(&bionic_ANativeWindow_setBuffersGeometry)},
    {"ANativeWindow_release", reinterpret_cast<void*>(&bionic_ANativeWindow_release)},
    {"ANativeWindow_acquire", reinterpret_cast<void*>(&bionic_ANativeWindow_acquire)},
    {"eglGetDisplay", reinterpret_cast<void*>(&bionic_eglGetDisplay)},
    {"eglGetPlatformDisplayEXT", reinterpret_cast<void*>(&bionic_eglGetPlatformDisplayEXT)},
};

} // namespace

const SymbolEntry* get_graphics_symbols(size_t* count) {
    if (count) {
        *count = sizeof(kGraphicsSymbols) / sizeof(SymbolEntry);
    }
    return kGraphicsSymbols;
}

} // namespace kudroid
