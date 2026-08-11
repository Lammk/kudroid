#include "kudroid/shims/GraphicsShim.h"
#include <dlfcn.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
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
    
    size_t count = 0;
    const kudroid::SymbolEntry* symbols = kudroid::get_graphics_symbols(&count);
    for (size_t i = 0; i < count; ++i) {
        if (strcmp(symbols[i].name, procname) == 0) {
            fprintf(stdout, "[KuDroidGPU] eglGetProcAddress: %s intercepted by GraphicsShim\n", procname);
            return symbols[i].address;
        }
    }

    // Resolve from the ANGLE handle directly (loaded RTLD_LOCAL).
    static void* egl_handle = nullptr;
    if (!egl_handle) {
        egl_handle = ::dlopen("@executable_path/Frameworks/libEGL.framework/libEGL", RTLD_NOW | RTLD_LOCAL);
    }
    if (egl_handle) {
        auto host_func = (PFN_eglGetProcAddress) ::dlsym(egl_handle, "eglGetProcAddress");
        if (host_func) {
            void* addr = host_func(procname);
            fprintf(stdout, "[KuDroidGPU] eglGetProcAddress: %s returned %s\n", procname, addr ? "VALID" : "NULL");
            return addr;
        }
    }
    // Fallback to RTLD_DEFAULT.
    auto host_func = (PFN_eglGetProcAddress) ::dlsym(RTLD_DEFAULT, "eglGetProcAddress");
    if (host_func) {
        void* addr = host_func(procname);
        fprintf(stdout, "[KuDroidGPU] eglGetProcAddress: %s returned %s\n", procname, addr ? "VALID" : "NULL");
        return addr;
    }
    fprintf(stdout, "[KuDroidGPU] eglGetProcAddress: host function not found\n");
    return nullptr;
}

static void* get_egl_func(const char* name) {
    // ANGLE is loaded with RTLD_LOCAL, so its symbols are NOT in RTLD_DEFAULT.
    // Resolve directly from the ANGLE framework handle instead.
    static void* egl_handle = nullptr;
    if (!egl_handle) {
        egl_handle = ::dlopen("@executable_path/Frameworks/libEGL.framework/libEGL", RTLD_NOW | RTLD_LOCAL);
        if (!egl_handle) {
            fprintf(stderr, "[KuDroidGPU] FATAL: dlopen libEGL failed: %s\n", dlerror());
            // Try alternative path without @executable_path
            egl_handle = ::dlopen("Frameworks/libEGL.framework/libEGL", RTLD_NOW | RTLD_LOCAL);
            if (!egl_handle) {
                fprintf(stderr, "[KuDroidGPU] FATAL: alternative dlopen also failed: %s\n", dlerror());
            } else {
                fprintf(stderr, "[KuDroidGPU] SUCCESS: alternative dlopen loaded libEGL\n");
            }
        } else {
            fprintf(stderr, "[KuDroidGPU] SUCCESS: dlopen loaded libEGL\n");
        }
    }
    if (egl_handle) {
        void* func = ::dlsym(egl_handle, name);
        if (func) return func;
    }
    // Fallback: try RTLD_DEFAULT (works if ANGLE was loaded RTLD_GLOBAL).
    void* func = ::dlsym(RTLD_DEFAULT, name);
    if (func) return func;
    // Last resort: eglGetProcAddress.
    auto host_get_proc = (PFN_eglGetProcAddress) ::dlsym(RTLD_DEFAULT, "eglGetProcAddress");
    if (host_get_proc) {
        func = host_get_proc(name);
    }
    return func;
}

extern "C" EGLDisplay bionic_eglGetPlatformDisplayEXT(EGLint platform, void* native_display, const EGLint* attrib_list) {
    (void)native_display;
    fprintf(stdout, "[KuDroidGPU] bionic_eglGetPlatformDisplayEXT called\n");
    auto host_func = (PFN_eglGetPlatformDisplayEXT) get_egl_func("eglGetPlatformDisplayEXT");
    if (host_func) {
        // IMPORTANT: eglGetDisplay/eglGetPlatformDisplayEXT must ALWAYS use
        // EGL_DEFAULT_DISPLAY (0). CAMetalLayer is ONLY for eglCreateWindowSurface.
        // Force native_display to 0 regardless of what the Android game passed.
        EGLDisplay dpy = host_func(platform, (void*)0, attrib_list);
        fprintf(stdout, "[KuDroidGPU] bionic_eglGetPlatformDisplayEXT returned %s\n", dpy ? "VALID" : "NULL");
        return dpy;
    }
    fprintf(stdout, "[KuDroidGPU] bionic_eglGetPlatformDisplayEXT: not found in host\n");
    return nullptr;
}

extern "C" EGLDisplay bionic_eglGetDisplay(EGLNativeDisplayType display_id) {
    (void)display_id;
    fprintf(stdout, "[KuDroidGPU] bionic_eglGetDisplay called\n");
    auto host_func = (PFN_eglGetPlatformDisplayEXT) get_egl_func("eglGetPlatformDisplayEXT");
    if (host_func) {
        #define EGL_PLATFORM_ANGLE_ANGLE 0x3202
        #define EGL_PLATFORM_ANGLE_TYPE_ANGLE 0x3203
        #define EGL_PLATFORM_ANGLE_TYPE_VULKAN_ANGLE 0x3450
        #define EGL_PLATFORM_ANGLE_TYPE_METAL_ANGLE 0x3489
        #define EGL_PLATFORM_ANGLE_TYPE_DEFAULT_ANGLE 0x3204
        #define EGL_PLATFORM_ANGLE_DEVICE_TYPE_ANGLE 0x3205
        #define EGL_PLATFORM_ANGLE_DEVICE_TYPE_METAL_ANGLE 0x348A
        #define EGL_NONE 0x3038
        
        // On iOS, ANGLE uses the Metal backend. Try Metal first with the
        // device type explicitly set. IMPORTANT: always pass EGL_DEFAULT_DISPLAY
        // (0) here — CAMetalLayer is ONLY for eglCreateWindowSurface.
        EGLint backends[] = {
            EGL_PLATFORM_ANGLE_TYPE_METAL_ANGLE,
            EGL_PLATFORM_ANGLE_TYPE_VULKAN_ANGLE,
            EGL_PLATFORM_ANGLE_TYPE_DEFAULT_ANGLE
        };
        
        for (int i = 0; i < 3; i++) {
            const EGLint attribs[] = {
                EGL_PLATFORM_ANGLE_TYPE_ANGLE, backends[i],
                EGL_PLATFORM_ANGLE_DEVICE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_DEVICE_TYPE_METAL_ANGLE,
                EGL_NONE
            };
            EGLDisplay dpy = host_func(EGL_PLATFORM_ANGLE_ANGLE, (void*)0, attribs);
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
        // Always pass EGL_DEFAULT_DISPLAY (0) — CAMetalLayer is only for surfaces.
        EGLDisplay dpy = host_get_display((EGLNativeDisplayType)0);
        fprintf(stdout, "[KuDroidGPU] bionic_eglGetDisplay: fallback returned %s\n", dpy ? "VALID" : "NULL");
        return dpy;
    }
    
    fprintf(stdout, "[KuDroidGPU] bionic_eglGetDisplay: completely failed to find eglGetDisplay\n");
    return nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// Vulkan ↔ MoltenVK translation
//
// Android games request the Android-specific surface extension
// (VK_KHR_android_surface / vkCreateAndroidSurfaceKHR). MoltenVK on iOS only
// provides VK_EXT_metal_surface / vkCreateMetalSurfaceEXT. We intercept
// vkGetInstanceProcAddr and translate the surface creation call.
//
// Because bionic_ANativeWindow_fromSurface returns g_metalLayer (a CAMetalLayer)
// directly, the "window" pointer the game passes IS the CAMetalLayer — no
// struct unwrapping needed.
// ─────────────────────────────────────────────────────────────────────────────

// Minimal Vulkan types (opaque handles + result codes).
typedef uint32_t VkResult;
typedef uint32_t VkFlags;
typedef void* VkInstance;
typedef void* VkSurfaceKHR;
typedef void* VkAllocationCallbacks;
typedef void* PFN_vkVoidFunction;

#define VK_SUCCESS 0
#define VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT 1000217000
#define VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR 1000008000

typedef struct VkAndroidSurfaceCreateInfoKHR {
    uint32_t sType;
    const void* pNext;
    VkFlags flags;
    void* window; // ANativeWindow* — actually our CAMetalLayer
} VkAndroidSurfaceCreateInfoKHR;

typedef struct VkMetalSurfaceCreateInfoEXT {
    uint32_t sType;
    const void* pNext;
    VkFlags flags;
    void* pLayer; // CAMetalLayer*
} VkMetalSurfaceCreateInfoEXT;

typedef VkResult (*PFN_vkCreateMetalSurfaceEXT)(VkInstance, const VkMetalSurfaceCreateInfoEXT*, const VkAllocationCallbacks*, VkSurfaceKHR*);
typedef PFN_vkVoidFunction (*PFN_vkGetInstanceProcAddr)(VkInstance, const char*);

// Real MoltenVK vkGetInstanceProcAddr (resolved lazily).
static PFN_vkGetInstanceProcAddr real_vkGetInstanceProcAddr = nullptr;

static PFN_vkGetInstanceProcAddr get_real_vkGetInstanceProcAddr() {
    if (!real_vkGetInstanceProcAddr) {
        void* mvk = ::dlopen("@executable_path/Frameworks/MoltenVK.framework/MoltenVK", RTLD_NOW | RTLD_LOCAL);
        if (mvk) {
            real_vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)::dlsym(mvk, "vkGetInstanceProcAddr");
        }
    }
    return real_vkGetInstanceProcAddr;
}

// Translate vkCreateAndroidSurfaceKHR → vkCreateMetalSurfaceEXT.
extern "C" VkResult bionic_vkCreateAndroidSurfaceKHR(VkInstance instance,
                                                     const VkAndroidSurfaceCreateInfoKHR* pCreateInfo,
                                                     const VkAllocationCallbacks* pAllocator,
                                                     VkSurfaceKHR* pSurface) {
    fprintf(stdout, "[KuDroidGPU] vkCreateAndroidSurfaceKHR → vkCreateMetalSurfaceEXT\n");
    if (!pCreateInfo || !pSurface) return -1; // VK_ERROR_INITIALIZATION_FAILED

    auto real = get_real_vkGetInstanceProcAddr();
    if (!real) {
        fprintf(stderr, "[KuDroidGPU] ERROR: MoltenVK vkGetInstanceProcAddr not found\n");
        return -1;
    }
    auto createMetalSurface = (PFN_vkCreateMetalSurfaceEXT)real(instance, "vkCreateMetalSurfaceEXT");
    if (!createMetalSurface) {
        fprintf(stderr, "[KuDroidGPU] ERROR: vkCreateMetalSurfaceEXT not found in MoltenVK\n");
        return -1;
    }

    VkMetalSurfaceCreateInfoEXT metalInfo = {};
    metalInfo.sType = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT;
    metalInfo.pNext = nullptr;
    metalInfo.flags = 0;
    metalInfo.pLayer = pCreateInfo->window; // window IS the CAMetalLayer

    VkResult r = createMetalSurface(instance, &metalInfo, pAllocator, pSurface);
    fprintf(stdout, "[KuDroidGPU] vkCreateMetalSurfaceEXT returned %d (surface=%p)\n", (int)r, (void*)*pSurface);
    return r;
}

// Intercept vkGetInstanceProcAddr: translate Android surface calls, forward
// everything else to MoltenVK.
extern "C" PFN_vkVoidFunction bionic_vkGetInstanceProcAddr(VkInstance instance, const char* pName) {
    if (!pName) return nullptr;
    if (strcmp(pName, "vkCreateAndroidSurfaceKHR") == 0) {
        return (PFN_vkVoidFunction)&bionic_vkCreateAndroidSurfaceKHR;
    }
    auto real = get_real_vkGetInstanceProcAddr();
    if (real) {
        return real(instance, pName);
    }
    return nullptr;
}

// Intercept vkEnumerateInstanceExtensionProperties: inject
// VK_KHR_android_surface and mask VK_EXT_metal_surface so the game sees the
// Android surface extension it expects.
typedef VkResult (*PFN_vkEnumerateInstanceExtensionProperties)(const char*, uint32_t*, void*);

typedef struct VkExtensionProperties {
    char extensionName[256];
    uint32_t specVersion;
} VkExtensionProperties;

extern "C" VkResult bionic_vkEnumerateInstanceExtensionProperties(const char* pLayerName,
                                                                  uint32_t* pPropertyCount,
                                                                  VkExtensionProperties* pProperties) {
    // Resolve the real function directly from the MoltenVK handle.
    static PFN_vkEnumerateInstanceExtensionProperties real = nullptr;
    if (!real) {
        void* mvk = ::dlopen("@executable_path/Frameworks/MoltenVK.framework/MoltenVK", RTLD_NOW | RTLD_LOCAL);
        if (mvk) {
            real = (PFN_vkEnumerateInstanceExtensionProperties)::dlsym(mvk, "vkEnumerateInstanceExtensionProperties");
        }
    }
    if (!real) return -1;

    // First call: get the real count.
    uint32_t realCount = 0;
    VkResult r = real(pLayerName, &realCount, nullptr);
    if (r != VK_SUCCESS) return r;

    // We add VK_KHR_android_surface (and keep VK_EXT_metal_surface masked out).
    const uint32_t added = 1;
    uint32_t total = realCount + added;

    if (!pProperties) {
        *pPropertyCount = total;
        return VK_SUCCESS;
    }

    // Copy real properties, skipping VK_EXT_metal_surface.
    uint32_t out = 0;
    VkExtensionProperties* tmp = (VkExtensionProperties*)malloc(sizeof(VkExtensionProperties) * realCount);
    if (!tmp) return -1;
    real(pLayerName, &realCount, tmp);

    for (uint32_t i = 0; i < realCount && out < total; ++i) {
        if (strcmp(tmp[i].extensionName, "VK_EXT_metal_surface") == 0) {
            continue; // mask it
        }
        pProperties[out++] = tmp[i];
    }
    free(tmp);

    // Inject VK_KHR_android_surface.
    if (out < total) {
        strncpy(pProperties[out].extensionName, "VK_KHR_android_surface", sizeof(pProperties[out].extensionName) - 1);
        pProperties[out].extensionName[sizeof(pProperties[out].extensionName) - 1] = 0;
        pProperties[out].specVersion = 1;
        out++;
    }

    *pPropertyCount = out;
    return VK_SUCCESS;
}

// ─────────────────────────────────────────────────────────────────────────────
// EGL ↔ ANGLE translation
//
// ANGLE on iOS expects a CAMetalLayer/UIView as the native window. Because
// bionic_ANativeWindow_fromSurface returns g_metalLayer directly, the window
// pointer IS the CAMetalLayer — pass it straight through.
// ─────────────────────────────────────────────────────────────────────────────

typedef void* EGLSurface;
typedef void* EGLConfig;
typedef void* EGLContext;
typedef void* EGLNativeWindowType;
typedef EGLSurface (*PFN_eglCreateWindowSurface)(EGLDisplay, EGLConfig, EGLNativeWindowType, const EGLint*);

extern "C" EGLSurface bionic_eglCreateWindowSurface(EGLDisplay dpy, EGLConfig config,
                                                    EGLNativeWindowType win,
                                                    const EGLint* attrib_list) {
    fprintf(stdout, "[KuDroidGPU] eglCreateWindowSurface: window=%p\n", (void*)win);
    auto host_func = (PFN_eglCreateWindowSurface)get_egl_func("eglCreateWindowSurface");
    if (host_func) {
        // IMPORTANT: ANGLE on iOS expects a CAMetalLayer as the native window.
        // The Android game passes an ANativeWindow (which our
        // ANativeWindow_fromSurface already maps to g_metalLayer). Force the
        // window to g_metalLayer so ANGLE gets the CAMetalLayer it needs.
        EGLNativeWindowType nativeWin = (EGLNativeWindowType)g_metalLayer;
        EGLSurface s = host_func(dpy, config, nativeWin, attrib_list);
        fprintf(stdout, "[KuDroidGPU] eglCreateWindowSurface returned %p\n", (void*)s);
        return s;
    }
    fprintf(stdout, "[KuDroidGPU] eglCreateWindowSurface: not found in host\n");
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
    {"eglCreateWindowSurface", reinterpret_cast<void*>(&bionic_eglCreateWindowSurface)},
    {"vkGetInstanceProcAddr", reinterpret_cast<void*>(&bionic_vkGetInstanceProcAddr)},
    {"vkCreateAndroidSurfaceKHR", reinterpret_cast<void*>(&bionic_vkCreateAndroidSurfaceKHR)},
    {"vkEnumerateInstanceExtensionProperties", reinterpret_cast<void*>(&bionic_vkEnumerateInstanceExtensionProperties)},
};

} // namespace

const SymbolEntry* get_graphics_symbols(size_t* count) {
    if (count) {
        *count = sizeof(kGraphicsSymbols) / sizeof(SymbolEntry);
    }
    return kGraphicsSymbols;
}

} // namespace kudroid
