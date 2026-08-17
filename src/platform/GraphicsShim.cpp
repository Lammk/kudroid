#include "kudroid/platform/GraphicsShim.h"
#include "kudroid/Log.h"
#include <dlfcn.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cstdarg>
#include <string>
#include <vector>
#include <atomic>

#if defined(__APPLE__)
#include <pthread.h>
#include <dispatch/dispatch.h>
extern "C" void* objc_autoreleasePoolPush(void);
extern "C" void objc_autoreleasePoolPop(void* pool);
// Thread-local autorelease pool for ANGLE Metal.
// ANGLE internally creates @autoreleased ObjC objects (MTLCommandBuffer,
// MTLRenderPipelineState, MTLLibrary, etc.) during GL calls such as
// glCreateShader, glCompileShader, glDrawArrays, etc. Without a thread-local
// autorelease pool, these objects have no pool to drain into, causing SIGABRT.
// We push a pool when eglMakeCurrent binds a context to the thread, and
// drain+re-push on every eglSwapBuffers to keep memory bounded.
static thread_local void* tls_autorelease_pool = nullptr;
#endif

// This comes from kudroid_bridge.cpp
extern void* g_metalLayer;
extern int g_metalLayerWidth;
extern int g_metalLayerHeight;

namespace kudroid {

namespace {

// Log GPU qua pipeline chuẩn (KLOG → stdout + file + crash buffer) để lần
// crash sau "log up to crash" không còn trống. Giữ cùng text/priority (debug).
static void gpuLog(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    ::kudroid::log::write(::kudroid::log::kDebug, "KuDroidGPU", "%s", buf);
}

// Cấu trúc ANativeWindow chuẩn cho KuDroid
struct KuDroidNativeWindow {
    uint32_t magic;
    void* layer;
    int32_t width;
    int32_t height;
    int32_t format;
    std::atomic<int32_t> refCount;

    KuDroidNativeWindow() : magic(0x4B554457), layer(nullptr), width(1080), height(1920), format(1), refCount(1) {}
};

static KuDroidNativeWindow g_nativeWindowInstance;

extern "C" void* bionic_ANativeWindow_fromSurface(void* env, void* surface) {
    (void)env; (void)surface;
    g_nativeWindowInstance.layer = g_metalLayer;
    g_nativeWindowInstance.width = g_metalLayerWidth > 0 ? g_metalLayerWidth : 1080;
    g_nativeWindowInstance.height = g_metalLayerHeight > 0 ? g_metalLayerHeight : 1920;
    gpuLog("ANativeWindow_fromSurface -> %p (layer=%p, size=%dx%d)",
           (void*)&g_nativeWindowInstance, g_nativeWindowInstance.layer,
           g_nativeWindowInstance.width, g_nativeWindowInstance.height);
    return &g_nativeWindowInstance;
}

extern "C" int bionic_ANativeWindow_getWidth(void* window) {
    if (window) {
        auto* nw = static_cast<KuDroidNativeWindow*>(window);
        if (nw->magic == 0x4B554457) return nw->width > 0 ? nw->width : 1080;
    }
    return g_metalLayerWidth > 0 ? g_metalLayerWidth : 1080;
}

extern "C" int bionic_ANativeWindow_getHeight(void* window) {
    if (window) {
        auto* nw = static_cast<KuDroidNativeWindow*>(window);
        if (nw->magic == 0x4B554457) return nw->height > 0 ? nw->height : 1920;
    }
    return g_metalLayerHeight > 0 ? g_metalLayerHeight : 1920;
}

extern "C" int bionic_ANativeWindow_getFormat(void* window) {
    if (window) {
        auto* nw = static_cast<KuDroidNativeWindow*>(window);
        if (nw->magic == 0x4B554457) return nw->format;
    }
    return 1; // WINDOW_FORMAT_RGBA_8888
}

extern "C" int bionic_ANativeWindow_setBuffersGeometry(void* window, int width, int height, int format) {
    if (window) {
        auto* nw = static_cast<KuDroidNativeWindow*>(window);
        if (nw->magic == 0x4B554457) {
            if (width > 0) nw->width = width;
            if (height > 0) nw->height = height;
            if (format > 0) nw->format = format;
        }
    }
    gpuLog("ANativeWindow_setBuffersGeometry(%dx%d format=%d) -> 0", width, height, format);
    return 0;
}

extern "C" void bionic_ANativeWindow_release(void* window) {
    if (!window) return;
    auto* nw = static_cast<KuDroidNativeWindow*>(window);
    if (nw->magic == 0x4B554457) {
        nw->refCount.fetch_sub(1);
    }
    gpuLog("ANativeWindow_release(%p)", window);
}

extern "C" void bionic_ANativeWindow_acquire(void* window) {
    if (!window) return;
    auto* nw = static_cast<KuDroidNativeWindow*>(window);
    if (nw->magic == 0x4B554457) {
        nw->refCount.fetch_add(1);
    }
    gpuLog("ANativeWindow_acquire(%p)", window);
}

// ANativeWindow_Buffer — describes a locked buffer.
struct ANativeWindow_Buffer {
    int32_t width;
    int32_t height;
    int32_t stride;
    int32_t format;
    void* bits;
};

static void* s_canvasBits = nullptr;
static size_t s_canvasBitsSize = 0;
static int s_canvasWidth = 1080;
static int s_canvasHeight = 1920;

extern "C" int bionic_ANativeWindow_lock(void* window, ANativeWindow_Buffer* outBuffer,
                                         void* inOutDirtyRect) {
    (void)inOutDirtyRect;
    if (!outBuffer) {
        gpuLog("ANativeWindow_lock -> -1 (outBuffer NULL)");
        return -1;
    }
    int w = bionic_ANativeWindow_getWidth(window);
    int h = bionic_ANativeWindow_getHeight(window);
    constexpr int kMaxBufferDim = 8192;
    if (w <= 0 || w > kMaxBufferDim || h <= 0 || h > kMaxBufferDim) {
        w = 1080;
        h = 1920;
    }
    s_canvasWidth = w;
    s_canvasHeight = h;
    const size_t needed = static_cast<size_t>(w) * static_cast<size_t>(h) * 4;
    if (!s_canvasBits || needed > s_canvasBitsSize) {
        void* nb = std::realloc(s_canvasBits, needed);
        if (!nb) return -1;
        s_canvasBits = nb;
        s_canvasBitsSize = needed;
        std::memset(s_canvasBits, 0, needed);
    }
    outBuffer->width = w;
    outBuffer->height = h;
    outBuffer->stride = w;
    outBuffer->format = bionic_ANativeWindow_getFormat(window);
    outBuffer->bits = s_canvasBits;
    return 0;
}

#if defined(__APPLE__)
extern "C" __attribute__((weak)) void kudroid_blit_canvas_to_layer(void* layer, const void* bits, int width, int height) {
    (void)layer; (void)bits; (void)width; (void)height;
}
#endif

extern "C" int bionic_ANativeWindow_unlockAndPost(void* window) {
    (void)window;
    if (!s_canvasBits || s_canvasWidth <= 0 || s_canvasHeight <= 0) return 0;
#if defined(__APPLE__)
    if (g_metalLayer && kudroid_blit_canvas_to_layer) {
        kudroid_blit_canvas_to_layer(g_metalLayer, s_canvasBits, s_canvasWidth, s_canvasHeight);
    }
#endif
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
    KLOG(kDebug, "KuDroidGPU", "eglGetProcAddress: requested %s", procname);
    
    size_t count = 0;
    const kudroid::SymbolEntry* symbols = kudroid::get_graphics_symbols(&count);
    for (size_t i = 0; i < count; ++i) {
        if (strcmp(symbols[i].name, procname) == 0) {
            KLOG(kDebug, "KuDroidGPU", "eglGetProcAddress: %s intercepted by GraphicsShim", procname);
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
            KLOG(kDebug, "KuDroidGPU", "eglGetProcAddress: %s returned %s", procname, addr ? "VALID" : "NULL");
            return addr;
        }
    }
    // Fallback to RTLD_DEFAULT.
    auto host_func = (PFN_eglGetProcAddress) ::dlsym(RTLD_DEFAULT, "eglGetProcAddress");
    if (host_func) {
        void* addr = host_func(procname);
        KLOG(kDebug, "KuDroidGPU", "eglGetProcAddress: %s returned %s", procname, addr ? "VALID" : "NULL");
        return addr;
    }
    KLOG(kDebug, "KuDroidGPU", "eglGetProcAddress: host function not found");
    return nullptr;
}

} // namespace

void* get_gl_func(const char* name) {
    if (!name) return nullptr;
    static void* gl_handle = nullptr;
    if (!gl_handle) {
        gl_handle = ::dlopen("@executable_path/Frameworks/libGLESv2.framework/libGLESv2", RTLD_NOW | RTLD_GLOBAL);
        if (!gl_handle) {
            gl_handle = ::dlopen("Frameworks/libGLESv2.framework/libGLESv2", RTLD_NOW | RTLD_GLOBAL);
        }
    }
    if (gl_handle) {
        void* func = ::dlsym(gl_handle, name);
        if (func) return func;
    }
    void* func = ::dlsym(RTLD_DEFAULT, name);
    if (func) return func;
    auto host_get_proc = (PFN_eglGetProcAddress) ::dlsym(RTLD_DEFAULT, "eglGetProcAddress");
    if (host_get_proc) {
        func = host_get_proc(name);
    }
    return func;
}

void* get_egl_func(const char* name) {
    if (!name) return nullptr;
    static void* egl_handle = nullptr;
    if (!egl_handle) {
        egl_handle = ::dlopen("@executable_path/Frameworks/libEGL.framework/libEGL", RTLD_NOW | RTLD_GLOBAL);
        if (!egl_handle) {
            KLOG(kError, "KuDroidGPU", "FATAL: dlopen libEGL failed: %s", dlerror());
            egl_handle = ::dlopen("Frameworks/libEGL.framework/libEGL", RTLD_NOW | RTLD_GLOBAL);
            if (!egl_handle) {
                KLOG(kError, "KuDroidGPU", "FATAL: alternative dlopen also failed: %s", dlerror());
            } else {
                KLOG(kInfo, "KuDroidGPU", "SUCCESS: alternative dlopen loaded libEGL");
            }
        } else {
            KLOG(kInfo, "KuDroidGPU", "SUCCESS: dlopen loaded libEGL");
        }
    }
    if (egl_handle) {
        void* func = ::dlsym(egl_handle, name);
        if (func) return func;
    }
    void* func = ::dlsym(RTLD_DEFAULT, name);
    if (func) return func;
    auto host_get_proc = (PFN_eglGetProcAddress) ::dlsym(RTLD_DEFAULT, "eglGetProcAddress");
    if (host_get_proc) {
        func = host_get_proc(name);
    }
    return func;
}

namespace {

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

// ANGLE first-touch phải xảy ra trên MAIN thread. Bằng chứng log máy thật:
// GL TEST (chạy từ C++ kudroid, main thread) init ANGLE hoàn toàn OK
// (eglInitialize v1.5 -> pbuffer -> context -> glClear -> GL_RENDERER=
// "ANGLE Metal Renderer: Apple A13 GPU"), còn triangle guest first-touch ANGLE
// TRÊN RENDER PTHREAD -> abort ngay sau eglGetDisplay trả VALID, trước cả
// eglInitialize (stack crash có chuỗi guard "(pretend done)" — static init của
// ANGLE bị guard shim chặn khi chạy lần đầu trên thread mới). Warm-up để mọi
// static init chạy trên main thread trước khi guest đụng tới.
// Warm-up chạy TRỌN pipeline mà GL TEST đã chứng minh hoạt động 100% trên
// main thread (init v1.5 -> pbuffer -> context -> makeCurrent -> glClear ->
// GL_RENDERER="ANGLE Metal Renderer: Apple A13 GPU"). Mỗi first-touch ANGLE
// trên render thread guest đều abort (bằng chứng: warm-up getDisplay+init ->
// guest chết ở eglInitialize; mở rộng -> guest qua được, chết ở eglMakeCurrent
// giờ ở +0x4f18). Đẩy hết first-touch (display, device Metal, command queue,
// context đầu tiên) lên main thread — render thread chỉ dùng lại state đã init.
extern "C" void kudroid_gpu_warmup_egl(void) {
    // EGL typedefs đầy đủ chưa có ở đây (khai báo sau trong file) — dùng local.
    typedef void* EGLConfig;
    typedef void* EGLSurface;
    typedef void* EGLContext;
    typedef unsigned int (*PFN_eglInitialize)(EGLDisplay, EGLint*, EGLint*);
    typedef EGLDisplay (*PFN_eglGetDisplay)(EGLNativeDisplayType);
    typedef int (*PFN_eglChooseConfig)(EGLDisplay, const EGLint*, EGLConfig*, EGLint, EGLint*);
    typedef EGLSurface (*PFN_eglCreatePbufferSurface)(EGLDisplay, EGLConfig, const EGLint*);
    typedef EGLContext (*PFN_eglCreateContext)(EGLDisplay, EGLConfig, EGLContext, const EGLint*);
    typedef unsigned int (*PFN_eglMakeCurrent)(EGLDisplay, EGLSurface, EGLSurface, EGLContext);

    auto get_display = (PFN_eglGetDisplay) get_egl_func("eglGetDisplay");
    auto init = (PFN_eglInitialize) get_egl_func("eglInitialize");
    auto choose_config = (PFN_eglChooseConfig) get_egl_func("eglChooseConfig");
    auto create_pbuffer = (PFN_eglCreatePbufferSurface) get_egl_func("eglCreatePbufferSurface");
    auto create_context = (PFN_eglCreateContext) get_egl_func("eglCreateContext");
    auto make_current = (PFN_eglMakeCurrent) get_egl_func("eglMakeCurrent");
    auto gl_clear = (void (*)(unsigned int)) get_gl_func("glClear");
    if (!get_display || !init || !choose_config || !create_pbuffer ||
        !create_context || !make_current || !gl_clear) {
        gpuLog("warmup: egl entry points missing (get_display=%p init=%p "
               "choose_config=%p create_pbuffer=%p create_context=%p "
               "make_current=%p gl_clear=%p)",
               (void*)get_display, (void*)init, (void*)choose_config,
               (void*)create_pbuffer, (void*)create_context,
               (void*)make_current, (void*)gl_clear);
        return;
    }
    EGLDisplay dpy = get_display((EGLNativeDisplayType)0);
    if (!dpy) {
        gpuLog("warmup: eglGetDisplay -> NULL");
        return;
    }
    EGLint major = 0, minor = 0;
    if (!init(dpy, &major, &minor)) {
        gpuLog("warmup: eglInitialize -> false");
        return;
    }
    gpuLog("warmup: eglInitialize -> true (major=%d minor=%d)", major, minor);

    // EGL 1.4 constants — giá trị chuẩn, dùng raw vì shim không include egl.h.
    #define W_EGL_SURFACE_TYPE 0x3033
    #define W_EGL_PBUFFER_BIT 0x0001
    #define W_EGL_RENDERABLE_TYPE 0x3040
    #define W_EGL_OPENGL_ES2_BIT 0x0004
    #define W_EGL_WIDTH 0x3057
    #define W_EGL_HEIGHT 0x3056
    #define W_EGL_CONTEXT_CLIENT_VERSION 0x3098
    #define W_EGL_NONE 0x3038
    const EGLint configAttribs[] = {
        W_EGL_SURFACE_TYPE, W_EGL_PBUFFER_BIT,
        W_EGL_RENDERABLE_TYPE, W_EGL_OPENGL_ES2_BIT,
        W_EGL_NONE
    };
    EGLConfig config = nullptr;
    EGLint numConfigs = 0;
    if (!choose_config(dpy, configAttribs, &config, 1, &numConfigs) || numConfigs < 1) {
        gpuLog("warmup: eglChooseConfig failed (num=%d)", numConfigs);
        return;
    }
    gpuLog("warmup: eglChooseConfig -> true (num=%d)", numConfigs);
    const EGLint pbufferAttribs[] = { W_EGL_WIDTH, 128, W_EGL_HEIGHT, 128, W_EGL_NONE };
    EGLSurface surface = create_pbuffer(dpy, config, pbufferAttribs);
    if (!surface) {
        gpuLog("warmup: eglCreatePbufferSurface -> NULL");
        return;
    }
    gpuLog("warmup: eglCreatePbufferSurface -> %p", (void*)surface);
    const EGLint contextAttribs[] = { W_EGL_CONTEXT_CLIENT_VERSION, 2, W_EGL_NONE };
    EGLContext context = create_context(dpy, config, (EGLContext)0, contextAttribs);
    if (!context) {
        gpuLog("warmup: eglCreateContext -> NULL");
        return;
    }
    gpuLog("warmup: eglCreateContext -> %p", (void*)context);
    if (!make_current(dpy, surface, surface, context)) {
        gpuLog("warmup: eglMakeCurrent -> false");
        return;
    }
    gpuLog("warmup: eglMakeCurrent -> true");
    gl_clear(0x4000 /* GL_COLOR_BUFFER_BIT */);
    gpuLog("warmup: glClear OK");
    make_current(dpy, (EGLSurface)0, (EGLSurface)0, (EGLContext)0);
    gpuLog("warmup: context released from main thread");
    #undef W_EGL_SURFACE_TYPE
    #undef W_EGL_PBUFFER_BIT
    #undef W_EGL_RENDERABLE_TYPE
    #undef W_EGL_OPENGL_ES2_BIT
    #undef W_EGL_WIDTH
    #undef W_EGL_HEIGHT
    #undef W_EGL_CONTEXT_CLIENT_VERSION
    #undef W_EGL_NONE
}

extern "C" EGLDisplay bionic_eglGetDisplay(EGLNativeDisplayType display_id) {
    (void)display_id;
    gpuLog("eglGetDisplay(display_id=%p)", (void*)display_id);
    // QUA QUA (bằng chứng log máy thật, không phải đoán): display trả từ
    // eglGetPlatformDisplayEXT(Metal) — dù "VALID" — làm process ABORT ngay
    // khi dùng (triangle: crash libtriangle_gles ngay sau eglGetDisplay, trước
    // cả log eglInitialize). Còn eglGetDisplay(0) fallback CHẠY TỐT TOÀN BỘ:
    // eglInitialize v1.5 → pbuffer surface → context → glClear OK →
    // GL_RENDERER="ANGLE Metal Renderer: Apple A13 GPU" → glGetError=0.
    // Cả hai đều là ANGLE Metal backend — fallback không mất gì, và là đường
    // duy nhất đã chứng minh hoạt động. Bỏ probe, dùng thẳng fallback.
    typedef EGLDisplay (*PFN_eglGetDisplay)(EGLNativeDisplayType);
    auto host_get_display = (PFN_eglGetDisplay) get_egl_func("eglGetDisplay");
    if (host_get_display) {
        // Luôn EGL_DEFAULT_DISPLAY (0) — CAMetalLayer chỉ dùng cho surface.
        EGLDisplay dpy = host_get_display((EGLNativeDisplayType)0);
        gpuLog("eglGetDisplay: fallback -> %s (%p)", dpy ? "VALID" : "NULL", (void*)dpy);
        return dpy;
    }

    gpuLog("eglGetDisplay: no eglGetDisplay entry point available");
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
        void* mvk = ::dlopen("@executable_path/Frameworks/MoltenVK.framework/MoltenVK", RTLD_NOW | RTLD_GLOBAL);
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
    KLOG(kDebug, "KuDroidGPU", "vkCreateAndroidSurfaceKHR → vkCreateMetalSurfaceEXT");
    if (!pCreateInfo || !pSurface) return -1; // VK_ERROR_INITIALIZATION_FAILED

    auto real = get_real_vkGetInstanceProcAddr();
    if (!real) {
        KLOG(kError, "KuDroidGPU", "ERROR: MoltenVK vkGetInstanceProcAddr not found");
        return -1;
    }
    auto createMetalSurface = (PFN_vkCreateMetalSurfaceEXT)real(instance, "vkCreateMetalSurfaceEXT");
    if (!createMetalSurface) {
        KLOG(kError, "KuDroidGPU", "ERROR: vkCreateMetalSurfaceEXT not found in MoltenVK");
        return -1;
    }

    VkMetalSurfaceCreateInfoEXT metalInfo = {};
    metalInfo.sType = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT;
    metalInfo.pNext = nullptr;
    metalInfo.flags = 0;
    metalInfo.pLayer = (pCreateInfo->window && pCreateInfo->window != (void*)1) ? pCreateInfo->window : g_metalLayer;

    VkResult r = createMetalSurface(instance, &metalInfo, pAllocator, pSurface);
    KLOG(kDebug, "KuDroidGPU", "vkCreateMetalSurfaceEXT returned %d (surface=%p)", (int)r, (void*)*pSurface);
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
        void* mvk = ::dlopen("@executable_path/Frameworks/MoltenVK.framework/MoltenVK", RTLD_NOW | RTLD_GLOBAL);
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

    // Clamp theo capacity.
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

} // namespace

void* get_vk_func(const char* name) {
    if (!name) return nullptr;
    if (strcmp(name, "vkGetInstanceProcAddr") == 0) {
        return (void*)&bionic_vkGetInstanceProcAddr;
    }
    if (strcmp(name, "vkCreateAndroidSurfaceKHR") == 0) {
        return (void*)&bionic_vkCreateAndroidSurfaceKHR;
    }
    if (strcmp(name, "vkEnumerateInstanceExtensionProperties") == 0) {
        return (void*)&bionic_vkEnumerateInstanceExtensionProperties;
    }
    auto real = get_real_vkGetInstanceProcAddr();
    if (real) {
        void* f = (void*)real(nullptr, name);
        if (f) return f;
    }
    void* mvk = ::dlopen("@executable_path/Frameworks/MoltenVK.framework/MoltenVK", RTLD_NOW | RTLD_GLOBAL);
    if (mvk) {
        void* f = ::dlsym(mvk, name);
        if (f) return f;
    }
    return ::dlsym(RTLD_DEFAULT, name);
}

namespace {

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
        void* resolvedLayer = g_metalLayer;
        if (win) {
            auto* nw = static_cast<KuDroidNativeWindow*>(win);
            if (nw->magic == 0x4B554457 && nw->layer) {
                resolvedLayer = nw->layer;
            }
        }
        EGLNativeWindowType nativeWin = (EGLNativeWindowType)(resolvedLayer ? resolvedLayer : g_metalLayer);
        gpuLog("eglCreateWindowSurface: calling ANGLE with layer=%p...", (void*)nativeWin);
#if defined(__APPLE__)
        void* pool = objc_autoreleasePoolPush();
        EGLSurface s = host_func(dpy, config, nativeWin, attrib_list);
        objc_autoreleasePoolPop(pool);
        gpuLog("eglCreateWindowSurface returned %p", (void*)s);
        return s;
#else
        EGLSurface s = host_func(dpy, config, nativeWin, attrib_list);
        gpuLog("eglCreateWindowSurface returned %p", (void*)s);
        return s;
#endif
    }
    gpuLog("eglCreateWindowSurface: not found in host");
    return nullptr;
}

// ── EGL 1.x entry points còn thiếu — forward thẳng sang ANGLE ────────────────

#define EGL_FORWARD_ERR(name, what) gpuLog("%s: ANGLE %s not available", name, what)

// Gom phần lấy hàm ANGLE + check NULL: trả con trỏ hàm đúng signature hoặc
// nullptr (caller tự log lỗi). Bỏ boilerplate typedef PFN mỗi hàm.
template <typename Signature>
Signature* eglFn(const char* name) {
    return reinterpret_cast<Signature*>(get_egl_func(name));
}

extern "C" EGLBoolean bionic_eglInitialize(EGLDisplay dpy, EGLint* major, EGLint* minor) {
    auto f = eglFn<EGLBoolean(EGLDisplay, EGLint*, EGLint*)>("eglInitialize");
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
    gpuLog("eglTerminate(dpy=%p) intercepted -> NO-OP (preserving display for app lifetime)", (void*)dpy);
    return EGL_TRUE;
}

extern "C" EGLBoolean bionic_eglChooseConfig(EGLDisplay dpy, const EGLint* attrib_list,
                                             EGLConfig* configs, EGLint config_size,
                                             EGLint* num_config) {
    auto f = eglFn<EGLBoolean(EGLDisplay, const EGLint*, EGLConfig*, EGLint, EGLint*)>("eglChooseConfig");
    if (!f) { EGL_FORWARD_ERR("eglChooseConfig", ""); return EGL_FALSE; }
    gpuLog("eglChooseConfig: calling ANGLE...");
    EGLBoolean r = f(dpy, attrib_list, configs, config_size, num_config);
    gpuLog("eglChooseConfig -> %s (num=%d)", r ? "true" : "false",
           num_config ? *num_config : -1);
    return r;
}

extern "C" EGLBoolean bionic_eglGetConfigAttrib(EGLDisplay dpy, EGLConfig config,
                                                EGLint attribute, EGLint* value) {
    auto f = eglFn<EGLBoolean(EGLDisplay, EGLConfig, EGLint, EGLint*)>("eglGetConfigAttrib");
    if (!f) { EGL_FORWARD_ERR("eglGetConfigAttrib", ""); return EGL_FALSE; }
    EGLBoolean r = f(dpy, config, attribute, value);
    gpuLog("eglGetConfigAttrib(attr=0x%x) -> %s%s%s", (unsigned)attribute,
           r ? "true" : "false", r ? " value=" : "",
           (r && value) ? std::to_string(*value).c_str() : "");
    return r;
}

extern "C" EGLBoolean bionic_eglGetConfigs(EGLDisplay dpy, EGLConfig* configs,
                                           EGLint config_size, EGLint* num_config) {
    auto f = eglFn<EGLBoolean(EGLDisplay, EGLConfig*, EGLint, EGLint*)>("eglGetConfigs");
    if (!f) { EGL_FORWARD_ERR("eglGetConfigs", ""); return EGL_FALSE; }
    EGLBoolean r = f(dpy, configs, config_size, num_config);
    gpuLog("eglGetConfigs(size=%d) -> %s (num=%d)", config_size, r ? "true" : "false",
           num_config ? *num_config : -1);
    return r;
}

extern "C" EGLContext bionic_eglCreateContext(EGLDisplay dpy, EGLConfig config,
                                              EGLContext share_context,
                                              const EGLint* attrib_list) {
    auto f = eglFn<EGLContext(EGLDisplay, EGLConfig, EGLContext, const EGLint*)>("eglCreateContext");
    if (!f) { EGL_FORWARD_ERR("eglCreateContext", ""); return EGL_NO_CONTEXT; }
    gpuLog("eglCreateContext: calling ANGLE...");
    EGLContext ctx = f(dpy, config, share_context, attrib_list);
    gpuLog("eglCreateContext -> %p", (void*)ctx);
    return ctx;
}

extern "C" EGLBoolean bionic_eglDestroyContext(EGLDisplay dpy, EGLContext ctx) {
    auto f = eglFn<EGLBoolean(EGLDisplay, EGLContext)>("eglDestroyContext");
    if (!f) { EGL_FORWARD_ERR("eglDestroyContext", ""); return EGL_FALSE; }
    EGLBoolean r = f(dpy, ctx);
    gpuLog("eglDestroyContext(%p) -> %s", (void*)ctx, r ? "true" : "false");
    return r;
}

extern "C" EGLSurface bionic_eglCreatePbufferSurface(EGLDisplay dpy, EGLConfig config,
                                                     const EGLint* attrib_list) {
    auto f = eglFn<EGLSurface(EGLDisplay, EGLConfig, const EGLint*)>("eglCreatePbufferSurface");
    if (!f) { EGL_FORWARD_ERR("eglCreatePbufferSurface", ""); return EGL_NO_SURFACE; }
    EGLSurface s = f(dpy, config, attrib_list);
    gpuLog("eglCreatePbufferSurface -> %p", (void*)s);
    return s;
}

extern "C" EGLBoolean bionic_eglDestroySurface(EGLDisplay dpy, EGLSurface surface) {
    auto f = eglFn<EGLBoolean(EGLDisplay, EGLSurface)>("eglDestroySurface");
    if (!f) { EGL_FORWARD_ERR("eglDestroySurface", ""); return EGL_FALSE; }
    EGLBoolean r = f(dpy, surface);
    gpuLog("eglDestroySurface(%p) -> %s", (void*)surface, r ? "true" : "false");
    return r;
}

extern "C" EGLBoolean bionic_eglMakeCurrent(EGLDisplay dpy, EGLSurface draw,
                                            EGLSurface read, EGLContext ctx) {
    auto f = eglFn<EGLBoolean(EGLDisplay, EGLSurface, EGLSurface, EGLContext)>("eglMakeCurrent");
    if (!f) { EGL_FORWARD_ERR("eglMakeCurrent", ""); return EGL_FALSE; }
    gpuLog("eglMakeCurrent: calling ANGLE...");
    EGLBoolean r = f(dpy, draw, read, ctx);
    gpuLog("eglMakeCurrent(ctx=%p, draw=%p) -> %s", (void*)ctx, (void*)draw,
           r ? "true" : "false");
#if defined(__APPLE__)
    // Manage thread-local autorelease pool for ANGLE Metal.
    // When a context is bound, push a pool so all subsequent GL calls
    // (glCreateShader, glCompileShader, glDrawArrays, etc.) have a valid
    // autorelease pool for ANGLE Metal's internal ObjC allocations.
    // When the context is unbound (ctx==nullptr), pop the pool.
    if (r) {
        if (ctx) {
            if (!tls_autorelease_pool) {
                tls_autorelease_pool = objc_autoreleasePoolPush();
                gpuLog("eglMakeCurrent: pushed thread-local autorelease pool");
            }
        } else {
            if (tls_autorelease_pool) {
                objc_autoreleasePoolPop(tls_autorelease_pool);
                tls_autorelease_pool = nullptr;
                gpuLog("eglMakeCurrent: popped thread-local autorelease pool (ctx=NULL)");
            }
        }
    }
#endif
    return r;
}

extern "C" EGLContext bionic_eglGetCurrentContext(void) {
    auto f = eglFn<EGLContext(void)>("eglGetCurrentContext");
    if (!f) { EGL_FORWARD_ERR("eglGetCurrentContext", ""); return EGL_NO_CONTEXT; }
    EGLContext c = f();
    gpuLog("eglGetCurrentContext -> %p", (void*)c);
    return c;
}

extern "C" EGLSurface bionic_eglGetCurrentSurface(EGLint readdraw) {
    auto f = eglFn<EGLSurface(EGLint)>("eglGetCurrentSurface");
    if (!f) { EGL_FORWARD_ERR("eglGetCurrentSurface", ""); return EGL_NO_SURFACE; }
    EGLSurface s = f(readdraw);
    gpuLog("eglGetCurrentSurface(0x%x) -> %p", (unsigned)readdraw, (void*)s);
    return s;
}

extern "C" EGLDisplay bionic_eglGetCurrentDisplay(void) {
    auto f = eglFn<EGLDisplay(void)>("eglGetCurrentDisplay");
    if (!f) { EGL_FORWARD_ERR("eglGetCurrentDisplay", ""); return EGL_NO_DISPLAY; }
    EGLDisplay d = f();
    gpuLog("eglGetCurrentDisplay -> %p", (void*)d);
    return d;
}

extern "C" EGLBoolean bionic_eglSwapBuffers(EGLDisplay dpy, EGLSurface surface) {
    auto f = eglFn<EGLBoolean(EGLDisplay, EGLSurface)>("eglSwapBuffers");
    if (!f) { EGL_FORWARD_ERR("eglSwapBuffers", ""); return EGL_FALSE; }
#if defined(__APPLE__)
    if (tls_autorelease_pool) {
        objc_autoreleasePoolPop(tls_autorelease_pool);
        tls_autorelease_pool = nullptr;
    }
    tls_autorelease_pool = objc_autoreleasePoolPush();
#endif
    gpuLog("eglSwapBuffers: calling ANGLE...");
    EGLBoolean r = f(dpy, surface);
    gpuLog("eglSwapBuffers(surface=%p) -> %s", (void*)surface, r ? "true" : "false");
    return r;
}

extern "C" EGLBoolean bionic_eglSwapInterval(EGLDisplay dpy, EGLint interval) {
    auto f = eglFn<EGLBoolean(EGLDisplay, EGLint)>("eglSwapInterval");
    if (!f) { EGL_FORWARD_ERR("eglSwapInterval", ""); return EGL_FALSE; }
    EGLBoolean r = f(dpy, interval);
    gpuLog("eglSwapInterval(%d) -> %s", interval, r ? "true" : "false");
    return r;
}

extern "C" const char* bionic_eglQueryString(EGLDisplay dpy, EGLint name) {
    auto f = eglFn<const char*(EGLDisplay, EGLint)>("eglQueryString");
    if (!f) { EGL_FORWARD_ERR("eglQueryString", ""); return nullptr; }
    const char* s = f(dpy, name);
    gpuLog("eglQueryString(name=%d) -> %s", name, s ? s : "(null)");
    return s;
}

extern "C" EGLBoolean bionic_eglQuerySurface(EGLDisplay dpy, EGLSurface surface,
                                             EGLint attribute, EGLint* value) {
    auto f = eglFn<EGLBoolean(EGLDisplay, EGLSurface, EGLint, EGLint*)>("eglQuerySurface");
    if (!f) { EGL_FORWARD_ERR("eglQuerySurface", ""); return EGL_FALSE; }
    EGLBoolean r = f(dpy, surface, attribute, value);
    gpuLog("eglQuerySurface(attr=0x%x) -> %s%s%s", (unsigned)attribute,
           r ? "true" : "false", r ? " value=" : "",
           (r && value) ? std::to_string(*value).c_str() : "");
    return r;
}

extern "C" EGLint bionic_eglGetError(void) {
    auto f = eglFn<EGLint(void)>("eglGetError");
    if (!f) { EGL_FORWARD_ERR("eglGetError", ""); return EGL_SUCCESS; }
    EGLint e = f();
    gpuLog("eglGetError -> %s (0x%x)", e == EGL_SUCCESS ? "EGL_SUCCESS" : "ERROR", (unsigned)e);
    return e;
}

extern "C" EGLBoolean bionic_eglSurfaceAttrib(EGLDisplay dpy, EGLSurface surface,
                                              EGLint attribute, EGLint value) {
    auto f = eglFn<EGLBoolean(EGLDisplay, EGLSurface, EGLint, EGLint)>("eglSurfaceAttrib");
    if (!f) { EGL_FORWARD_ERR("eglSurfaceAttrib", ""); return EGL_FALSE; }
    EGLBoolean r = f(dpy, surface, attribute, value);
    gpuLog("eglSurfaceAttrib(attr=0x%x val=%d) -> %s", (unsigned)attribute, value,
           r ? "true" : "false");
    return r;
}

extern "C" EGLBoolean bionic_eglReleaseThread(void) {
    auto f = eglFn<EGLBoolean(void)>("eglReleaseThread");
    if (!f) { EGL_FORWARD_ERR("eglReleaseThread", ""); return EGL_TRUE; }
    EGLBoolean r = f();
    gpuLog("eglReleaseThread -> %s", r ? "true" : "false");
    return r;
}

extern "C" EGLBoolean bionic_eglBindAPI(EGLenum api) {
    auto f = eglFn<EGLBoolean(EGLenum)>("eglBindAPI");
    if (!f) { EGL_FORWARD_ERR("eglBindAPI", ""); return EGL_FALSE; }
    EGLBoolean r = f(api);
    gpuLog("eglBindAPI(0x%x) -> %s", (unsigned)api, r ? "true" : "false");
    return r;
}

extern "C" EGLenum bionic_eglQueryAPI(void) {
    auto f = eglFn<EGLenum(void)>("eglQueryAPI");
    if (!f) { EGL_FORWARD_ERR("eglQueryAPI", ""); return 0; }
    EGLenum e = f();
    gpuLog("eglQueryAPI -> 0x%x", (unsigned)e);
    return e;
}

extern "C" EGLDisplay bionic_eglGetPlatformDisplay(EGLenum platform, void* native_display,
                                                   const EGLint* attrib_list) {
    (void)native_display;
    auto f = eglFn<EGLDisplay(EGLenum, void*, const EGLint*)>("eglGetPlatformDisplay");
    if (!f) { EGL_FORWARD_ERR("eglGetPlatformDisplay", ""); return EGL_NO_DISPLAY; }
    // Same as EXT variant: native display phải là 0 (EGL_DEFAULT_DISPLAY),
    // CAMetalLayer chỉ dùng cho eglCreateWindowSurface.
    EGLDisplay d = f(platform, (void*)0, attrib_list);
    gpuLog("eglGetPlatformDisplay -> %p", (void*)d);
    return d;
}

extern "C" EGLBoolean bionic_eglBindTexImage(EGLDisplay dpy, EGLSurface surface, EGLint buffer) {
    auto f = eglFn<EGLBoolean(EGLDisplay, EGLSurface, EGLint)>("eglBindTexImage");
    if (!f) { EGL_FORWARD_ERR("eglBindTexImage", ""); return EGL_FALSE; }
    EGLBoolean r = f(dpy, surface, buffer);
    gpuLog("eglBindTexImage(surface=%p buffer=%d) -> %s", (void*)surface, buffer,
           r ? "true" : "false");
    return r;
}

extern "C" EGLBoolean bionic_eglReleaseTexImage(EGLDisplay dpy, EGLSurface surface, EGLint buffer) {
    auto f = eglFn<EGLBoolean(EGLDisplay, EGLSurface, EGLint)>("eglReleaseTexImage");
    if (!f) { EGL_FORWARD_ERR("eglReleaseTexImage", ""); return EGL_FALSE; }
    EGLBoolean r = f(dpy, surface, buffer);
    gpuLog("eglReleaseTexImage(surface=%p buffer=%d) -> %s", (void*)surface, buffer,
           r ? "true" : "false");
    return r;
}

extern "C" EGLBoolean bionic_eglWaitGL(void) {
    auto f = eglFn<EGLBoolean(void)>("eglWaitGL");
    if (!f) { EGL_FORWARD_ERR("eglWaitGL", ""); return EGL_TRUE; }
    EGLBoolean r = f();
    gpuLog("eglWaitGL -> %s", r ? "true" : "false");
    return r;
}

extern "C" EGLBoolean bionic_eglWaitNative(EGLint engine) {
    auto f = eglFn<EGLBoolean(EGLint)>("eglWaitNative");
    if (!f) { EGL_FORWARD_ERR("eglWaitNative", ""); return EGL_TRUE; }
    EGLBoolean r = f(engine);
    gpuLog("eglWaitNative(%d) -> %s", engine, r ? "true" : "false");
    return r;
}

// ── Hàm EGL/GLES từng bị ELF loader bind dummy (log Discord: "missing symbol
// bound to dummy: ...") vì thiếu trong bảng shim + ANGLE load RTLD_LOCAL.
// Forward thật sang ANGLE — dummy trả 0 làm game nhận giá trị sai → crash. ──

extern "C" unsigned int bionic_eglQueryContext(void* dpy, void* ctx, unsigned int attribute,
                                                int* value) {
    typedef unsigned int (*PFN)(void*, void*, unsigned int, int*);
    auto f = (PFN)get_egl_func("eglQueryContext");
    if (!f) { EGL_FORWARD_ERR("eglQueryContext", ""); return 0; }
    unsigned int r = f(dpy, ctx, attribute, value);
    gpuLog("eglQueryContext(attr=0x%x) -> %s%s%s", attribute,
           r ? "true" : "false", r ? " value=" : "",
           (r && value) ? std::to_string(*value).c_str() : "");
    return r;
}

extern "C" void bionic_glMemoryBarrier(unsigned int barriers) {
    typedef void (*PFN)(unsigned int);
    auto f = (PFN)get_gl_func("glMemoryBarrier");
    if (!f) { EGL_FORWARD_ERR("glMemoryBarrier", ""); return; }
    gpuLog("glMemoryBarrier(barriers=0x%x)", barriers);
    f(barriers);
}

extern "C" void bionic_glBindImageTexture(unsigned int unit, unsigned int texture, int level,
                                          unsigned char layered, int layer,
                                          unsigned int access, unsigned int format) {
    typedef void (*PFN)(unsigned int, unsigned int, int, unsigned char, int,
                        unsigned int, unsigned int);
    auto f = (PFN)get_gl_func("glBindImageTexture");
    if (!f) { EGL_FORWARD_ERR("glBindImageTexture", ""); return; }
    gpuLog("glBindImageTexture(unit=%u texture=%u level=%d layered=%u layer=%d)",
           unit, texture, level, layered, layer);
    f(unit, texture, level, layered, layer, access, format);
}

extern "C" void bionic_glFramebufferParameteri(unsigned int target, unsigned int pname, int param) {
    typedef void (*PFN)(unsigned int, unsigned int, int);
    auto f = (PFN)get_gl_func("glFramebufferParameteri");
    if (!f) { EGL_FORWARD_ERR("glFramebufferParameteri", ""); return; }
    gpuLog("glFramebufferParameteri(target=0x%x pname=0x%x param=%d)", target, pname, param);
    f(target, pname, param);
}

extern "C" unsigned int bionic_glCreateShader(unsigned int type) {
    typedef unsigned int (*PFN)(unsigned int);
    auto f = (PFN)get_gl_func("glCreateShader");
    if (!f) { EGL_FORWARD_ERR("glCreateShader", ""); return 0; }
    gpuLog("glCreateShader(type=0x%x): calling ANGLE...", type);
    unsigned int s = f(type);
    gpuLog("glCreateShader -> %u", s);
    return s;
}

extern "C" void bionic_glShaderSource(unsigned int shader, int count, const char* const* string, const int* length) {
    typedef void (*PFN)(unsigned int, int, const char* const*, const int*);
    auto f = (PFN)get_gl_func("glShaderSource");
    if (!f) { EGL_FORWARD_ERR("glShaderSource", ""); return; }

    gpuLog("glShaderSource(shader=%u count=%d): marshalling guest memory to host buffer...", shader, count);
    if (!string || count <= 0) {
        f(shader, count, string, length);
        return;
    }

    // Sao chép an toàn toàn bộ chuỗi shader từ vùng nhớ Guest (ELF/Stack) sang Host C++ heap
    std::vector<std::string> hostStrings;
    std::vector<const char*> hostPtrs;
    std::vector<int> hostLens;
    hostStrings.reserve(count);
    hostPtrs.reserve(count);
    hostLens.reserve(count);

    for (int i = 0; i < count; ++i) {
        if (string[i]) {
            int len = (length && length[i] > 0) ? length[i] : static_cast<int>(strlen(string[i]));
            hostStrings.emplace_back(string[i], len);
            hostLens.push_back(len);
        } else {
            hostStrings.emplace_back("");
            hostLens.push_back(0);
        }
    }

    for (size_t i = 0; i < hostStrings.size(); ++i) {
        hostPtrs.push_back(hostStrings[i].c_str());
    }

    if (!hostStrings.empty()) {
        char snippet[64] = {0};
        strncpy(snippet, hostStrings[0].c_str(), 40);
        gpuLog("glShaderSource(shader=%u): marshalled len=%zu snippet='%s...'",
               shader, hostStrings[0].size(), snippet);
    }

    // Truyền mảng con trỏ Host an toàn 100% xuống ANGLE
    f(shader, count, hostPtrs.data(), hostLens.data());
    gpuLog("glShaderSource(shader=%u) -> OK", shader);
}

extern "C" void bionic_glCompileShader(unsigned int shader) {
    typedef void (*PFN)(unsigned int);
    auto f = (PFN)get_gl_func("glCompileShader");
    if (!f) { EGL_FORWARD_ERR("glCompileShader", ""); return; }
    gpuLog("glCompileShader(shader=%u): calling ANGLE...", shader);
    f(shader);
    gpuLog("glCompileShader(shader=%u) -> OK", shader);
}

extern "C" void bionic_glGetShaderiv(unsigned int shader, unsigned int pname, int* params) {
    typedef void (*PFN)(unsigned int, unsigned int, int*);
    auto f = (PFN)get_gl_func("glGetShaderiv");
    if (!f) { EGL_FORWARD_ERR("glGetShaderiv", ""); return; }
    f(shader, pname, params);
    gpuLog("glGetShaderiv(shader=%u pname=0x%x param=%d)", shader, pname, params ? *params : 0);
}

extern "C" void bionic_glGetShaderInfoLog(unsigned int shader, int maxLength, int* length, char* infoLog) {
    typedef void (*PFN)(unsigned int, int, int*, char*);
    auto f = (PFN)get_gl_func("glGetShaderInfoLog");
    if (!f) { EGL_FORWARD_ERR("glGetShaderInfoLog", ""); return; }
    f(shader, maxLength, length, infoLog);
    gpuLog("glGetShaderInfoLog(shader=%u log='%s')", shader, infoLog ? infoLog : "");
}

extern "C" unsigned int bionic_glCreateProgram(void) {
    typedef unsigned int (*PFN)(void);
    auto f = (PFN)get_gl_func("glCreateProgram");
    if (!f) { EGL_FORWARD_ERR("glCreateProgram", ""); return 0; }
    gpuLog("glCreateProgram: calling ANGLE...");
    unsigned int p = f();
    gpuLog("glCreateProgram -> %u", p);
    return p;
}

extern "C" void bionic_glAttachShader(unsigned int program, unsigned int shader) {
    typedef void (*PFN)(unsigned int, unsigned int);
    auto f = (PFN)get_gl_func("glAttachShader");
    if (!f) { EGL_FORWARD_ERR("glAttachShader", ""); return; }
    gpuLog("glAttachShader(prog=%u, shader=%u)", program, shader);
    f(program, shader);
}

extern "C" void bionic_glLinkProgram(unsigned int program) {
    typedef void (*PFN)(unsigned int);
    auto f = (PFN)get_gl_func("glLinkProgram");
    if (!f) { EGL_FORWARD_ERR("glLinkProgram", ""); return; }
    gpuLog("glLinkProgram(program=%u): calling ANGLE...", program);
#if defined(__APPLE__)
    void* pool = objc_autoreleasePoolPush();
    f(program);
    objc_autoreleasePoolPop(pool);
#else
    f(program);
#endif
    gpuLog("glLinkProgram(program=%u) -> OK", program);
}

extern "C" void bionic_glGetProgramiv(unsigned int program, unsigned int pname, int* params) {
    typedef void (*PFN)(unsigned int, unsigned int, int*);
    auto f = (PFN)get_gl_func("glGetProgramiv");
    if (!f) { EGL_FORWARD_ERR("glGetProgramiv", ""); return; }
    f(program, pname, params);
    gpuLog("glGetProgramiv(prog=%u pname=0x%x param=%d)", program, pname, params ? *params : 0);
}

extern "C" void bionic_glUseProgram(unsigned int program) {
    typedef void (*PFN)(unsigned int);
    auto f = (PFN)get_gl_func("glUseProgram");
    if (!f) { EGL_FORWARD_ERR("glUseProgram", ""); return; }
    gpuLog("glUseProgram(prog=%u)", program);
    f(program);
}

extern "C" int bionic_glGetAttribLocation(unsigned int program, const char* name) {
    typedef int (*PFN)(unsigned int, const char*);
    auto f = (PFN)get_gl_func("glGetAttribLocation");
    if (!f) { EGL_FORWARD_ERR("glGetAttribLocation", ""); return -1; }
    int loc = f(program, name);
    gpuLog("glGetAttribLocation(prog=%u name='%s') -> %d", program, name ? name : "", loc);
    return loc;
}

extern "C" void bionic_glEnableVertexAttribArray(unsigned int index) {
    typedef void (*PFN)(unsigned int);
    auto f = (PFN)get_gl_func("glEnableVertexAttribArray");
    if (!f) { EGL_FORWARD_ERR("glEnableVertexAttribArray", ""); return; }
    f(index);
}

extern "C" void bionic_glVertexAttribPointer(unsigned int index, int size, unsigned int type, unsigned char normalized, int stride, const void* pointer) {
    typedef void (*PFN)(unsigned int, int, unsigned int, unsigned char, int, const void*);
    auto f = (PFN)get_gl_func("glVertexAttribPointer");
    if (!f) { EGL_FORWARD_ERR("glVertexAttribPointer", ""); return; }
    gpuLog("glVertexAttribPointer(index=%u size=%d stride=%d ptr=%p)", index, size, stride, pointer);
    f(index, size, type, normalized, stride, pointer);
}

extern "C" void bionic_glClearColor(float red, float green, float blue, float alpha) {
    typedef void (*PFN)(float, float, float, float);
    auto f = (PFN)get_gl_func("glClearColor");
    if (!f) { EGL_FORWARD_ERR("glClearColor", ""); return; }
    f(red, green, blue, alpha);
}

extern "C" void bionic_glClear(unsigned int mask) {
    typedef void (*PFN)(unsigned int);
    auto f = (PFN)get_gl_func("glClear");
    if (!f) { EGL_FORWARD_ERR("glClear", ""); return; }
    f(mask);
}

extern "C" void bionic_glDrawArrays(unsigned int mode, int first, int count) {
    typedef void (*PFN)(unsigned int, int, int);
    auto f = (PFN)get_gl_func("glDrawArrays");
    if (!f) { EGL_FORWARD_ERR("glDrawArrays", ""); return; }
    gpuLog("glDrawArrays(mode=0x%x first=%d count=%d): calling ANGLE...", mode, first, count);
#if defined(__APPLE__)
    void* pool = objc_autoreleasePoolPush();
    f(mode, first, count);
    objc_autoreleasePoolPop(pool);
#else
    f(mode, first, count);
#endif
    gpuLog("glDrawArrays -> OK");
}

extern "C" void bionic_glViewport(int x, int y, int width, int height) {
    typedef void (*PFN)(int, int, int, int);
    auto f = (PFN)get_gl_func("glViewport");
    if (!f) { EGL_FORWARD_ERR("glViewport", ""); return; }
    gpuLog("glViewport(%d, %d, %d, %d)", x, y, width, height);
    f(x, y, width, height);
}

extern "C" void bionic_glGenBuffers(int n, unsigned int* buffers) {
    typedef void (*PFN)(int, unsigned int*);
    auto f = (PFN)get_gl_func("glGenBuffers");
    if (!f) { EGL_FORWARD_ERR("glGenBuffers", ""); return; }
    f(n, buffers);
    gpuLog("glGenBuffers(n=%d) -> buf=%u", n, (buffers && n > 0) ? buffers[0] : 0);
}

extern "C" void bionic_glBindBuffer(unsigned int target, unsigned int buffer) {
    typedef void (*PFN)(unsigned int, unsigned int);
    auto f = (PFN)get_gl_func("glBindBuffer");
    if (!f) { EGL_FORWARD_ERR("glBindBuffer", ""); return; }
    gpuLog("glBindBuffer(target=0x%x buf=%u)", target, buffer);
    f(target, buffer);
}

extern "C" void bionic_glBufferData(unsigned int target, long size, const void* data, unsigned int usage) {
    typedef void (*PFN)(unsigned int, long, const void*, unsigned int);
    auto f = (PFN)get_gl_func("glBufferData");
    if (!f) { EGL_FORWARD_ERR("glBufferData", ""); return; }
    gpuLog("glBufferData(target=0x%x size=%ld usage=0x%x)", target, size, usage);
    f(target, size, data, usage);
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
    {"eglQueryContext", reinterpret_cast<void*>(&bionic_eglQueryContext)},
    {"glMemoryBarrier", reinterpret_cast<void*>(&bionic_glMemoryBarrier)},
    {"glBindImageTexture", reinterpret_cast<void*>(&bionic_glBindImageTexture)},
    {"glFramebufferParameteri", reinterpret_cast<void*>(&bionic_glFramebufferParameteri)},
    {"glCreateShader", reinterpret_cast<void*>(&bionic_glCreateShader)},
    {"glShaderSource", reinterpret_cast<void*>(&bionic_glShaderSource)},
    {"glCompileShader", reinterpret_cast<void*>(&bionic_glCompileShader)},
    {"glGetShaderiv", reinterpret_cast<void*>(&bionic_glGetShaderiv)},
    {"glGetShaderInfoLog", reinterpret_cast<void*>(&bionic_glGetShaderInfoLog)},
    {"glCreateProgram", reinterpret_cast<void*>(&bionic_glCreateProgram)},
    {"glAttachShader", reinterpret_cast<void*>(&bionic_glAttachShader)},
    {"glLinkProgram", reinterpret_cast<void*>(&bionic_glLinkProgram)},
    {"glGetProgramiv", reinterpret_cast<void*>(&bionic_glGetProgramiv)},
    {"glUseProgram", reinterpret_cast<void*>(&bionic_glUseProgram)},
    {"glGetAttribLocation", reinterpret_cast<void*>(&bionic_glGetAttribLocation)},
    {"glEnableVertexAttribArray", reinterpret_cast<void*>(&bionic_glEnableVertexAttribArray)},
    {"glVertexAttribPointer", reinterpret_cast<void*>(&bionic_glVertexAttribPointer)},
    {"glClearColor", reinterpret_cast<void*>(&bionic_glClearColor)},
    {"glClear", reinterpret_cast<void*>(&bionic_glClear)},
    {"glDrawArrays", reinterpret_cast<void*>(&bionic_glDrawArrays)},
    {"glViewport", reinterpret_cast<void*>(&bionic_glViewport)},
    {"glGenBuffers", reinterpret_cast<void*>(&bionic_glGenBuffers)},
    {"glBindBuffer", reinterpret_cast<void*>(&bionic_glBindBuffer)},
    {"glBufferData", reinterpret_cast<void*>(&bionic_glBufferData)},
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
