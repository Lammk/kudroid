#include "kudroid/shims/GraphicsShim.h"
#include <dlfcn.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cstdarg>
#include <string>
// This comes from kudroid_bridge.cpp
extern void* g_metalLayer;
extern int g_metalLayerWidth;
extern int g_metalLayerHeight;

namespace kudroid {

namespace {

// Log GPU qua pipeline chuẩn (stdout + file + crash buffer) để lần crash sau
// "log up to crash" không còn trống. Forward declaration để mọi hàm dùng được.
extern "C" int kudroid_android_log_message(int priority, const char* tag, const char* message);
static void gpuLog(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    kudroid_android_log_message(2, "KuDroidGPU", buf);
}

extern "C" void* bionic_ANativeWindow_fromSurface(void* env, void* surface) {
    (void)env; (void)surface;
    // ANGLE on iOS uses CAMetalLayer/UIView as the native window!
    if (!g_metalLayer) {
        gpuLog("ANativeWindow_fromSurface: g_metalLayer is NULL!");
    }
    gpuLog("ANativeWindow_fromSurface -> %p", (void*)g_metalLayer);
    return g_metalLayer;
}

extern "C" int bionic_ANativeWindow_getWidth(void* window) {
    (void)window; return g_metalLayerWidth;
}
extern "C" int bionic_ANativeWindow_getHeight(void* window) {
    (void)window; return g_metalLayerHeight;
}
extern "C" int bionic_ANativeWindow_setBuffersGeometry(void* window, int width, int height, int format) {
    (void)window;
    gpuLog("ANativeWindow_setBuffersGeometry(%dx%d format=%d) -> 0", width, height, format);
    return 0;
}
extern "C" void bionic_ANativeWindow_release(void* window) {
    (void)window; gpuLog("ANativeWindow_release(%p)", (void*)window);
}
extern "C" void bionic_ANativeWindow_acquire(void* window) {
    (void)window; gpuLog("ANativeWindow_acquire(%p)", (void*)window);
}

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
    (void)inOutDirtyRect;
    gpuLog("ANativeWindow_lock(window=%p, size=%dx%d)", (void*)window,
           g_metalLayerWidth, g_metalLayerHeight);
    if (!outBuffer) {
        gpuLog("ANativeWindow_lock -> -1 (outBuffer NULL)");
        return -1;
    }
    // Trước đây trả con trỏ tới 1 uint32_t static — game vẽ vào "buffer" đó sẽ
    // ghi tràn 4 byte → heap/static corruption → crash. Cấp buffer thật đủ
    // width*height*4 (RGBA8888), grow theo nhu cầu.
    static void* bits = nullptr;
    static size_t bitsSize = 0;
    const size_t needed = static_cast<size_t>(g_metalLayerWidth) *
                          static_cast<size_t>(g_metalLayerHeight) * 4;
    if (!bits || needed > bitsSize) {
        void* nb = std::realloc(bits, needed);
        if (!nb) return -1;
        bits = nb;
        bitsSize = needed;
        std::memset(bits, 0, needed);
    }
    outBuffer->width = g_metalLayerWidth;
    outBuffer->height = g_metalLayerHeight;
    outBuffer->stride = g_metalLayerWidth;
    outBuffer->format = 1; // WINDOW_FORMAT_RGBA_8888
    outBuffer->bits = bits;
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
    gpuLog("eglGetPlatformDisplayEXT(platform=0x%x)", (unsigned)platform);
    auto host_func = (PFN_eglGetPlatformDisplayEXT) get_egl_func("eglGetPlatformDisplayEXT");
    if (host_func) {
        // IMPORTANT: eglGetDisplay/eglGetPlatformDisplayEXT must ALWAYS use
        // EGL_DEFAULT_DISPLAY (0). CAMetalLayer is ONLY for eglCreateWindowSurface.
        // Force native_display to 0 regardless of what the Android game passed.
        EGLDisplay dpy = host_func(platform, (void*)0, attrib_list);
        gpuLog("eglGetPlatformDisplayEXT -> %s (%p)", dpy ? "VALID" : "NULL", (void*)dpy);
        return dpy;
    }
    gpuLog("eglGetPlatformDisplayEXT: not found in host");
    return nullptr;
}

extern "C" EGLDisplay bionic_eglGetDisplay(EGLNativeDisplayType display_id) {
    (void)display_id;
    gpuLog("eglGetDisplay(display_id=%p)", (void*)display_id);
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
        
        // Trên iOS, ANGLE chỉ có Metal backend khả dụng: build này patch
        // IsVulkanMacDisplayAvailable()=false ngay trong Display.cpp nên
        // TYPE_VULKAN và DEFAULT (ưu tiên Vulkan) luôn trả NULL — không probe.
        // Probe nhiều tổ hợp attrib, ưu tiên KHÔNG có device-type: nếu bản
        // ANGLE pin không biết enum EGL_PLATFORM_ANGLE_DEVICE_TYPE_METAL_ANGLE
        // (0x348A) thì toàn bộ attribs bị coi là EGL_BAD_ATTRIBUTE →
        // EGL_NO_DISPLAY (đúng log "backend[0..2] -> NULL" trước đây).
        // IMPORTANT: luôn truyền EGL_DEFAULT_DISPLAY (0) — CAMetalLayer chỉ
        // dùng cho eglCreateWindowSurface.

        // 1) Metal, không device-type (tổ hợp tối giản, ít rủi ro nhất)
        {
            const EGLint attribs[] = {
                EGL_PLATFORM_ANGLE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_TYPE_METAL_ANGLE,
                EGL_NONE
            };
            EGLDisplay dpy = host_func(EGL_PLATFORM_ANGLE_ANGLE, (void*)0, attribs);
            gpuLog("eglGetDisplay: Metal (no device-type) -> %s", dpy ? "VALID" : "NULL");
            if (dpy != nullptr) {
                gpuLog("eglGetDisplay: got display via eglGetPlatformDisplayEXT (Metal)");
                return dpy;
            }
        }
        // 2) Metal + device-type METAL (cho bản ANGLE mới có enum 0x348A)
        {
            const EGLint attribs[] = {
                EGL_PLATFORM_ANGLE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_TYPE_METAL_ANGLE,
                EGL_PLATFORM_ANGLE_DEVICE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_DEVICE_TYPE_METAL_ANGLE,
                EGL_NONE
            };
            EGLDisplay dpy = host_func(EGL_PLATFORM_ANGLE_ANGLE, (void*)0, attribs);
            gpuLog("eglGetDisplay: Metal (device-type) -> %s", dpy ? "VALID" : "NULL");
            if (dpy != nullptr) {
                gpuLog("eglGetDisplay: got display via eglGetPlatformDisplayEXT (Metal+device)");
                return dpy;
            }
        }
        // 3) DEFAULT, không device-type
        {
            const EGLint attribs[] = {
                EGL_PLATFORM_ANGLE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_TYPE_DEFAULT_ANGLE,
                EGL_NONE
            };
            EGLDisplay dpy = host_func(EGL_PLATFORM_ANGLE_ANGLE, (void*)0, attribs);
            gpuLog("eglGetDisplay: Default (no device-type) -> %s", dpy ? "VALID" : "NULL");
            if (dpy != nullptr) {
                gpuLog("eglGetDisplay: got display via eglGetPlatformDisplayEXT (Default)");
                return dpy;
            }
        }
        

    }
    
    // KHÔNG fallback sang eglGetDisplay(EGL_DEFAULT_DISPLAY): display đó trả
    // về "VALID" nhưng không có backend thật — eglInitialize() kế tiếp abort
    // bên trong ANGLE (SIGABRT, đúng crash log). Trả EGL_NO_DISPLAY để guest
    // thất bại nhẹ nhàng thay vì crash.
    
    gpuLog("eglGetDisplay: all platform display attempts failed — returning EGL_NO_DISPLAY");
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

    // Trước đây ghi tới `total` entry KHÔNG quan tâm *pPropertyCount — game gọi
    // với count nhỏ hơn (vd count = số extension thật, chưa biết extension mới
    // được inject) → ghi tràn buffer của game → crash. Clamp theo capacity.
    const uint32_t capacity = *pPropertyCount;

    // Copy real properties, skipping VK_EXT_metal_surface.
    uint32_t out = 0;
    if (realCount > 0) {
        VkExtensionProperties* tmp = (VkExtensionProperties*)malloc(sizeof(VkExtensionProperties) * realCount);
        if (!tmp) return -1;
        real(pLayerName, &realCount, tmp);

        for (uint32_t i = 0; i < realCount && out < capacity; ++i) {
            if (strcmp(tmp[i].extensionName, "VK_EXT_metal_surface") == 0) {
                continue; // mask it
            }
            pProperties[out++] = tmp[i];
        }
        free(tmp);
    }

    // Inject VK_KHR_android_surface (chỉ nếu còn chỗ).
    if (out < capacity) {
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
//
// QUAN TRỌNG: phải export ĐỦ toàn bộ entry point EGL, không chỉ 4-5 hàm. ELF
// loader resolve import của game qua BionicShim (bảng shim + RTLD_DEFAULT) —
// ANGLE load RTLD_LOCAL nên không nằm trong RTLD_DEFAULT. Trước đây 17 hàm
// egl* còn lại (eglInitialize, eglChooseConfig, eglCreateContext, eglMakeCurrent,
// eglSwapBuffers...) rơi vào dummy trả 0 → Unity nhận eglCreateContext=NULL →
// crash NULL+0x50. Giờ mỗi hàm forward thẳng sang ANGLE qua get_egl_func().
// ─────────────────────────────────────────────────────────────────────────────

typedef void* EGLSurface;
typedef void* EGLConfig;
typedef void* EGLContext;
typedef void* EGLNativeWindowType;
typedef unsigned int EGLBoolean;
typedef unsigned int EGLenum;
#define EGL_TRUE 1
#define EGL_FALSE 0
#define EGL_SUCCESS 0x3000
#define EGL_NO_DISPLAY ((EGLDisplay)0)
#define EGL_NO_CONTEXT ((EGLContext)0)
#define EGL_NO_SURFACE ((EGLSurface)0)

typedef EGLSurface (*PFN_eglCreateWindowSurface)(EGLDisplay, EGLConfig, EGLNativeWindowType, const EGLint*);


extern "C" EGLSurface bionic_eglCreateWindowSurface(EGLDisplay dpy, EGLConfig config,
                                                    EGLNativeWindowType win,
                                                    const EGLint* attrib_list) {
    gpuLog("eglCreateWindowSurface: window=%p", (void*)win);
    auto host_func = (PFN_eglCreateWindowSurface)get_egl_func("eglCreateWindowSurface");
    if (host_func) {
        // IMPORTANT: ANGLE on iOS expects a CAMetalLayer as the native window.
        // The Android game passes an ANativeWindow (which our
        // ANativeWindow_fromSurface already maps to g_metalLayer). Force the
        // window to g_metalLayer so ANGLE gets the CAMetalLayer it needs.
        EGLNativeWindowType nativeWin = (EGLNativeWindowType)g_metalLayer;
        gpuLog("eglCreateWindowSurface: calling ANGLE with window=%p...", (void*)nativeWin);
        EGLSurface s = host_func(dpy, config, nativeWin, attrib_list);
        gpuLog("eglCreateWindowSurface returned %p", (void*)s);
        return s;
    }
    gpuLog("eglCreateWindowSurface: not found in host");
    return nullptr;
}

// ── EGL 1.x entry points còn thiếu — forward thẳng sang ANGLE ────────────────

#define EGL_FORWARD_ERR(name, what) gpuLog("%s: ANGLE %s not available", name, what)

extern "C" EGLBoolean bionic_eglInitialize(EGLDisplay dpy, EGLint* major, EGLint* minor) {
    typedef EGLBoolean (*PFN)(EGLDisplay, EGLint*, EGLint*);
    auto f = (PFN)get_egl_func("eglInitialize");
    if (!f) { EGL_FORWARD_ERR("eglInitialize", ""); return EGL_FALSE; }
    // Guest (vd TriangleGLES gọi eglInitialize(display, 0, 0)) có thể truyền
    // NULL out-params — spec EGL cho phép, nhưng vài bản ANGLE dereference
    // chúng → abort. Forward với buffer địa phương rồi copy kết quả về.
    EGLint localMajor = 0, localMinor = 0;
    gpuLog("eglInitialize: calling ANGLE eglInitialize(dpy=%p, major=%s, minor=%s)...",
           (void*)dpy, major ? "ptr" : "NULL", minor ? "ptr" : "NULL");
    const EGLBoolean r = f(dpy, major ? major : &localMajor, minor ? minor : &localMinor);
    gpuLog("eglInitialize -> %s (major=%d minor=%d)", r ? "true" : "false",
           major ? *major : localMajor, minor ? *minor : localMinor);
    return r;
}

extern "C" EGLBoolean bionic_eglTerminate(EGLDisplay dpy) {
    typedef EGLBoolean (*PFN)(EGLDisplay);
    auto f = (PFN)get_egl_func("eglTerminate");
    if (!f) { EGL_FORWARD_ERR("eglTerminate", ""); return EGL_FALSE; }
    EGLBoolean r = f(dpy);
    gpuLog("eglTerminate -> %s", r ? "true" : "false");
    return r;
}

extern "C" EGLBoolean bionic_eglChooseConfig(EGLDisplay dpy, const EGLint* attrib_list,
                                             EGLConfig* configs, EGLint config_size,
                                             EGLint* num_config) {
    typedef EGLBoolean (*PFN)(EGLDisplay, const EGLint*, EGLConfig*, EGLint, EGLint*);
    auto f = (PFN)get_egl_func("eglChooseConfig");
    if (!f) { EGL_FORWARD_ERR("eglChooseConfig", ""); return EGL_FALSE; }
    gpuLog("eglChooseConfig: calling ANGLE...");
    EGLBoolean r = f(dpy, attrib_list, configs, config_size, num_config);
    gpuLog("eglChooseConfig -> %s (num=%d)", r ? "true" : "false",
           num_config ? *num_config : -1);
    return r;
}

extern "C" EGLBoolean bionic_eglGetConfigAttrib(EGLDisplay dpy, EGLConfig config,
                                                EGLint attribute, EGLint* value) {
    typedef EGLBoolean (*PFN)(EGLDisplay, EGLConfig, EGLint, EGLint*);
    auto f = (PFN)get_egl_func("eglGetConfigAttrib");
    if (!f) { EGL_FORWARD_ERR("eglGetConfigAttrib", ""); return EGL_FALSE; }
    EGLBoolean r = f(dpy, config, attribute, value);
    gpuLog("eglGetConfigAttrib(attr=0x%x) -> %s%s%s", (unsigned)attribute,
           r ? "true" : "false", r ? " value=" : "",
           (r && value) ? std::to_string(*value).c_str() : "");
    return r;
}

extern "C" EGLBoolean bionic_eglGetConfigs(EGLDisplay dpy, EGLConfig* configs,
                                           EGLint config_size, EGLint* num_config) {
    typedef EGLBoolean (*PFN)(EGLDisplay, EGLConfig*, EGLint, EGLint*);
    auto f = (PFN)get_egl_func("eglGetConfigs");
    if (!f) { EGL_FORWARD_ERR("eglGetConfigs", ""); return EGL_FALSE; }
    EGLBoolean r = f(dpy, configs, config_size, num_config);
    gpuLog("eglGetConfigs(size=%d) -> %s (num=%d)", config_size, r ? "true" : "false",
           num_config ? *num_config : -1);
    return r;
}

extern "C" EGLContext bionic_eglCreateContext(EGLDisplay dpy, EGLConfig config,
                                              EGLContext share_context,
                                              const EGLint* attrib_list) {
    typedef EGLContext (*PFN)(EGLDisplay, EGLConfig, EGLContext, const EGLint*);
    auto f = (PFN)get_egl_func("eglCreateContext");
    if (!f) { EGL_FORWARD_ERR("eglCreateContext", ""); return EGL_NO_CONTEXT; }
    gpuLog("eglCreateContext: calling ANGLE...");
    EGLContext ctx = f(dpy, config, share_context, attrib_list);
    gpuLog("eglCreateContext -> %p", (void*)ctx);
    return ctx;
}

extern "C" EGLBoolean bionic_eglDestroyContext(EGLDisplay dpy, EGLContext ctx) {
    typedef EGLBoolean (*PFN)(EGLDisplay, EGLContext);
    auto f = (PFN)get_egl_func("eglDestroyContext");
    if (!f) { EGL_FORWARD_ERR("eglDestroyContext", ""); return EGL_FALSE; }
    EGLBoolean r = f(dpy, ctx);
    gpuLog("eglDestroyContext(%p) -> %s", (void*)ctx, r ? "true" : "false");
    return r;
}

extern "C" EGLSurface bionic_eglCreatePbufferSurface(EGLDisplay dpy, EGLConfig config,
                                                     const EGLint* attrib_list) {
    typedef EGLSurface (*PFN)(EGLDisplay, EGLConfig, const EGLint*);
    auto f = (PFN)get_egl_func("eglCreatePbufferSurface");
    if (!f) { EGL_FORWARD_ERR("eglCreatePbufferSurface", ""); return EGL_NO_SURFACE; }
    EGLSurface s = f(dpy, config, attrib_list);
    gpuLog("eglCreatePbufferSurface -> %p", (void*)s);
    return s;
}

extern "C" EGLBoolean bionic_eglDestroySurface(EGLDisplay dpy, EGLSurface surface) {
    typedef EGLBoolean (*PFN)(EGLDisplay, EGLSurface);
    auto f = (PFN)get_egl_func("eglDestroySurface");
    if (!f) { EGL_FORWARD_ERR("eglDestroySurface", ""); return EGL_FALSE; }
    EGLBoolean r = f(dpy, surface);
    gpuLog("eglDestroySurface(%p) -> %s", (void*)surface, r ? "true" : "false");
    return r;
}

extern "C" EGLBoolean bionic_eglMakeCurrent(EGLDisplay dpy, EGLSurface draw,
                                            EGLSurface read, EGLContext ctx) {
    typedef EGLBoolean (*PFN)(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
    auto f = (PFN)get_egl_func("eglMakeCurrent");
    if (!f) { EGL_FORWARD_ERR("eglMakeCurrent", ""); return EGL_FALSE; }
    gpuLog("eglMakeCurrent: calling ANGLE...");
    EGLBoolean r = f(dpy, draw, read, ctx);
    gpuLog("eglMakeCurrent(ctx=%p, draw=%p) -> %s", (void*)ctx, (void*)draw,
           r ? "true" : "false");
    return r;
}

extern "C" EGLContext bionic_eglGetCurrentContext(void) {
    typedef EGLContext (*PFN)(void);
    auto f = (PFN)get_egl_func("eglGetCurrentContext");
    if (!f) { EGL_FORWARD_ERR("eglGetCurrentContext", ""); return EGL_NO_CONTEXT; }
    EGLContext c = f();
    gpuLog("eglGetCurrentContext -> %p", (void*)c);
    return c;
}

extern "C" EGLSurface bionic_eglGetCurrentSurface(EGLint readdraw) {
    typedef EGLSurface (*PFN)(EGLint);
    auto f = (PFN)get_egl_func("eglGetCurrentSurface");
    if (!f) { EGL_FORWARD_ERR("eglGetCurrentSurface", ""); return EGL_NO_SURFACE; }
    EGLSurface s = f(readdraw);
    gpuLog("eglGetCurrentSurface(0x%x) -> %p", (unsigned)readdraw, (void*)s);
    return s;
}

extern "C" EGLDisplay bionic_eglGetCurrentDisplay(void) {
    typedef EGLDisplay (*PFN)(void);
    auto f = (PFN)get_egl_func("eglGetCurrentDisplay");
    if (!f) { EGL_FORWARD_ERR("eglGetCurrentDisplay", ""); return EGL_NO_DISPLAY; }
    EGLDisplay d = f();
    gpuLog("eglGetCurrentDisplay -> %p", (void*)d);
    return d;
}

extern "C" EGLBoolean bionic_eglSwapBuffers(EGLDisplay dpy, EGLSurface surface) {
    typedef EGLBoolean (*PFN)(EGLDisplay, EGLSurface);
    auto f = (PFN)get_egl_func("eglSwapBuffers");
    if (!f) { EGL_FORWARD_ERR("eglSwapBuffers", ""); return EGL_FALSE; }
    gpuLog("eglSwapBuffers: calling ANGLE...");
    EGLBoolean r = f(dpy, surface);
    gpuLog("eglSwapBuffers(surface=%p) -> %s", (void*)surface, r ? "true" : "false");
    return r;
}

extern "C" EGLBoolean bionic_eglSwapInterval(EGLDisplay dpy, EGLint interval) {
    typedef EGLBoolean (*PFN)(EGLDisplay, EGLint);
    auto f = (PFN)get_egl_func("eglSwapInterval");
    if (!f) { EGL_FORWARD_ERR("eglSwapInterval", ""); return EGL_FALSE; }
    EGLBoolean r = f(dpy, interval);
    gpuLog("eglSwapInterval(%d) -> %s", interval, r ? "true" : "false");
    return r;
}

extern "C" const char* bionic_eglQueryString(EGLDisplay dpy, EGLint name) {
    typedef const char* (*PFN)(EGLDisplay, EGLint);
    auto f = (PFN)get_egl_func("eglQueryString");
    if (!f) { EGL_FORWARD_ERR("eglQueryString", ""); return nullptr; }
    const char* s = f(dpy, name);
    gpuLog("eglQueryString(name=%d) -> %s", name, s ? s : "(null)");
    return s;
}

extern "C" EGLBoolean bionic_eglQuerySurface(EGLDisplay dpy, EGLSurface surface,
                                             EGLint attribute, EGLint* value) {
    typedef EGLBoolean (*PFN)(EGLDisplay, EGLSurface, EGLint, EGLint*);
    auto f = (PFN)get_egl_func("eglQuerySurface");
    if (!f) { EGL_FORWARD_ERR("eglQuerySurface", ""); return EGL_FALSE; }
    EGLBoolean r = f(dpy, surface, attribute, value);
    gpuLog("eglQuerySurface(attr=0x%x) -> %s%s%s", (unsigned)attribute,
           r ? "true" : "false", r ? " value=" : "",
           (r && value) ? std::to_string(*value).c_str() : "");
    return r;
}

extern "C" EGLint bionic_eglGetError(void) {
    typedef EGLint (*PFN)(void);
    auto f = (PFN)get_egl_func("eglGetError");
    if (!f) { EGL_FORWARD_ERR("eglGetError", ""); return EGL_SUCCESS; }
    EGLint e = f();
    gpuLog("eglGetError -> %s (0x%x)", e == EGL_SUCCESS ? "EGL_SUCCESS" : "ERROR", (unsigned)e);
    return e;
}

extern "C" EGLBoolean bionic_eglSurfaceAttrib(EGLDisplay dpy, EGLSurface surface,
                                              EGLint attribute, EGLint value) {
    typedef EGLBoolean (*PFN)(EGLDisplay, EGLSurface, EGLint, EGLint);
    auto f = (PFN)get_egl_func("eglSurfaceAttrib");
    if (!f) { EGL_FORWARD_ERR("eglSurfaceAttrib", ""); return EGL_FALSE; }
    EGLBoolean r = f(dpy, surface, attribute, value);
    gpuLog("eglSurfaceAttrib(attr=0x%x val=%d) -> %s", (unsigned)attribute, value,
           r ? "true" : "false");
    return r;
}

extern "C" EGLBoolean bionic_eglReleaseThread(void) {
    typedef EGLBoolean (*PFN)(void);
    auto f = (PFN)get_egl_func("eglReleaseThread");
    if (!f) { EGL_FORWARD_ERR("eglReleaseThread", ""); return EGL_TRUE; }
    EGLBoolean r = f();
    gpuLog("eglReleaseThread -> %s", r ? "true" : "false");
    return r;
}

extern "C" EGLBoolean bionic_eglBindAPI(EGLenum api) {
    typedef EGLBoolean (*PFN)(EGLenum);
    auto f = (PFN)get_egl_func("eglBindAPI");
    if (!f) { EGL_FORWARD_ERR("eglBindAPI", ""); return EGL_FALSE; }
    EGLBoolean r = f(api);
    gpuLog("eglBindAPI(0x%x) -> %s", (unsigned)api, r ? "true" : "false");
    return r;
}

extern "C" EGLenum bionic_eglQueryAPI(void) {
    typedef EGLenum (*PFN)(void);
    auto f = (PFN)get_egl_func("eglQueryAPI");
    if (!f) { EGL_FORWARD_ERR("eglQueryAPI", ""); return 0; }
    EGLenum e = f();
    gpuLog("eglQueryAPI -> 0x%x", (unsigned)e);
    return e;
}

extern "C" EGLDisplay bionic_eglGetPlatformDisplay(EGLenum platform, void* native_display,
                                                   const EGLint* attrib_list) {
    (void)native_display;
    typedef EGLDisplay (*PFN)(EGLenum, void*, const EGLint*);
    auto f = (PFN)get_egl_func("eglGetPlatformDisplay");
    if (!f) { EGL_FORWARD_ERR("eglGetPlatformDisplay", ""); return EGL_NO_DISPLAY; }
    // Same as EXT variant: native display phải là 0 (EGL_DEFAULT_DISPLAY),
    // CAMetalLayer chỉ dùng cho eglCreateWindowSurface.
    EGLDisplay d = f(platform, (void*)0, attrib_list);
    gpuLog("eglGetPlatformDisplay -> %p", (void*)d);
    return d;
}

extern "C" EGLBoolean bionic_eglBindTexImage(EGLDisplay dpy, EGLSurface surface, EGLint buffer) {
    typedef EGLBoolean (*PFN)(EGLDisplay, EGLSurface, EGLint);
    auto f = (PFN)get_egl_func("eglBindTexImage");
    if (!f) { EGL_FORWARD_ERR("eglBindTexImage", ""); return EGL_FALSE; }
    EGLBoolean r = f(dpy, surface, buffer);
    gpuLog("eglBindTexImage(surface=%p buffer=%d) -> %s", (void*)surface, buffer,
           r ? "true" : "false");
    return r;
}

extern "C" EGLBoolean bionic_eglReleaseTexImage(EGLDisplay dpy, EGLSurface surface, EGLint buffer) {
    typedef EGLBoolean (*PFN)(EGLDisplay, EGLSurface, EGLint);
    auto f = (PFN)get_egl_func("eglReleaseTexImage");
    if (!f) { EGL_FORWARD_ERR("eglReleaseTexImage", ""); return EGL_FALSE; }
    EGLBoolean r = f(dpy, surface, buffer);
    gpuLog("eglReleaseTexImage(surface=%p buffer=%d) -> %s", (void*)surface, buffer,
           r ? "true" : "false");
    return r;
}

extern "C" EGLBoolean bionic_eglWaitGL(void) {
    typedef EGLBoolean (*PFN)(void);
    auto f = (PFN)get_egl_func("eglWaitGL");
    if (!f) { EGL_FORWARD_ERR("eglWaitGL", ""); return EGL_TRUE; }
    EGLBoolean r = f();
    gpuLog("eglWaitGL -> %s", r ? "true" : "false");
    return r;
}

extern "C" EGLBoolean bionic_eglWaitNative(EGLint engine) {
    typedef EGLBoolean (*PFN)(EGLint);
    auto f = (PFN)get_egl_func("eglWaitNative");
    if (!f) { EGL_FORWARD_ERR("eglWaitNative", ""); return EGL_TRUE; }
    EGLBoolean r = f(engine);
    gpuLog("eglWaitNative(%d) -> %s", engine, r ? "true" : "false");
    return r;
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
    {"eglGetPlatformDisplay", reinterpret_cast<void*>(&bionic_eglGetPlatformDisplay)},
    {"eglGetProcAddress", reinterpret_cast<void*>(&bionic_eglGetProcAddress)},
    {"eglCreateWindowSurface", reinterpret_cast<void*>(&bionic_eglCreateWindowSurface)},
    {"eglInitialize", reinterpret_cast<void*>(&bionic_eglInitialize)},
    {"eglTerminate", reinterpret_cast<void*>(&bionic_eglTerminate)},
    {"eglChooseConfig", reinterpret_cast<void*>(&bionic_eglChooseConfig)},
    {"eglGetConfigAttrib", reinterpret_cast<void*>(&bionic_eglGetConfigAttrib)},
    {"eglGetConfigs", reinterpret_cast<void*>(&bionic_eglGetConfigs)},
    {"eglCreateContext", reinterpret_cast<void*>(&bionic_eglCreateContext)},
    {"eglDestroyContext", reinterpret_cast<void*>(&bionic_eglDestroyContext)},
    {"eglCreatePbufferSurface", reinterpret_cast<void*>(&bionic_eglCreatePbufferSurface)},
    {"eglDestroySurface", reinterpret_cast<void*>(&bionic_eglDestroySurface)},
    {"eglMakeCurrent", reinterpret_cast<void*>(&bionic_eglMakeCurrent)},
    {"eglGetCurrentContext", reinterpret_cast<void*>(&bionic_eglGetCurrentContext)},
    {"eglGetCurrentSurface", reinterpret_cast<void*>(&bionic_eglGetCurrentSurface)},
    {"eglGetCurrentDisplay", reinterpret_cast<void*>(&bionic_eglGetCurrentDisplay)},
    {"eglSwapBuffers", reinterpret_cast<void*>(&bionic_eglSwapBuffers)},
    {"eglSwapInterval", reinterpret_cast<void*>(&bionic_eglSwapInterval)},
    {"eglQueryString", reinterpret_cast<void*>(&bionic_eglQueryString)},
    {"eglQuerySurface", reinterpret_cast<void*>(&bionic_eglQuerySurface)},
    {"eglGetError", reinterpret_cast<void*>(&bionic_eglGetError)},
    {"eglSurfaceAttrib", reinterpret_cast<void*>(&bionic_eglSurfaceAttrib)},
    {"eglReleaseThread", reinterpret_cast<void*>(&bionic_eglReleaseThread)},
    {"eglBindAPI", reinterpret_cast<void*>(&bionic_eglBindAPI)},
    {"eglQueryAPI", reinterpret_cast<void*>(&bionic_eglQueryAPI)},
    {"eglBindTexImage", reinterpret_cast<void*>(&bionic_eglBindTexImage)},
    {"eglReleaseTexImage", reinterpret_cast<void*>(&bionic_eglReleaseTexImage)},
    {"eglWaitGL", reinterpret_cast<void*>(&bionic_eglWaitGL)},
    {"eglWaitNative", reinterpret_cast<void*>(&bionic_eglWaitNative)},
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
