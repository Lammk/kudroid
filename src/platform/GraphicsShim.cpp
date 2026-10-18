#include "kudroid/platform/GraphicsShim.h"
#include "kudroid/kudroid_bridge.h"
#include "kudroid/platform/BundledFramework.h"
#include "kudroid/platform/FramePacer.h"
#include "kudroid/platform/GuestClock.h"
#include "kudroid/platform/SurfaceTextureHost.h"
#include "kudroid/Log.h"
#include <jni.h>
#include <dlfcn.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cstdarg>
#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>

#if defined(__APPLE__)
#include <pthread.h>
#include <dispatch/dispatch.h>
#include <objc/runtime.h>
#include <objc/message.h>
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
extern float g_metalLayerDensity;

// Frame-liveness hook (defined in kudroid_bridge.cpp): stamps the last
// successful present so the watchdog can tell a dead frame loop from a busy
// one even when no tracked I/O is flowing.
extern "C" void kudroid_note_frame_presented(void);

// Log directory owned by the bridge; weak so the shim also links on its own.
extern const char* g_kudroid_log_dir_ptr __attribute__((weak));

namespace kudroid {

namespace {

// Log the GPU through the standard pipeline (KLOG → stdout + file + crash buffer) to run
// crash after "log up to crash" is no longer empty. Keep the same text/priority (debug).
//
// Sampled: a scene load emits per-draw glUseProgram/glVertexAttribPointer at hundreds
// per frame — 12k lines in one 80-second ULTRAKILL load, and the log write sits on the
// render thread between draws. Keep the first N (setup state is unique) and then one in
// 64: the log stays a presence detector without taxing every draw. Pass
// KUDROID_GPU_LOG_ALL=1 to restore the full stream for a session.
static std::atomic<int> g_gpuLogCount{0};
static std::atomic<unsigned> g_materialAuditCount{0};
static std::atomic<unsigned> g_materialAuditEpoch{0};
static bool gpu_log_all() {
    static const bool all = [] {
        const char* env = std::getenv("KUDROID_GPU_LOG_ALL");
        return env != nullptr && env[0] == '1';
    }();
    return all;
}
static void gpuLog(const char* fmt, ...) {
    const int n = g_gpuLogCount.fetch_add(1, std::memory_order_relaxed);
    // Per-call GL chatter past the startup window costs a log-lock round trip on the
    // render thread for nothing; KUDROID_GPU_LOG_ALL keeps it.
    if (!gpu_log_all() && n >= 256) return;
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    ::kudroid::log::write(::kudroid::log::kDebug, "KuDroidGPU", "%s", buf);
}

#if defined(__APPLE__)
#include <CoreGraphics/CoreGraphics.h>
#include <objc/runtime.h>
#include <objc/message.h>

static void* s_fallbackLayer = nullptr;

static void* get_or_create_fallback_metal_layer() {
    if (g_metalLayer) return g_metalLayer;
    if (s_fallbackLayer) return s_fallbackLayer;

    Class clsLayer = objc_getClass("CAMetalLayer");
    if (!clsLayer) return nullptr;

    typedef id (*AllocFn)(id, SEL);
    typedef id (*InitFn)(id, SEL);
    typedef void (*SetDevFn)(id, SEL, id);

    auto allocFn = reinterpret_cast<AllocFn>(objc_msgSend);
    auto initFn = reinterpret_cast<InitFn>(objc_msgSend);
    auto setDevFn = reinterpret_cast<SetDevFn>(objc_msgSend);

    id layerInstance = initFn(allocFn(reinterpret_cast<id>(clsLayer), sel_registerName("alloc")), sel_registerName("init"));
    if (!layerInstance) return nullptr;

    typedef void* (*MTLDevFn)();
    auto mtlDevFn = reinterpret_cast<MTLDevFn>(::dlsym(RTLD_DEFAULT, "MTLCreateSystemDefaultDevice"));
    if (mtlDevFn) {
        void* dev = mtlDevFn();
        if (dev) {
            setDevFn(layerInstance, sel_registerName("setDevice:"), reinterpret_cast<id>(dev));
        }
    }
    const int w = g_metalLayerWidth > 0 ? g_metalLayerWidth : 1080;
    const int h = g_metalLayerHeight > 0 ? g_metalLayerHeight : 1920;
    const float scale = g_metalLayerDensity > 0.0f ? g_metalLayerDensity : 2.0f;
    typedef void (*SetBoolFn)(id, SEL, BOOL);
    typedef void (*SetFloatFn)(id, SEL, double);
    typedef void (*SetRectFn)(id, SEL, struct CGRect);
    typedef void (*SetSizeFn)(id, SEL, struct CGSize);
    auto setBoolFn = reinterpret_cast<SetBoolFn>(objc_msgSend);
    auto setFloatFn = reinterpret_cast<SetFloatFn>(objc_msgSend);
    auto setRectFn = reinterpret_cast<SetRectFn>(objc_msgSend);
    auto setSizeFn = reinterpret_cast<SetSizeFn>(objc_msgSend);

    setBoolFn(layerInstance, sel_registerName("setOpaque:"), YES);
    setBoolFn(layerInstance, sel_registerName("setHidden:"), NO);
    setFloatFn(layerInstance, sel_registerName("setContentsScale:"), scale);
    setRectFn(layerInstance, sel_registerName("setBounds:"), CGRectMake(0, 0, w / scale, h / scale));
    setSizeFn(layerInstance, sel_registerName("setDrawableSize:"), CGSizeMake(w, h));

    s_fallbackLayer = reinterpret_cast<void*>(layerInstance);
    gpuLog("Created fallback headless CAMetalLayer: %p (%dx%d)", s_fallbackLayer, w, h);
    return s_fallbackLayer;
}
#else
[[maybe_unused]] static void* s_fallbackLayer = nullptr;
#endif

// Standard ANativeWindow structure for KuDroid
struct KuDroidNativeWindow {
    uint32_t magic;
    void* layer;
    int32_t width;
    int32_t height;
    int32_t format;
    std::atomic<int32_t> refCount;
    // Non-zero when the Surface wraps a SurfaceTexture: frames a codec "renders" into this
    // window are announced to that texture's listener instead of reaching the screen.
    int32_t surfaceTextureId;

    KuDroidNativeWindow()
        : magic(0x4B554457), layer(nullptr), width(1080), height(1920), format(1), refCount(1),
          surfaceTextureId(0) {}
};

static KuDroidNativeWindow g_nativeWindowInstance;

// Every window handed to the guest, so a later CAMetalLayer change can reach the
// ones already in flight.
//
// A guest calls ANativeWindow_fromSurface once, keeps the pointer, and renders
// through it forever. The shell, meanwhile, can bind a different CAMetalLayer
// afterwards: the app starts on one UIView and the guest-app runner installs its
// own on top, so kudroid_set_metal_layer arrives a second time with a different
// layer. Without this registry the guest kept drawing into the layer it captured
// first, which by then was covered by the new one — a black screen while the guest
// itself renders perfectly happily, with nothing in any log to say so.
static std::mutex g_nativeWindowsMutex;
static std::vector<KuDroidNativeWindow*> g_nativeWindows;

static void register_native_window(KuDroidNativeWindow* nw) {
    if (!nw) return;
    std::lock_guard<std::mutex> lock(g_nativeWindowsMutex);
    for (KuDroidNativeWindow* known : g_nativeWindows) {
        if (known == nw) return;
    }
    g_nativeWindows.push_back(nw);
}

static void unregister_native_window(KuDroidNativeWindow* nw) {
    std::lock_guard<std::mutex> lock(g_nativeWindowsMutex);
    for (size_t i = 0; i < g_nativeWindows.size(); ++i) {
        if (g_nativeWindows[i] == nw) {
            g_nativeWindows.erase(g_nativeWindows.begin() + static_cast<long>(i));
            return;
        }
    }
}

extern "C" void* bionic_ANativeWindow_fromSurface(void* env_ptr, void* surface_obj) {
    void* layer = g_metalLayer;
#if defined(__APPLE__)
    if (!layer) layer = get_or_create_fallback_metal_layer();
#endif

    int width = g_metalLayerWidth > 0 ? g_metalLayerWidth : 1080;
    int height = g_metalLayerHeight > 0 ? g_metalLayerHeight : 1920;

    if (env_ptr != nullptr && surface_obj != nullptr) {
        JNIEnv* env = reinterpret_cast<JNIEnv*>(env_ptr);
        jobject jsurf = reinterpret_cast<jobject>(surface_obj);
        jclass clazz = env->GetObjectClass(jsurf);
        if (clazz != nullptr) {
            jfieldID fid = env->GetFieldID(clazz, "mNativeObject", "J");
            if (fid != nullptr) {
                jlong existing = env->GetLongField(jsurf, fid);
                // The SurfaceTexture behind the Surface, if any (0 for a screen surface).
                jint textureId = 0;
                if (jfieldID tid = env->GetFieldID(clazz, "mSurfaceTextureId", "I")) {
                    textureId = env->GetIntField(jsurf, tid);
                } else if (env->ExceptionCheck()) {
                    env->ExceptionClear();
                }
                if (existing != 0) {
                    auto* nw = reinterpret_cast<KuDroidNativeWindow*>(existing);
                    if (nw->magic == 0x4B554457) {
                        nw->surfaceTextureId = textureId;
                        nw->layer = layer;
                        nw->width = width;
                        nw->height = height;
                        nw->refCount.fetch_add(1);
                        register_native_window(nw);
                        gpuLog("ANativeWindow_fromSurface reused %p (layer=%p, size=%dx%d)",
                               (void*)nw, nw->layer, nw->width, nw->height);
                        return nw;
                    }
                }
                auto* nw = new KuDroidNativeWindow();
                nw->magic = 0x4B554457;
                nw->layer = layer;
                nw->width = width;
                nw->height = height;
                nw->format = 1;
                nw->refCount.store(1);
                nw->surfaceTextureId = textureId;
                env->SetLongField(jsurf, fid, reinterpret_cast<jlong>(nw));
                register_native_window(nw);
                gpuLog("ANativeWindow_fromSurface allocated %p for Surface %p (layer=%p, size=%dx%d)",
                       (void*)nw, (void*)jsurf, nw->layer, nw->width, nw->height);
                return nw;
            }
        }
    }

    g_nativeWindowInstance.layer = layer;
    g_nativeWindowInstance.width = width;
    g_nativeWindowInstance.height = height;
    register_native_window(&g_nativeWindowInstance);
    gpuLog("ANativeWindow_fromSurface fallback -> %p (layer=%p, size=%dx%d)",
           (void*)&g_nativeWindowInstance, g_nativeWindowInstance.layer,
           g_nativeWindowInstance.width, g_nativeWindowInstance.height);
    return &g_nativeWindowInstance;
}

// The SurfaceTexture id behind a window handed out by ANativeWindow_fromSurface, 0 when the
// window is not backed by one (or is not ours).
extern "C" int kudroid_native_window_surface_texture_id(void* window) {
    if (window == nullptr) return 0;
    auto* nw = static_cast<KuDroidNativeWindow*>(window);
    return nw->magic == 0x4B554457 ? nw->surfaceTextureId : 0;
}

// Point every live guest window at the layer the shell is actually presenting.
// Called from kudroid_set_metal_layer, which can fire long after the guest took
// its window. Sizes are only widened to the new surface when they came from this
// same path; a guest that called setBuffersGeometry chose its own resolution and
// must keep it, because it has already sized its swapchain to match.
extern "C" void kudroid_gpu_rebind_native_windows(void* layer, int width, int height) {
    if (!layer) return;
    std::lock_guard<std::mutex> lock(g_nativeWindowsMutex);
    for (KuDroidNativeWindow* nw : g_nativeWindows) {
        if (!nw || nw->magic != 0x4B554457) continue;
        if (nw->layer == layer) continue;
        gpuLog("rebind window %p: layer %p -> %p (size %dx%d -> %dx%d)",
               (void*)nw, nw->layer, layer, nw->width, nw->height, width, height);
        nw->layer = layer;
        if (width > 0) nw->width = width;
        if (height > 0) nw->height = height;
    }
}

// Resize windows that follow the layer (same rule as rebind: a guest that set
// its own geometry via setBuffersGeometry keeps it). Rotation changes the
// drawable under a live swapchain; without this the guest keeps presenting at
// launch geometry into a layer that moved on.
extern "C" void kudroid_gpu_resize_native_windows(void* layer, int oldW, int oldH,
                                                  int width, int height) {
    if (!layer || width <= 0 || height <= 0) return;
    std::lock_guard<std::mutex> lock(g_nativeWindowsMutex);
    for (KuDroidNativeWindow* nw : g_nativeWindows) {
        if (!nw || nw->magic != 0x4B554457) continue;
        if (nw->layer != layer) continue;
        if (nw->width != oldW || nw->height != oldH) continue;  // guest-owned
        gpuLog("resize window %p on layer %p: size %dx%d -> %dx%d", (void*)nw,
               layer, nw->width, nw->height, width, height);
        nw->width = width;
        nw->height = height;
    }
}

// Which CAMetalLayer a guest window currently points at. Exists so the rebind
// above is observable — from a test, and from a log line when a black screen has
// to be told apart from a guest that is not drawing at all.
extern "C" void* kudroid_gpu_native_window_layer(void* window) {
    if (!window) return nullptr;
    auto* nw = static_cast<KuDroidNativeWindow*>(window);
    if (nw->magic != 0x4B554457) return nullptr;
    return nw->layer;
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
        // One decrement per release. This used to fetch_sub in the condition and
        // then fetch_sub AGAIN in the else branch, so every release past the last
        // one dropped the count by two and the window could be destroyed while the
        // guest still held it.
        const int32_t previous = nw->refCount.fetch_sub(1);
        if (nw != &g_nativeWindowInstance && previous == 1) {
            gpuLog("ANativeWindow_release destroying window %p", window);
            // Drop it from the rebind registry BEFORE freeing: a later
            // kudroid_set_metal_layer would otherwise walk into freed memory.
            unregister_native_window(nw);
            delete nw;
            return;
        }
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
// Guards the canvas triple: without it two lock() calls race realloc (one
// thread's outBuffer->bits freed under it) and unlockAndPost can read a
// half-reallocated buffer.
static std::mutex s_canvasMtx;

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
    std::lock_guard<std::mutex> lock(s_canvasMtx);
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

// Surface->layer table: a swapchain bakes its layer at creation, so the canvas
// must know which layer has an active swapchain to stop covering its drawables
// with static contents. Platform-neutral (void* only); enforced on Apple.
namespace {
constexpr int kSurfaceLayers = 8;
struct SurfaceLayer {
    void* surface = nullptr;
    void* layer = nullptr;
};
SurfaceLayer g_surfaceLayers[kSurfaceLayers];
std::mutex g_surfaceLayersMtx;
void* s_vulkanLayer = nullptr;
std::unordered_map<void*, void*> g_eglLayers;
// Host layer -> the sublayer GL presents into (see gl_layer_for_host).
using RenderLayers = std::unordered_map<void*, void*>;
RenderLayers g_glLayers;
RenderLayers g_vulkanLayers;

bool hardware_owns_layer(void* layer) {
    if (s_vulkanLayer && s_vulkanLayer == layer) return true;
    const auto vkHost = g_vulkanLayers.find(layer);
    if (vkHost != g_vulkanLayers.end()) {
        for (const auto& surface : g_surfaceLayers) {
            if (surface.layer == vkHost->second) return true;
        }
    }
    for (const auto& entry : g_eglLayers) {
        if (entry.second == layer) return true;
    }
    return false;
}
}  // namespace

extern "C" bool kudroid_gpu_canvas_allowed(void* layer) {
    std::lock_guard<std::mutex> lock(g_surfaceLayersMtx);
    return !hardware_owns_layer(layer);
}

static void register_egl_layer(void* surface, void* layer) {
    {
        std::lock_guard<std::mutex> lock(g_surfaceLayersMtx);
        g_eglLayers[surface] = layer;
    }
#if defined(__APPLE__)
    dispatch_async(dispatch_get_main_queue(), ^{
        std::lock_guard<std::mutex> lock(g_surfaceLayersMtx);
        if (!hardware_owns_layer(layer)) return;
        reinterpret_cast<void (*)(id, SEL, id)>(objc_msgSend)(
            reinterpret_cast<id>(layer), sel_registerName("setContents:"), nullptr);
    });
#endif
}

static void drop_gl_layer_for_host(void* host);

static void unregister_egl_layer(void* surface) {
    void* host = nullptr;
    bool hostStillHasGl = false;
    {
        std::lock_guard<std::mutex> lock(g_surfaceLayersMtx);
        auto it = g_eglLayers.find(surface);
        if (it == g_eglLayers.end()) return;
        host = it->second;
        g_eglLayers.erase(it);
        for (const auto& entry : g_eglLayers) {
            if (entry.second == host) { hostStillHasGl = true; break; }
        }
    }
    // The GL sublayer would otherwise stay on top and cover whatever draws next.
    if (!hostStillHasGl) drop_gl_layer_for_host(host);
}

#if defined(__APPLE__)
#include <CoreGraphics/CoreGraphics.h>
#include <objc/runtime.h>
#include <objc/message.h>

// Each renderer presents into a child CAMetalLayer, separate from the UI backing store.

// Main thread only. Fits |child| to |host|: same bounds, host scale, matching drawable.
static void render_layer_sync_geometry(id child, id host, int w, int h, float density) {
    typedef CGRect (*GetRectFn)(id, SEL);
    typedef double (*GetFloatFn)(id, SEL);
    typedef void (*SetRectFn)(id, SEL, CGRect);
    typedef void (*SetFloatFn)(id, SEL, double);
    typedef void (*SetSizeFn)(id, SEL, CGSize);
    typedef void (*VoidFn)(id, SEL);
    typedef void (*SetBoolFn)(id, SEL, BOOL);
    auto getRect = reinterpret_cast<GetRectFn>(objc_msgSend);
    auto getFloat = reinterpret_cast<GetFloatFn>(objc_msgSend);
    auto setRect = reinterpret_cast<SetRectFn>(objc_msgSend);
    auto setFloat = reinterpret_cast<SetFloatFn>(objc_msgSend);
    auto setSize = reinterpret_cast<SetSizeFn>(objc_msgSend);
    auto voidCall = reinterpret_cast<VoidFn>(objc_msgSend);
    auto setBool = reinterpret_cast<SetBoolFn>(objc_msgSend);

    double scale = getFloat(host, sel_registerName("contentsScale"));
    if (scale <= 0.0) scale = density > 0.0f ? density : 2.0;
    const CGRect hostBounds = getRect(host, sel_registerName("bounds"));
    double bw = hostBounds.size.width;
    double bh = hostBounds.size.height;
    if (bw <= 0.0 || bh <= 0.0) {  // host not laid out yet
        bw = (w > 0 ? w : 1080) / scale;
        bh = (h > 0 ? h : 1920) / scale;
    }

    // Implicit animations would glide the layer to its new size.
    Class clsTxn = objc_getClass("CATransaction");
    if (clsTxn) {
        voidCall(reinterpret_cast<id>(clsTxn), sel_registerName("begin"));
        setBool(reinterpret_cast<id>(clsTxn), sel_registerName("setDisableActions:"), YES);
    }
    setRect(child, sel_registerName("setFrame:"), CGRectMake(0, 0, bw, bh));
    setFloat(child, sel_registerName("setContentsScale:"), scale);
    setSize(child, sel_registerName("setDrawableSize:"), CGSizeMake(bw * scale, bh * scale));
    if (clsTxn) voidCall(reinterpret_cast<id>(clsTxn), sel_registerName("commit"));
}

// Main thread only.
static void render_layer_create_on_main(void* hostPtr, RenderLayers* layers, const char* renderer) {
    {
        std::lock_guard<std::mutex> lock(g_surfaceLayersMtx);
        if (layers->count(hostPtr)) return;
    }
    Class clsCALayer = objc_getClass("CALayer");
    Class clsMetal = objc_getClass("CAMetalLayer");
    if (!clsCALayer || !clsMetal) return;
    typedef BOOL (*IsKindOfFn)(id, SEL, Class);
    typedef id (*IdRetFn)(id, SEL);
    typedef void (*SetBoolFn)(id, SEL, BOOL);
    typedef void (*SetIdFn)(id, SEL, id);
    auto isKindOf = reinterpret_cast<IsKindOfFn>(objc_msgSend);
    auto idRet = reinterpret_cast<IdRetFn>(objc_msgSend);
    auto setBool = reinterpret_cast<SetBoolFn>(objc_msgSend);
    auto setId = reinterpret_cast<SetIdFn>(objc_msgSend);

    id host = reinterpret_cast<id>(hostPtr);
    if (!isKindOf(host, sel_registerName("isKindOfClass:"), clsCALayer)) return;
    id child = idRet(reinterpret_cast<id>(clsMetal), sel_registerName("layer"));
    if (!child) return;
    idRet(child, sel_registerName("retain"));  // balanced in drop_render_layer_for_host
    setBool(child, sel_registerName("setOpaque:"), YES);
    render_layer_sync_geometry(child, host, g_metalLayerWidth, g_metalLayerHeight, g_metalLayerDensity);
    setId(host, sel_registerName("addSublayer:"), child);
    {
        std::lock_guard<std::mutex> lock(g_surfaceLayersMtx);
        (*layers)[hostPtr] = child;
    }
    gpuLog("%s layer %p attached under host %p", renderer, (void*)child, hostPtr);
}

// Create each renderer's child layer on the host UI thread.
static void* render_layer_for_host(void* host, RenderLayers* layers, const char* renderer) {
    if (!host) return nullptr;
    auto existing = [host, layers]() -> void* {
        std::lock_guard<std::mutex> lock(g_surfaceLayersMtx);
        auto it = layers->find(host);
        return it == layers->end() ? nullptr : it->second;
    };
    if (void* child = existing()) return child;
    if (pthread_main_np()) {
        render_layer_create_on_main(host, layers, renderer);
    } else {
        dispatch_semaphore_t done = dispatch_semaphore_create(0);
        dispatch_async(dispatch_get_main_queue(), ^{
            render_layer_create_on_main(host, layers, renderer);
            dispatch_semaphore_signal(done);
        });
        // Not released on timeout: the block may still signal it.
        if (dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 2 * NSEC_PER_SEC)) == 0) {
            dispatch_release(done);
        } else {
            gpuLog("%s layer creation timed out for host %p", renderer, host);
        }
    }
    void* child = existing();
    return child;
}

static void drop_render_layer_for_host(void* host, RenderLayers* layers) {
    void* child = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_surfaceLayersMtx);
        auto it = layers->find(host);
        if (it == layers->end()) return;
        child = it->second;
        layers->erase(it);
    }
    dispatch_async(dispatch_get_main_queue(), ^{
        typedef void (*VoidFn)(id, SEL);
        auto voidCall = reinterpret_cast<VoidFn>(objc_msgSend);
        voidCall(reinterpret_cast<id>(child), sel_registerName("removeFromSuperlayer"));
        voidCall(reinterpret_cast<id>(child), sel_registerName("release"));
    });
}

static void* gl_layer_for_host(void* host) {
    void* layer = render_layer_for_host(host, &g_glLayers, "GL");
    return layer != nullptr ? layer : host;
}

static void drop_gl_layer_for_host(void* host) {
    drop_render_layer_for_host(host, &g_glLayers);
}

// Follows the host through resize/rotation. Called by kudroid_set_metal_layer.
extern "C" void kudroid_gpu_sync_gl_layers(void* host, int w, int h, float density) {
    if (!host) return;
    auto apply = ^{
        void* gl = nullptr;
        {
            std::lock_guard<std::mutex> lock(g_surfaceLayersMtx);
            auto it = g_glLayers.find(host);
            if (it != g_glLayers.end()) gl = it->second;
        }
        if (gl) render_layer_sync_geometry(reinterpret_cast<id>(gl), reinterpret_cast<id>(host), w, h, density);
    };
    if (pthread_main_np()) apply();
    else dispatch_async(dispatch_get_main_queue(), apply);
}

static void kudroid_ensure_vulkan_layer_attached(void* vk_layer) {
    if (vk_layer == nullptr) return;
    dispatch_async(dispatch_get_main_queue(), ^{
        void* hostPtr = g_metalLayer;
        if (hostPtr == nullptr || hostPtr == vk_layer) return;
        {
            std::lock_guard<std::mutex> lock(g_surfaceLayersMtx);
            if (s_vulkanLayer != vk_layer) return;
        }
        id layer = reinterpret_cast<id>(vk_layer);
        id host = reinterpret_cast<id>(hostPtr);
        auto getId = reinterpret_cast<id (*)(id, SEL)>(objc_msgSend);
        auto setId = reinterpret_cast<void (*)(id, SEL, id)>(objc_msgSend);
        if (getId(layer, sel_registerName("superlayer")) != host) {
            setId(host, sel_registerName("addSublayer:"), layer);
        }
        render_layer_sync_geometry(layer, host, g_metalLayerWidth,
                                   g_metalLayerHeight, g_metalLayerDensity);
    });
}

extern "C" void kudroid_gpu_attach_vulkan_layer(void* hostLayer) {
    if (hostLayer == nullptr) return;
    void* layer;
    {
        std::lock_guard<std::mutex> lock(g_surfaceLayersMtx);
        layer = s_vulkanLayer;
    }
    kudroid_ensure_vulkan_layer_attached(layer);
}

extern "C" void kudroid_blit_canvas_to_layer(void* layer, const void* bits, int width, int height) {
    if (!layer || !bits || width <= 0 || height <= 0) return;

    // One producer per layer: if Vulkan is active, do not cover drawables
    {
        std::lock_guard<std::mutex> lock(g_surfaceLayersMtx);
        if (hardware_owns_layer(layer)) {
            gpuLog("blit skipped on hardware layer=%p %dx%d", layer, width, height);
            return;
        }
    }
    gpuLog("blit layer=%p %dx%d", layer, width, height);

    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
    if (!colorSpace) return;

    // Snapshot: the provider does not copy, and the async setContents below
    // runs later on the main thread — a realloc of s_canvasBits in between
    // would make the provider read freed memory. The copy is owned by the
    // provider and freed by its release callback.
    const size_t rowBytes = static_cast<size_t>(width) * 4;
    const size_t byteCount = rowBytes * static_cast<size_t>(height);
    void* snapshot = malloc(byteCount);
    if (!snapshot) {
        CGColorSpaceRelease(colorSpace);
        return;
    }
    std::memcpy(snapshot, bits, byteCount);
    CGDataProviderRef provider = CGDataProviderCreateWithData(
        snapshot, snapshot, byteCount, [](void* info, const void*, size_t) {
            std::free(info);
        });
    if (!provider) {
        CGColorSpaceRelease(colorSpace);
        return;
    }

    CGImageRef image = CGImageCreate(
        width, height, 8, 32, width * 4,
        colorSpace,
        kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big,
        provider, nullptr, false, kCGRenderingIntentDefault
    );

    CGDataProviderRelease(provider);
    CGColorSpaceRelease(colorSpace);

    if (!image) return;

    // Assign CGImage directly to CALayer/CAMetalLayer on Main Thread
    dispatch_async(dispatch_get_main_queue(), ^{
        std::lock_guard<std::mutex> lock(g_surfaceLayersMtx);
        if (hardware_owns_layer(layer)) {
            CGImageRelease(image);
            return;
        }
        Class clsCATransaction = objc_getClass("CATransaction");
        if (clsCATransaction) {
            typedef void (*TransBeginFn)(id, SEL);
            typedef void (*TransSetBoolFn)(id, SEL, BOOL);
            typedef void (*TransCommitFn)(id, SEL);
            
            auto beginFn = reinterpret_cast<TransBeginFn>(objc_msgSend);
            auto setBoolFn = reinterpret_cast<TransSetBoolFn>(objc_msgSend);
            auto commitFn = reinterpret_cast<TransCommitFn>(objc_msgSend);

            beginFn(reinterpret_cast<id>(clsCATransaction), sel_registerName("begin"));
            setBoolFn(reinterpret_cast<id>(clsCATransaction), sel_registerName("setDisableActions:"), YES);

            typedef void (*SetContentsFn)(id, SEL, id);
            auto setContents = reinterpret_cast<SetContentsFn>(objc_msgSend);
            setContents(reinterpret_cast<id>(layer), sel_registerName("setContents:"), reinterpret_cast<id>(const_cast<CGImageRef>(image)));

            commitFn(reinterpret_cast<id>(clsCATransaction), sel_registerName("commit"));
        }
        CGImageRelease(image);
    });
}
#else
static void* gl_layer_for_host(void* host) { return host; }
static void drop_gl_layer_for_host(void* host) { (void)host; }
extern "C" void kudroid_gpu_sync_gl_layers(void* host, int w, int h, float density) {
    (void)host; (void)w; (void)h; (void)density;
}
static void kudroid_ensure_vulkan_layer_attached(void* vk_layer) { (void)vk_layer; }
extern "C" void kudroid_gpu_attach_vulkan_layer(void* hostLayer) { (void)hostLayer; }
extern "C" void kudroid_blit_canvas_to_layer(void* layer, const void* bits, int width, int height) {
    (void)layer; (void)bits; (void)width; (void)height;
}
#endif

extern "C" int bionic_ANativeWindow_unlockAndPost(void* window) {
    (void)window;
    // Copy the triple under the mutex; blit snapshots the bytes synchronously,
    // so the lock is not held across the main-thread dispatch.
    void* bits = nullptr;
    int w = 0, h = 0;
    {
        std::lock_guard<std::mutex> lock(s_canvasMtx);
        bits = s_canvasBits;
        w = s_canvasWidth;
        h = s_canvasHeight;
    }
    if (!bits || w <= 0 || h <= 0) return 0;
#if defined(__APPLE__)
    if (g_metalLayer) {
        kudroid_blit_canvas_to_layer(g_metalLayer, bits, w, h);
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

    void* addr = nullptr;
    // Resolve from the ANGLE handle directly (loaded RTLD_LOCAL).
    static void* egl_handle = nullptr;
    if (!egl_handle) {
        egl_handle = dlopen_bundled_framework("libEGL.framework/libEGL",
                                              RTLD_NOW | RTLD_LOCAL);
    }
    if (egl_handle) {
        auto host_func = (PFN_eglGetProcAddress) ::dlsym(egl_handle, "eglGetProcAddress");
        if (host_func) {
            addr = host_func(procname);
            KLOG(kDebug, "KuDroidGPU", "eglGetProcAddress: %s returned %s", procname, addr ? "VALID" : "NULL");
        }
    }
    if (!addr) {
        // Fallback to RTLD_DEFAULT.
        auto host_func = (PFN_eglGetProcAddress) ::dlsym(RTLD_DEFAULT, "eglGetProcAddress");
        if (host_func) {
            addr = host_func(procname);
            KLOG(kDebug, "KuDroidGPU", "eglGetProcAddress: %s returned %s", procname, addr ? "VALID" : "NULL");
        } else {
            KLOG(kDebug, "KuDroidGPU", "eglGetProcAddress: host function not found");
        }
    }
    return addr;
}

} // namespace

void* get_gl_func(const char* name) {
    if (!name) return nullptr;
    static void* gl_handle = nullptr;
    if (!gl_handle) {
        gl_handle = dlopen_bundled_framework("libGLESv2.framework/libGLESv2",
                                            RTLD_NOW | RTLD_GLOBAL);
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


// Per-call-site cache for GL entry points named by a string literal: the wrappers run
// thousands of times a frame and a dlsym per call is pure overhead. A miss is retried.
#define GL_FUNC_CACHED(NAME)                                                         \
    ([]() -> void* {                                                                 \
        static std::atomic<void*> cached{nullptr};                                   \
        void* fn = cached.load(std::memory_order_relaxed);                           \
        if (!fn) {                                                                   \
            fn = get_gl_func(NAME);                                                  \
            if (fn) cached.store(fn, std::memory_order_relaxed);                     \
        }                                                                            \
        return fn;                                                                   \
    }())

void* get_egl_func(const char* name) {
    if (!name) return nullptr;
    static void* egl_handle = nullptr;
    if (!egl_handle) {
        egl_handle = dlopen_bundled_framework("libEGL.framework/libEGL",
                                              RTLD_NOW | RTLD_GLOBAL);
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

// ANGLE first-touch must occur on the MAIN thread. Evidence of real machine log:
// GL TEST (run from C++ kudroid, main thread) init ANGLE is completely OK
// (eglInitialize v1.5 -> pbuffer -> context -> glClear -> GL_RENDERER=
// "ANGLE Metal Renderer: Apple A13 GPU"), also triangle guest first-touch ANGLE
// ON RENDER PTHREAD -> abort immediately after eglGetDisplay returns VALID, first
// eglInitialize (stack crash has guard string "(pretend done)" — static init of
// ANGLE is blocked by guard shim when first run on a new thread). Warm-up to every
// static init runs on the main thread before the guest touches it.
// Warm-up runs the FULL pipeline that GL TEST has proven to work 100% on
// main thread (init v1.5 -> pbuffer -> context -> makeCurrent -> glClear ->
// GL_RENDERER="ANGLE Metal Renderer: Apple A13 GPU"). Every first-touch ANGLE
// on render guest thread all abort (proof: warm-up getDisplay+init ->
// guest died in eglInitialize; expand -> guest passes, dies at eglMakeCurrent
// now at +0x4f18). Push all first-touch (display, device Metal, command queue,
// context) to the main thread — the render thread just reuses the initialized state.
extern "C" void kudroid_gpu_warmup_egl(void) {
    // Full EGL typedefs not available here (declared later in file) — use local.
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
    auto gl_clear = (void (*)(unsigned int)) GL_FUNC_CACHED("glClear");
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

    // EGL 1.4 constants — standard values, use raw because shim does not include egl.h.
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
    // PASS QUA (proof of real computer log, not guessing): display returns the word
    // eglGetPlatformDisplayEXT(Metal) — even "VALID" — make process ABORT now
    // when using (triangle: crash libtriangle_gles immediately after eglGetDisplay, before
    // also log eglInitialize). And eglGetDisplay(0) fallback RUNS FULLY FINE:
    // eglInitialize v1.5 → pbuffer surface → context → glClear OK →
    // GL_RENDERER="ANGLE Metal Renderer: Apple A13 GPU" → glGetError=0.
    // Both are ANGLE Metal backends — the fallback costs nothing, and is the way
    // Only proven to work. Remove probe, use straight fallback.
    typedef EGLDisplay (*PFN_eglGetDisplay)(EGLNativeDisplayType);
    auto host_get_display = (PFN_eglGetDisplay) get_egl_func("eglGetDisplay");
    if (host_get_display) {
        // Always EGL_DEFAULT_DISPLAY (0) — CAMetalLayer is only used for surfaces.
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
typedef void* VkDevice;

extern "C" PFN_vkVoidFunction bionic_vkGetDeviceProcAddr(VkDevice device, const char* pName);

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

typedef struct VkInstanceCreateInfo {
    uint32_t sType;
    const void* pNext;
    VkFlags flags;
    const void* pApplicationInfo;
    uint32_t enabledLayerCount;
    const char* const* ppEnabledLayerNames;
    uint32_t enabledExtensionCount;
    const char* const* ppEnabledExtensionNames;
} VkInstanceCreateInfo;

#define VK_NULL_HANDLE nullptr

typedef struct VkExtensionProperties {
    char extensionName[256];
    uint32_t specVersion;
} VkExtensionProperties;

#define VK_STRUCTURE_TYPE_IOS_SURFACE_CREATE_INFO_MVK 1000122000
#define VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT 1000217000

typedef struct VkIOSSurfaceCreateInfoMVK {
    uint32_t sType;
    const void* pNext;
    uint32_t flags;
    const void* pView;
} VkIOSSurfaceCreateInfoMVK;

typedef VkResult (*PFN_vkCreateInstance)(const VkInstanceCreateInfo*, const VkAllocationCallbacks*, VkInstance*);
typedef PFN_vkVoidFunction (*PFN_vkGetInstanceProcAddr)(VkInstance, const char*);
typedef VkResult (*PFN_vkEnumerateInstanceExtensionProperties)(const char*, uint32_t*, void*);
typedef VkResult (*PFN_vkCreateIOSSurfaceMVK)(VkInstance, const VkIOSSurfaceCreateInfoMVK*, const VkAllocationCallbacks*, VkSurfaceKHR*);
typedef VkResult (*PFN_vkCreateMetalSurfaceEXT)(VkInstance, const VkMetalSurfaceCreateInfoEXT*, const VkAllocationCallbacks*, VkSurfaceKHR*);

extern "C" VkResult bionic_vkEnumerateInstanceExtensionProperties(const char* pLayerName,
                                                                  uint32_t* pPropertyCount,
                                                                  VkExtensionProperties* pProperties);

static void* get_mvk_handle() {
    static void* mvk = nullptr;
    if (!mvk) {
        mvk = dlopen_bundled_framework("MoltenVK.framework/MoltenVK",
                                       RTLD_NOW | RTLD_GLOBAL);
    }
    return mvk;
}

static PFN_vkGetInstanceProcAddr get_real_vkGetInstanceProcAddr() {
    static PFN_vkGetInstanceProcAddr real_gipa = nullptr;
    if (!real_gipa) {
        void* mvk = get_mvk_handle();
        if (mvk) {
            real_gipa = (PFN_vkGetInstanceProcAddr)::dlsym(mvk, "vkGetInstanceProcAddr");
        }
    }
    return real_gipa;
}

static VkInstance s_activeVkInstance = nullptr;

// One table drives all three resolvers (instance-proc, device-proc, raw dlsym):
// per-name resolve-then-wrap blocks drifted apart, which is how swapchain
// creation stayed invisible for weeks.
typedef uint32_t (*PFN_vkSurfaceCaps_fn)(void*, void*, void*);
typedef uint32_t (*PFN_vkCreateSwapchainKHR_fn)(void*, const void*, const void*, void**);
typedef uint32_t (*PFN_vkQueueSubmit_fn)(void*, uint32_t, const void*, void*);
typedef uint32_t (*PFN_vkQueuePresentKHR_fn)(void*, const void*);
typedef uint32_t (*PFN_vkAcquireNextImageKHR_fn)(void*, void*, uint64_t, void*, void*,
                                                 uint32_t*);
static PFN_vkSurfaceCaps_fn s_realSurfaceCaps = nullptr;
static PFN_vkCreateSwapchainKHR_fn s_realCreateSwapchain = nullptr;
static PFN_vkQueueSubmit_fn s_realQueueSubmit = nullptr;
static PFN_vkQueuePresentKHR_fn s_realQueuePresent = nullptr;
static PFN_vkAcquireNextImageKHR_fn s_realAcquireNextImage = nullptr;
extern "C" uint32_t bionic_vkSurfaceCaps(void* phys, void* surface, void* caps);
extern "C" uint32_t bionic_vkQueueSubmit(void* queue, uint32_t submit_count,
                                         const void* submits, void* fence);
extern "C" uint32_t bionic_vkQueuePresentKHR(void* queue, const void* present_info);
extern "C" uint32_t bionic_vkAcquireNextImageKHR(void* device, void* swapchain,
                                                 uint64_t timeout, void* semaphore,
                                                 void* fence, uint32_t* image_index);
extern "C" uint32_t bionic_vkCreateSwapchainKHR(void* device, const void* create_info,
                                                const void* allocator, void** swapchain);

namespace {
struct VkTapEntry {
    const char* name;
    PFN_vkVoidFunction wrapper;
    void** realSlot;
};
VkTapEntry g_vkTaps[] = {
    {"vkGetPhysicalDeviceSurfaceCapabilitiesKHR",
     reinterpret_cast<PFN_vkVoidFunction>(&bionic_vkSurfaceCaps),
     reinterpret_cast<void**>(&s_realSurfaceCaps)},
    {"vkQueueSubmit", reinterpret_cast<PFN_vkVoidFunction>(&bionic_vkQueueSubmit),
     reinterpret_cast<void**>(&s_realQueueSubmit)},
    {"vkQueuePresentKHR", reinterpret_cast<PFN_vkVoidFunction>(&bionic_vkQueuePresentKHR),
     reinterpret_cast<void**>(&s_realQueuePresent)},
    {"vkAcquireNextImageKHR",
     reinterpret_cast<PFN_vkVoidFunction>(&bionic_vkAcquireNextImageKHR),
     reinterpret_cast<void**>(&s_realAcquireNextImage)},
    {"vkCreateSwapchainKHR",
     reinterpret_cast<PFN_vkVoidFunction>(&bionic_vkCreateSwapchainKHR),
     reinterpret_cast<void**>(&s_realCreateSwapchain)},
};

// Resolve through MoltenVK on demand; null when the game never uses the name.
PFN_vkVoidFunction lookupVkTap(const char* name) {
    if (name == nullptr) return nullptr;
    for (auto& tap : g_vkTaps) {
        if (std::strcmp(name, tap.name) != 0) continue;
        if (*tap.realSlot == nullptr) {
            if (void* mvk = get_mvk_handle()) {
                *tap.realSlot = reinterpret_cast<void*>(::dlsym(mvk, name));
            }
        }
        return *tap.realSlot != nullptr ? tap.wrapper : nullptr;
    }
    return nullptr;
}
}  // namespace
static std::atomic<uint64_t> s_submitCount{0};

// Intercept vkCreateInstance: translate Android surface extension to iOS MoltenVK surface extension
extern "C" VkResult bionic_vkCreateInstance(const VkInstanceCreateInfo* pCreateInfo,
                                            const VkAllocationCallbacks* pAllocator,
                                            VkInstance* pInstance) {
    if (!pCreateInfo || !pInstance) return -3; // VK_ERROR_INITIALIZATION_FAILED
    
    void* mvk = get_mvk_handle();
    PFN_vkCreateInstance real_create = nullptr;
    if (mvk) {
        real_create = (PFN_vkCreateInstance)::dlsym(mvk, "vkCreateInstance");
    }
    if (!real_create) {
        KLOG(kError, "KuDroidGPU", "MoltenVK vkCreateInstance entry point not found in MoltenVK.framework");
        return -3;
    }

    std::vector<const char*> extList;
    if (pCreateInfo->ppEnabledExtensionNames && pCreateInfo->enabledExtensionCount > 0) {
        for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; ++i) {
            const char* ext = pCreateInfo->ppEnabledExtensionNames[i];
            if (strcmp(ext, "VK_KHR_android_surface") == 0) {
                // Translate to MoltenVK iOS surface extensions
                extList.push_back("VK_MVK_ios_surface");
                extList.push_back("VK_EXT_metal_surface");
            } else {
                extList.push_back(ext);
            }
        }
    }

    VkInstanceCreateInfo filteredInfo = *pCreateInfo;
    filteredInfo.enabledExtensionCount = static_cast<uint32_t>(extList.size());
    filteredInfo.ppEnabledExtensionNames = extList.data();

    VkResult res = real_create(&filteredInfo, pAllocator, pInstance);
    if (res == VK_SUCCESS && pInstance) {
        s_activeVkInstance = *pInstance;
    }
    KLOG(kInfo, "KuDroidGPU", "bionic_vkCreateInstance returned %d (instance=%p)", (int)res, (void*)*pInstance);
    return res;
}

#if defined(__APPLE__)
static id kudroid_calayer_self_layer(id self, SEL _cmd) {
    (void)_cmd;
    return self;
}

static void kudroid_ensure_calayer_layer_compat() {
    static bool s_done = false;
    if (!s_done) {
        s_done = true;
        Class clsCALayer = objc_getClass("CALayer");
        if (clsCALayer) {
            SEL selLayer = sel_registerName("layer");
            if (!class_getInstanceMethod(clsCALayer, selLayer)) {
                class_addMethod(clsCALayer, selLayer, (IMP)kudroid_calayer_self_layer, "@@:");
            }
        }
    }
}
#endif

// Translate vkCreateAndroidSurfaceKHR → vkCreateIOSSurfaceMVK / vkCreateMetalSurfaceEXT
extern "C" VkResult bionic_vkCreateAndroidSurfaceKHR(VkInstance instance,
                                                     const VkAndroidSurfaceCreateInfoKHR* pCreateInfo,
                                                     const VkAllocationCallbacks* pAllocator,
                                                     VkSurfaceKHR* pSurface) {
    KLOG(kDebug, "KuDroidGPU", "vkCreateAndroidSurfaceKHR translation");
    if (!pCreateInfo || !pSurface) return -1; // VK_ERROR_INITIALIZATION_FAILED

#if defined(__APPLE__)
    kudroid_ensure_calayer_layer_compat();
#endif

    auto real = get_real_vkGetInstanceProcAddr();
    if (!real) {
        KLOG(kError, "KuDroidGPU", "ERROR: MoltenVK vkGetInstanceProcAddr not found");
        return -1;
    }

    void* actualLayer = g_metalLayer;
    if (pCreateInfo->window && pCreateInfo->window != (void*)1) {
        auto* nw = static_cast<KuDroidNativeWindow*>(pCreateInfo->window);
        if (nw->magic == 0x4B554457 && nw->layer) {
            actualLayer = nw->layer;
        } else {
            actualLayer = pCreateInfo->window;
        }
    }
#if defined(__APPLE__)
    if (!actualLayer) {
        actualLayer = get_or_create_fallback_metal_layer();
    }
    actualLayer = render_layer_for_host(actualLayer, &g_vulkanLayers, "Vulkan");
    if (actualLayer == nullptr) return -3;  // VK_ERROR_INITIALIZATION_FAILED

#endif

    KLOG(kDebug, "KuDroidGPU", "Resolved CAMetalLayer pointer for Vulkan Surface: %p", actualLayer);

    // 1. Try the standard function vkCreateMetalSurfaceEXT first (requires CAMetalLayer)
    auto createMetalSurface = (PFN_vkCreateMetalSurfaceEXT)real(instance, "vkCreateMetalSurfaceEXT");
    if (createMetalSurface) {
        VkMetalSurfaceCreateInfoEXT metalInfo = {};
        metalInfo.sType = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT;
        metalInfo.pNext = nullptr;
        metalInfo.flags = 0;
        metalInfo.pLayer = actualLayer;

        VkResult r = createMetalSurface(instance, &metalInfo, pAllocator, pSurface);
        KLOG(kInfo, "KuDroidGPU", "vkCreateMetalSurfaceEXT returned %d (surface=%p)", (int)r, (void*)*pSurface);
        // Diagnostic: which layer the surface baked; compare with on-screen at present.
        gpuLog("vkCreateSurface layer=%p -> surface=%p r=%d", actualLayer,
               r == VK_SUCCESS ? (void*)*pSurface : nullptr, (int)r);
        if (r == VK_SUCCESS && *pSurface != nullptr) {
            {
                std::lock_guard<std::mutex> lock(g_surfaceLayersMtx);
                s_vulkanLayer = actualLayer;
                for (auto& e : g_surfaceLayers) {
                    if (e.surface == nullptr) {
                        e.surface = *pSurface;
                        e.layer = actualLayer;
                        break;
                    }
                }
            }
            kudroid_ensure_vulkan_layer_attached(actualLayer);
            return r;
        }
    }

    // 2. Try MoltenVK's iOS function: vkCreateIOSSurfaceMVK
    auto createIOSSurface = (PFN_vkCreateIOSSurfaceMVK)real(instance, "vkCreateIOSSurfaceMVK");
    if (createIOSSurface) {
        VkIOSSurfaceCreateInfoMVK iosInfo = {};
        iosInfo.sType = VK_STRUCTURE_TYPE_IOS_SURFACE_CREATE_INFO_MVK;
        iosInfo.pNext = nullptr;
        iosInfo.flags = 0;
        iosInfo.pView = actualLayer;

        VkResult r = createIOSSurface(instance, &iosInfo, pAllocator, pSurface);
        KLOG(kInfo, "KuDroidGPU", "vkCreateIOSSurfaceMVK returned %d (surface=%p)", (int)r, (void*)*pSurface);
        gpuLog("vkCreateSurface layer=%p -> surface=%p r=%d", actualLayer,
               r == VK_SUCCESS ? (void*)*pSurface : nullptr, (int)r);
        if (r == VK_SUCCESS && *pSurface != nullptr) {
            {
                std::lock_guard<std::mutex> lock(g_surfaceLayersMtx);
                s_vulkanLayer = actualLayer;
                for (auto& e : g_surfaceLayers) {
                    if (e.surface == nullptr) {
                        e.surface = *pSurface;
                        e.layer = actualLayer;
                        break;
                    }
                }
            }
            kudroid_ensure_vulkan_layer_attached(actualLayer);
            return r;
        }
    }

    KLOG(kError, "KuDroidGPU", "ERROR: Neither vkCreateMetalSurfaceEXT nor vkCreateIOSSurfaceMVK succeeded");
    return -1;
}

// Intercept vkGetInstanceProcAddr: translate Android surface calls, forward
// everything else to MoltenVK.
extern "C" PFN_vkVoidFunction bionic_vkGetInstanceProcAddr(VkInstance instance, const char* pName) {
    if (!pName) return nullptr;
    if (strcmp(pName, "vkCreateInstance") == 0) {
        return (PFN_vkVoidFunction)&bionic_vkCreateInstance;
    }
    if (strcmp(pName, "vkCreateAndroidSurfaceKHR") == 0) {
        return (PFN_vkVoidFunction)&bionic_vkCreateAndroidSurfaceKHR;
    }
    if (strcmp(pName, "vkEnumerateInstanceExtensionProperties") == 0) {
        return (PFN_vkVoidFunction)&bionic_vkEnumerateInstanceExtensionProperties;
    }
    if (strcmp(pName, "vkGetDeviceProcAddr") == 0) {
        return (PFN_vkVoidFunction)&bionic_vkGetDeviceProcAddr;
    }
    // Capabilities/swapchain/submit resolve here, never via the device-proc tap.
    if (PFN_vkVoidFunction tap = lookupVkTap(pName)) {
        return tap;
    }
    auto real = get_real_vkGetInstanceProcAddr();
    if (real) {
        return real(instance, pName);
    }
    return nullptr;
}


// Present/acquire tap. Signatures use opaque pointers (no Vulkan headers here);
// only the result code and counters are observed, arguments pass straight through.
static std::atomic<uint64_t> s_presentCount{0};
static std::atomic<uint64_t> s_acquireCount{0};

static void log_present_timing(const void* present_info, uint64_t n) {
    if (!present_info || (n > 3 && n % 120 != 0)) return;
    struct Chain {
        uint32_t type;
        const Chain* next;
    };
    struct PresentTime {
        uint32_t id;
        uint64_t desired;
    };
    struct PresentTimes {
        uint32_t type;
        const Chain* next;
        uint32_t count;
        const PresentTime* times;
    };
    static_assert(sizeof(PresentTime) == 16 && sizeof(PresentTimes) == 32);
    uint64_t desired = 0;
    uint32_t id = 0;
    bool timed = false;
    for (auto* next = static_cast<const Chain*>(present_info)->next; next;
         next = next->next) {
        if (next->type != 1000092000u) continue; // VK_STRUCTURE_TYPE_PRESENT_TIMES_INFO_GOOGLE
        const auto* timing = reinterpret_cast<const PresentTimes*>(next);
        if (!timing->count || !timing->times) return;
        desired = timing->times[0].desired;
        id = timing->times[0].id;
        timed = true;
        break;
    }
    const int64_t now = guest_monotonic_ns();
    const double lead_ms = desired ? (static_cast<double>(desired) - now) / 1.0e6 : 0;
    // Keep timing samples visible after the general GPU log budget is exhausted.
    KLOG(kDebug, "KuDroidGPU",
         "vkTiming #%llu timed=%d id=%u desired_ns=%llu now_ns=%lld lead_ms=%.3f suspend_gap_ms=%.3f",
         static_cast<unsigned long long>(n), timed, id,
         static_cast<unsigned long long>(desired), static_cast<long long>(now),
         lead_ms, static_cast<double>(guest_clock_ns(7) - now) / 1.0e6);
}

extern "C" uint32_t bionic_vkQueuePresentKHR(void* queue, const void* present_info) {
    const uint64_t n = ++s_presentCount;
    log_present_timing(present_info, n);
    // Fail loud, never fake SUCCESS: with no real present behind us the frame
    // went nowhere, and logging SUCCESS for a black screen misdirects every
    // later diagnosis. VK_ERROR_INITIALIZATION_FAILED is a valid VkResult the
    // guest's switch handles; 0xFFFFFFFF was not.
    constexpr uint32_t kVkErrorInit = 0xFFFFFFFDu;  // VK_ERROR_INITIALIZATION_FAILED (-3)
    const uint32_t r =
        s_realQueuePresent != nullptr ? s_realQueuePresent(queue, present_info) : kVkErrorInit;
    if (r == 0 || r == 1000001003u) kudroid_note_frame_presented();
    // Log every present: result + target swapchain/image (standard
    // VkPresentInfoKHR 64-bit layout) so a present stream that stops, fails,
    // or switches swapchain mid-run is visible. ~30 lines/s while running.
    void* swapchain = nullptr;
    uint32_t image_index = 0xFFFFFFFFu;
    if (present_info != nullptr) {
        const void* const* words = static_cast<const void* const*>(present_info);
        // sType(0) pNext(8) waitCount(16) pWait(24) swapCount(32) pSwap(40)
        // pImageIdx(48) pResults(56)
        const void* const* p_swap = static_cast<const void* const*>(words[5]);
        const uint32_t* p_idx = static_cast<const uint32_t*>(words[6]);
        if (p_swap != nullptr) swapchain = const_cast<void*>(p_swap[0]);
        if (p_idx != nullptr) image_index = p_idx[0];
    }
    gpuLog("vkQueuePresentKHR #%llu -> %u swap=%p image=%u submits=%llu",
           (unsigned long long)n, r, swapchain, image_index,
           (unsigned long long)s_submitCount.load(std::memory_order_relaxed));
    if (n == 1) {
        void* target = nullptr;
        {
            std::lock_guard<std::mutex> lock(g_surfaceLayersMtx);
            target = s_vulkanLayer ? s_vulkanLayer : g_metalLayer;
        }
        if (target != nullptr) {
            kudroid_ensure_vulkan_layer_attached(target);
        }
    }
    // Diagnostic: on-screen layer identity; a surface bakes its layer at
    // creation, so a mismatch here means frames go to a hidden layer.
    // 1000001003=SUBOPTIMAL 1000001004=OUT_OF_DATE.
    if (n == 1 || r == 1000001003 || r == 1000001004 || (n % 120) == 0) {
        const char* rname = r == 0 ? "SUCCESS" : (r == 1000001003 ? "SUBOPTIMAL"
                                            : (r == 1000001004 ? "OUT_OF_DATE" : "OTHER"));
        gpuLog("vkPresent #%llu %s onscreen_layer=%p window=%dx%d", (unsigned long long)n,
               rname, g_metalLayer, g_metalLayerWidth, g_metalLayerHeight);
    }
    return r;
}

extern "C" uint32_t bionic_vkAcquireNextImageKHR(void* device, void* swapchain, uint64_t timeout,
                                                void* semaphore, void* fence,
                                                uint32_t* image_index) {    const uint64_t n = ++s_acquireCount;
    uint32_t index = 0xFFFFFFFFu;
    // Valid VkResults only: 0xFFFFFFFF matches no VK_ERROR_* (small negatives)
    // and falls through guest switches into retry-forever. -3 init-failed.
    uint32_t r = 0xFFFFFFFDu;
    if (s_realAcquireNextImage != nullptr) {
        r = s_realAcquireNextImage(device, swapchain, timeout, semaphore, fence, &index);
        if (image_index != nullptr) *image_index = index;
    }
    if (n <= 5 || r != 0 || (n % 120) == 0) {
        gpuLog("vkAcquireNextImageKHR #%llu -> %u image=%u", (unsigned long long)n, r, index);
    }
    return r;
}

// Diagnostic: submit count proves whether Unity draws (submit>0) or only
// clear-presents (submit==0). Command-buffer counts are left to validation
// layers; only the batch count is observed here.
extern "C" uint32_t bionic_vkQueueSubmit(void* queue, uint32_t submit_count,
                                         const void* submits, void* fence) {
    const uint64_t n =
        s_submitCount.fetch_add(submit_count, std::memory_order_relaxed) + submit_count;
    const uint32_t r = s_realQueueSubmit != nullptr
                           ? s_realQueueSubmit(queue, submit_count, submits, fence)
                           : 0xFFFFFFFDu;  // valid VkResult, see above
    if (n <= 5 || r != 0 || (n % 120) == 0) {
        gpuLog("vkQueueSubmit #%llu -> %u batches=%u", (unsigned long long)n, r,
               submit_count);
    }
    return r;
}

extern "C" uint32_t bionic_vkCreateSwapchainKHR(void* device, const void* create_info,
                                               const void* allocator, void** swapchain) {
    // Diagnostic: full swapchain geometry (offsets for 64-bit
    // VkSwapchainCreateInfoKHR). Portrait extent on a landscape game, or
    // preTransform != IDENTITY, is the orientation-split signature.
    void* surface = nullptr;
    uint32_t min_count = 0, format = 0, extent_w = 0, extent_h = 0;
    int32_t pre_transform = -1, present_mode = -1;
    if (create_info != nullptr) {
        const char* ci = static_cast<const char*>(create_info);
        surface = *(void* const*)(ci + 24);
        std::memcpy(&min_count, ci + 32, 4);
        std::memcpy(&format, ci + 36, 4);
        std::memcpy(&extent_w, ci + 44, 4);
        std::memcpy(&extent_h, ci + 48, 4);
        std::memcpy(&pre_transform, ci + 80, 4);
        std::memcpy(&present_mode, ci + 88, 4);
    }
    void* created = nullptr;
    const uint32_t r = s_realCreateSwapchain != nullptr
                           ? s_realCreateSwapchain(device, create_info, allocator, &created)
                           : 0xFFFFFFFDu;  // valid VkResult, see above
    if (swapchain != nullptr) *swapchain = created;
    // The swapchain's layer now owns the pixels: static canvas contents must
    // stop covering its drawables (one producer per layer).
    if (r == 0) {
        void* target = nullptr;
        {
            std::lock_guard<std::mutex> lock(g_surfaceLayersMtx);
            if (surface != nullptr) {
                for (const auto& e : g_surfaceLayers) {
                    if (e.surface == surface && e.layer != nullptr) {
                        s_vulkanLayer = e.layer;
                        break;
                    }
                }
            }
            target = s_vulkanLayer ? s_vulkanLayer : g_metalLayer;
        }
        if (target != nullptr) {
            kudroid_ensure_vulkan_layer_attached(target);
        }
    }
    gpuLog("vkCreateSwapchainKHR surface=%p -> %u swap=%p extent=%ux%u fmt=%u preXform=%d mode=%d minImg=%u onscreen=%p",
           surface, r, created, extent_w, extent_h, format, pre_transform, present_mode,
           min_count, g_metalLayer);
    return r;
}

// Diagnostic: surface capabilities MoltenVK reports. Unity reads these through
// vkGetInstanceProcAddr (bypassing the device-proc tap), so this hook lives on
// the instance-proc path. VkSurfaceCapabilitiesKHR 64-bit layout:
// minCount(0) maxCount(4) currentExtent(8: w,h) supportedTransforms(36)
// currentTransform(40).
extern "C" uint32_t bionic_vkSurfaceCaps(void* phys, void* surface, void* caps) {
    const uint32_t r =
        s_realSurfaceCaps != nullptr ? s_realSurfaceCaps(phys, surface, caps) : 0xFFFFFFFDu;
    if (r == 0 && caps != nullptr) {
        uint32_t w = 0, h = 0, min_c = 0, max_c = 0;
        int32_t sup_xf = -1, cur_xf = -1;
        char* c = static_cast<char*>(caps);
        std::memcpy(&min_c, c + 0, 4);
        std::memcpy(&max_c, c + 4, 4);
        std::memcpy(&w, c + 8, 4);
        std::memcpy(&h, c + 12, 4);
        std::memcpy(&sup_xf, c + 36, 4);
        std::memcpy(&cur_xf, c + 40, 4);
        if (w == 0 || h == 0) {
            w = g_metalLayerWidth > 0 ? static_cast<uint32_t>(g_metalLayerWidth) : 1080u;
            h = g_metalLayerHeight > 0 ? static_cast<uint32_t>(g_metalLayerHeight) : 1920u;
            std::memcpy(c + 8, &w, 4);
            std::memcpy(c + 12, &h, 4);
            uint32_t max_w = 0, max_h = 0;
            std::memcpy(&max_w, c + 24, 4);
            std::memcpy(&max_h, c + 28, 4);
            if (max_w < w) std::memcpy(c + 24, &w, 4);
            if (max_h < h) std::memcpy(c + 28, &h, 4);
            gpuLog("vkSurfaceCaps clamped zero extent to %ux%u", w, h);
        }
        gpuLog("vkSurfaceCaps surface=%p extent=%ux%u curXform=%d supXform=%d min=%u max=%u",
               surface, w, h, cur_xf, sup_xf, min_c, max_c);
    }
    return r;
}

// Run-end reset: present/acquire numbering restarts so the next run's log
// starts at #1 instead of continuing the previous run's sequence.
extern "C" void kudroid_gpu_note_run_end(void) {
    g_materialAuditCount.store(0, std::memory_order_relaxed);
    g_materialAuditEpoch.fetch_add(1, std::memory_order_relaxed);
    s_presentCount.store(0, std::memory_order_relaxed);
    s_acquireCount.store(0, std::memory_order_relaxed);
    std::vector<void*> vkHosts;
    std::vector<void*> glHosts;
    {
        std::lock_guard<std::mutex> lock(g_surfaceLayersMtx);
        s_vulkanLayer = nullptr;
        for (auto& e : g_surfaceLayers) {
            e.surface = nullptr;
            e.layer = nullptr;
        }
        // The run's EGL surfaces are gone; stale entries would keep a layer "owned".
        g_eglLayers.clear();
        for (const auto& entry : g_glLayers) glHosts.push_back(entry.first);
        for (const auto& entry : g_vulkanLayers) vkHosts.push_back(entry.first);
    }
    for (void* host : glHosts) drop_gl_layer_for_host(host);
#if defined(__APPLE__)
    for (void* host : vkHosts) drop_render_layer_for_host(host, &g_vulkanLayers);
#endif
}

extern "C" PFN_vkVoidFunction bionic_vkGetDeviceProcAddr(VkDevice device, const char* pName) {
    if (!pName) return nullptr;
    // Diagnostic taps; pure passthrough, only for logs.
    if (PFN_vkVoidFunction tap = lookupVkTap(pName)) {
        return tap;
    }
    void* mvk = get_mvk_handle();
    if (mvk) {
        typedef PFN_vkVoidFunction (*PFN_vkGDPA)(VkDevice, const char*);
        auto gdpa = (PFN_vkGDPA)::dlsym(mvk, "vkGetDeviceProcAddr");
        if (gdpa) {
            PFN_vkVoidFunction f = gdpa(device, pName);
            if (f) return f;
        }
    }
    auto real = get_real_vkGetInstanceProcAddr();
    if (real) {
        return real(nullptr, pName);
    }
    return nullptr;
}

// Intercept vkEnumerateInstanceExtensionProperties: inject
// VK_KHR_android_surface and mask VK_EXT_metal_surface so the game sees the
// Android surface extension it expects.
typedef VkResult (*PFN_vkEnumerateInstanceExtensionProperties)(const char*, uint32_t*, void*);

extern "C" VkResult bionic_vkEnumerateInstanceExtensionProperties(const char* pLayerName,
                                                                  uint32_t* pPropertyCount,
                                                                  VkExtensionProperties* pProperties) {
    void* mvk = get_mvk_handle();
    PFN_vkEnumerateInstanceExtensionProperties real = nullptr;
    if (mvk) {
        real = (PFN_vkEnumerateInstanceExtensionProperties)::dlsym(mvk, "vkEnumerateInstanceExtensionProperties");
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
            memcpy(&pProperties[out], &tmp[i], sizeof(VkExtensionProperties));
            pProperties[out].extensionName[255] = '\0';
            out++;
        }
        free(tmp);
    }

    // Inject VK_KHR_android_surface (only if space is available).
    if (out < capacity) {
        memset(&pProperties[out], 0, sizeof(VkExtensionProperties));
        strncpy(pProperties[out].extensionName, "VK_KHR_android_surface", sizeof(pProperties[out].extensionName) - 1);
        pProperties[out].extensionName[sizeof(pProperties[out].extensionName) - 1] = '\0';
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
    if (strcmp(name, "vkGetDeviceProcAddr") == 0) {
        return (void*)&bionic_vkGetDeviceProcAddr;
    }
    if (strcmp(name, "vkCreateInstance") == 0) {
        return (void*)&bionic_vkCreateInstance;
    }
    if (strcmp(name, "vkCreateAndroidSurfaceKHR") == 0) {
        return (void*)&bionic_vkCreateAndroidSurfaceKHR;
    }
    if (strcmp(name, "vkEnumerateInstanceExtensionProperties") == 0) {
        return (void*)&bionic_vkEnumerateInstanceExtensionProperties;
    }
    // Submit/acquire/present/swapchain resolve through raw dlsym as well as the
    // proc-addr hooks; tap them on every path or Unity's route stays invisible.
    // Resolve the real function on demand so this path works even when the game
    // never queries the proc-addr hooks for these names.
    if (strcmp(name, "vkQueueSubmit") == 0 || strcmp(name, "vkQueuePresentKHR") == 0 ||
        strcmp(name, "vkAcquireNextImageKHR") == 0 || strcmp(name, "vkCreateSwapchainKHR") == 0) {
        void* mvk = get_mvk_handle();
        if (strcmp(name, "vkQueueSubmit") == 0) {
            if (s_realQueueSubmit == nullptr && mvk) {
                s_realQueueSubmit = reinterpret_cast<PFN_vkQueueSubmit_fn>(
                    ::dlsym(mvk, "vkQueueSubmit"));
            }
            if (s_realQueueSubmit != nullptr) return (void*)&bionic_vkQueueSubmit;
        } else if (strcmp(name, "vkQueuePresentKHR") == 0) {
            if (s_realQueuePresent == nullptr && mvk) {
                s_realQueuePresent = reinterpret_cast<PFN_vkQueuePresentKHR_fn>(
                    ::dlsym(mvk, "vkQueuePresentKHR"));
            }
            if (s_realQueuePresent != nullptr) return (void*)&bionic_vkQueuePresentKHR;
        } else if (strcmp(name, "vkAcquireNextImageKHR") == 0) {
            if (s_realAcquireNextImage == nullptr && mvk) {
                s_realAcquireNextImage = reinterpret_cast<PFN_vkAcquireNextImageKHR_fn>(
                    ::dlsym(mvk, "vkAcquireNextImageKHR"));
            }
            if (s_realAcquireNextImage != nullptr) {
                return (void*)&bionic_vkAcquireNextImageKHR;
            }
        } else {
            if (s_realCreateSwapchain == nullptr && mvk) {
                s_realCreateSwapchain = reinterpret_cast<PFN_vkCreateSwapchainKHR_fn>(
                    ::dlsym(mvk, "vkCreateSwapchainKHR"));
            }
            if (s_realCreateSwapchain != nullptr) {
                return (void*)&bionic_vkCreateSwapchainKHR;
            }
        }
    }
    void* mvk = get_mvk_handle();
    if (mvk) {
        void* f = ::dlsym(mvk, name);
        if (f) return f;
    }
    auto real = get_real_vkGetInstanceProcAddr();
    if (real) {
        if (s_activeVkInstance) {
            void* f = (void*)real(s_activeVkInstance, name);
            if (f) return f;
        }
        void* f = (void*)real(nullptr, name);
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
// IMPORTANT: must export FULL EGL entry points, not just 4-5 functions. ELF
// loader resolve import of game via BionicShim (shim table + RTLD_DEFAULT) —
// ANGLE load RTLD_LOCAL should not be in RTLD_DEFAULT. Previously 17 functions
// egl* remaining (eglInitialize, eglChooseConfig, eglCreateContext, eglMakeCurrent,
// eglSwapBuffers...) falls into dummy returning 0 → Unity receives eglCreateContext=NULL →
// crash NULL+0x50. Now each function forwards directly to ANGLE via get_egl_func().
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
typedef EGLBoolean (*PFN_eglDestroySurface)(EGLDisplay, EGLSurface);
static EGLSurface s_activeEglSurface = nullptr;
static EGLDisplay s_activeEglDisplay = nullptr;

// Frame census: draws/clears were invisible (silent forwards), so per-frame
// activity could not be told apart from an idle swap loop. Counters only;
// summary printed from eglSwapBuffers every 120 swaps.
static uint64_t g_drawCalls = 0;
static uint64_t g_clearCalls = 0;
static uint64_t g_swapCalls = 0;

extern "C" EGLSurface bionic_eglCreateWindowSurface(EGLDisplay dpy, EGLConfig config,
                                                    EGLNativeWindowType win,
                                                    const EGLint* attrib_list) {
    gpuLog("eglCreateWindowSurface: window=%p", (void*)win);
    auto host_func = (PFN_eglCreateWindowSurface)get_egl_func("eglCreateWindowSurface");
    auto host_destroy = (PFN_eglDestroySurface)get_egl_func("eglDestroySurface");
    if (host_func) {
        void* resolvedLayer = g_metalLayer;
        if (win) {
            auto* nw = static_cast<KuDroidNativeWindow*>(win);
            if (nw->magic == 0x4B554457 && nw->layer) {
                resolvedLayer = nw->layer;
            }
        }
#if defined(__APPLE__)
        if (!resolvedLayer) {
            resolvedLayer = get_or_create_fallback_metal_layer();
        }
#endif
        // ANGLE presents into a sublayer of its own; resolvedLayer stays the host
        // that ownership and canvas decisions are keyed on.
        EGLNativeWindowType nativeWin = (EGLNativeWindowType)gl_layer_for_host(resolvedLayer);
        gpuLog("eglCreateWindowSurface: calling ANGLE with layer=%p (host=%p)...", (void*)nativeWin, resolvedLayer);
#if defined(__APPLE__)
        void* pool = objc_autoreleasePoolPush();
        EGLSurface s = host_func(dpy, config, nativeWin, attrib_list);
        if (!s && s_activeEglSurface && host_destroy) {
            gpuLog("eglCreateWindowSurface failed, auto-recycling stale EGLSurface %p on layer %p...", s_activeEglSurface, (void*)nativeWin);
            host_destroy(s_activeEglDisplay ? s_activeEglDisplay : dpy, s_activeEglSurface);
            unregister_egl_layer(s_activeEglSurface);
            s_activeEglSurface = nullptr;
            nativeWin = (EGLNativeWindowType)gl_layer_for_host(resolvedLayer);  // may have been dropped
            s = host_func(dpy, config, nativeWin, attrib_list);
        }
        objc_autoreleasePoolPop(pool);
        gpuLog("eglCreateWindowSurface returned %p", (void*)s);
        if (s) {
            register_egl_layer(s, resolvedLayer);
            s_activeEglSurface = s;
            s_activeEglDisplay = dpy;
        }
        return s;
#else
        EGLSurface s = host_func(dpy, config, nativeWin, attrib_list);
        if (!s && s_activeEglSurface && host_destroy) {
            gpuLog("eglCreateWindowSurface failed, auto-recycling stale EGLSurface %p on layer %p...", s_activeEglSurface, (void*)nativeWin);
            host_destroy(s_activeEglDisplay ? s_activeEglDisplay : dpy, s_activeEglSurface);
            unregister_egl_layer(s_activeEglSurface);
            s_activeEglSurface = nullptr;
            nativeWin = (EGLNativeWindowType)gl_layer_for_host(resolvedLayer);
            s = host_func(dpy, config, nativeWin, attrib_list);
        }
        gpuLog("eglCreateWindowSurface returned %p", (void*)s);
        if (s) {
            register_egl_layer(s, resolvedLayer);
            s_activeEglSurface = s;
            s_activeEglDisplay = dpy;
        }
        return s;
#endif
    }
    gpuLog("eglCreateWindowSurface: not found in host");
    return nullptr;
}

extern "C" void kudroid_gpu_cleanup_on_test_exit(void) {
    typedef EGLBoolean (*PFN_eglMakeCurrent)(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
    typedef EGLBoolean (*PFN_eglDestroySurface)(EGLDisplay, EGLSurface);
    auto fnMakeCurrent = (PFN_eglMakeCurrent)get_egl_func("eglMakeCurrent");
    auto fnDestroySurface = (PFN_eglDestroySurface)get_egl_func("eglDestroySurface");
    if (fnMakeCurrent) {
        fnMakeCurrent(s_activeEglDisplay ? s_activeEglDisplay : (EGLDisplay)1, nullptr, nullptr, nullptr);
    }
    if (s_activeEglSurface && fnDestroySurface) {
        gpuLog("kudroid_gpu_cleanup_on_test_exit: destroying stale surface %p", s_activeEglSurface);
        fnDestroySurface(s_activeEglDisplay ? s_activeEglDisplay : (EGLDisplay)1, s_activeEglSurface);
        unregister_egl_layer(s_activeEglSurface);
        s_activeEglSurface = nullptr;
        s_activeEglDisplay = nullptr;
    }
#if defined(__APPLE__)
    if (tls_autorelease_pool) {
        objc_autoreleasePoolPop(tls_autorelease_pool);
        tls_autorelease_pool = nullptr;
    }
#endif
    gpuLog("kudroid_gpu_cleanup_on_test_exit: GPU state cleaned up successfully");
}

extern "C" bool kudroid_gpu_has_active_surface(void) {
    std::lock_guard<std::mutex> lock(g_nativeWindowsMutex);
    return s_activeEglSurface != nullptr || !g_nativeWindows.empty();
}

// ── EGL 1.x entry points missing — forward straight to ANGLE ────────────────

#define EGL_FORWARD_ERR(name, what) gpuLog("%s: ANGLE %s not available", name, what)

// Collect function get part ANGLE + check NULL: return function pointer with correct signature or
// nullptr (caller automatically logs errors). Remove the PFN typedef boilerplate from each function.
template <typename Signature>
Signature* eglFn(const char* name) {
    return reinterpret_cast<Signature*>(get_egl_func(name));
}

extern "C" EGLBoolean bionic_eglInitialize(EGLDisplay dpy, EGLint* major, EGLint* minor) {
    auto f = eglFn<EGLBoolean(EGLDisplay, EGLint*, EGLint*)>("eglInitialize");
    if (!f) { EGL_FORWARD_ERR("eglInitialize", ""); return EGL_FALSE; }
    // Guest (e.g. TriangleGLES calls eglInitialize(display, 0, 0)) can pass
    // NULL out-params — EGL spec allows, but some ANGLE versions dereference
    // them → abort. Forward with local buffer then copy the result back.
    EGLint localMajor = 0, localMinor = 0;
    gpuLog("eglInitialize: calling ANGLE eglInitialize(dpy=%p, major=%s, minor=%s)...",
           (void*)dpy, major ? "ptr" : "NULL", minor ? "ptr" : "NULL");
    kudroid_boot_mark("eglInitialize-enter");
    const EGLBoolean r = f(dpy, major ? major : &localMajor, minor ? minor : &localMinor);
    kudroid_boot_mark("eglInitialize-exit");
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

static std::mutex g_glDiagnosticsMutex;
static std::unordered_set<void*> g_glDiagnosticContexts;

static void gl_diagnostic_message(unsigned int source, unsigned int type, unsigned int id,
                                  unsigned int severity, int length, const char* message,
                                  const void* context) {
    if (!message || length <= 0 || (type != 0x824C && type != 0x824E && severity != 0x9146)) return;
    static std::mutex mutex;
    static std::unordered_map<size_t, unsigned> seen;
    const std::string text(message, static_cast<size_t>(std::min(length, 768)));
    {
        std::lock_guard<std::mutex> lock(mutex);
        const size_t hash = std::hash<std::string>{}(text);
        if (seen.size() >= 128 && !seen.count(hash)) return;
        if (++seen[hash] > 4) return;
    }
    KLOG(kError, "KuDroidGPU", "GL driver ctx=%p source=0x%x type=0x%x id=%u severity=0x%x: %s",
         context, source, type, id, severity, text.c_str());
}

static void install_gl_diagnostics(void* context) {
    {
        std::lock_guard<std::mutex> lock(g_glDiagnosticsMutex);
        if (!g_glDiagnosticContexts.insert(context).second) return;
    }
    using GetString = const char* (*)(unsigned int);
    auto strings = reinterpret_cast<GetString>(GL_FUNC_CACHED("glGetString"));
    if (!strings) return;
    const char* version = strings(0x1F02);
    const char* renderer = strings(0x1F01);
    const char* language = strings(0x8B8C);
    KLOG(kInfo, "KuDroidGPU", "GL context=%p version=%s renderer=%s shading=%s", context,
         version ? version : "", renderer ? renderer : "", language ? language : "");
    const char* extensions = strings(0x1F03);
    if (!extensions || !std::strstr(extensions, "GL_KHR_debug")) return;
    using Callback = void (*)(unsigned int, unsigned int, unsigned int, unsigned int, int, const char*, const void*);
    using SetCallback = void (*)(Callback, const void*);
    using GetPointer = void (*)(unsigned int, void**);
    using Enable = void (*)(unsigned int);
    auto callback = reinterpret_cast<SetCallback>(GL_FUNC_CACHED("glDebugMessageCallbackKHR"));
    if (!callback) callback = reinterpret_cast<SetCallback>(GL_FUNC_CACHED("glDebugMessageCallback"));
    auto pointer = reinterpret_cast<GetPointer>(GL_FUNC_CACHED("glGetPointervKHR"));
    if (!pointer) pointer = reinterpret_cast<GetPointer>(GL_FUNC_CACHED("glGetPointerv"));
    auto enable = reinterpret_cast<Enable>(GL_FUNC_CACHED("glEnable"));
    if (!callback || !pointer || !enable) return;
    void* existing = nullptr;
    pointer(0x8244 /* GL_DEBUG_CALLBACK_FUNCTION */, &existing);
    if (existing) return;
    callback(gl_diagnostic_message, context);
    enable(0x92E0 /* GL_DEBUG_OUTPUT */);
}

extern "C" EGLBoolean bionic_eglDestroyContext(EGLDisplay dpy, EGLContext ctx) {
    auto f = eglFn<EGLBoolean(EGLDisplay, EGLContext)>("eglDestroyContext");
    if (!f) { EGL_FORWARD_ERR("eglDestroyContext", ""); return EGL_FALSE; }
    EGLBoolean r = f(dpy, ctx);
    if (r) {
        surface_texture::context_destroyed(ctx);
        std::lock_guard<std::mutex> lock(g_glDiagnosticsMutex);
        g_glDiagnosticContexts.erase(ctx);
    }
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
    if (r) unregister_egl_layer(surface);
    if (r && surface == s_activeEglSurface) {
        s_activeEglSurface = nullptr;
        s_activeEglDisplay = nullptr;
    }
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
    if (r) surface_texture::context_changed();
    if (r && ctx) install_gl_diagnostics(ctx);
    return r;
}

extern "C" EGLContext bionic_eglGetCurrentContext(void) {
    auto f = eglFn<EGLContext(void)>("eglGetCurrentContext");
    if (!f) { EGL_FORWARD_ERR("eglGetCurrentContext", ""); return EGL_NO_CONTEXT; }
    return f();
}

extern "C" EGLSurface bionic_eglGetCurrentSurface(EGLint readdraw) {
    auto f = eglFn<EGLSurface(EGLint)>("eglGetCurrentSurface");
    if (!f) { EGL_FORWARD_ERR("eglGetCurrentSurface", ""); return EGL_NO_SURFACE; }
    return f(readdraw);
}

extern "C" EGLDisplay bionic_eglGetCurrentDisplay(void) {
    auto f = eglFn<EGLDisplay(void)>("eglGetCurrentDisplay");
    if (!f) { EGL_FORWARD_ERR("eglGetCurrentDisplay", ""); return EGL_NO_DISPLAY; }
    return f();
}

// KUDROID_GPU_DIAG: separates "GL drew black" from "the compositor hid the frame".
// Samples the bound framebuffer before present and reports texture upload contents.
// Off by default; glGetError here consumes the guest's pending error flag.
static bool gpu_diag_enabled() {
    static const bool enabled = [] {
        const char* v = std::getenv("KUDROID_GPU_DIAG");
        return v && *v && *v != '0';
    }();
    return enabled;
}

// Writes the whole bound framebuffer as a binary PPM (rows flipped upright) to the log
// directory, so what GL actually rendered can be inspected directly.
static void gpu_diag_dump_framebuffer(void* readPixelsFn, int w, int h, uint64_t swapIndex) {
    const char* dump = std::getenv("KUDROID_GPU_DUMP_FRAMES");
    if (!dump || !*dump || *dump == '0') return;
    typedef void (*PFN_ReadPixels)(int, int, int, int, unsigned int, unsigned int, void*);
    auto readPixels = reinterpret_cast<PFN_ReadPixels>(readPixelsFn);
    if (!readPixels || w <= 0 || h <= 0 || w > 8192 || h > 8192) return;
    const char* dir = std::getenv("KUDROID_GPU_DIAG_DIR");
    if ((!dir || !*dir) && &g_kudroid_log_dir_ptr && g_kudroid_log_dir_ptr) dir = g_kudroid_log_dir_ptr;
    char path[1024];
    std::snprintf(path, sizeof(path), "%s/fb_swap%llu.ppm", dir && *dir ? dir : ".",
                  static_cast<unsigned long long>(swapIndex));
    std::vector<unsigned char> rgba(static_cast<size_t>(w) * h * 4);
    readPixels(0, 0, w, h, 0x1908 /* GL_RGBA */, 0x1401 /* GL_UNSIGNED_BYTE */, rgba.data());
    std::FILE* f = std::fopen(path, "wb");
    if (!f) return;
    std::fprintf(f, "P6\n%d %d\n255\n", w, h);
    std::vector<unsigned char> row(static_cast<size_t>(w) * 3);
    for (int y = h - 1; y >= 0; --y) {
        const unsigned char* src = rgba.data() + static_cast<size_t>(y) * w * 4;
        for (int x = 0; x < w; ++x) {
            row[x * 3] = src[x * 4];
            row[x * 3 + 1] = src[x * 4 + 1];
            row[x * 3 + 2] = src[x * 4 + 2];
        }
        std::fwrite(row.data(), 1, row.size(), f);
    }
    std::fclose(f);
    ::kudroid::log::write(::kudroid::log::kInfo, "KuDroidGPU", "diag framebuffer dumped to %s", path);
}

static void gpu_diag_sample_backbuffer(EGLDisplay dpy, EGLSurface surface, uint64_t swapIndex) {
    typedef void (*PFN_ReadPixels)(int, int, int, int, unsigned int, unsigned int, void*);
    typedef unsigned int (*PFN_GetError)(void);
    typedef void (*PFN_GetIntegerv)(unsigned int, int*);
    auto readPixels = (PFN_ReadPixels)GL_FUNC_CACHED("glReadPixels");
    auto getError = (PFN_GetError)GL_FUNC_CACHED("glGetError");
    auto getIntegerv = (PFN_GetIntegerv)GL_FUNC_CACHED("glGetIntegerv");
    auto query = eglFn<EGLBoolean(EGLDisplay, EGLSurface, EGLint, EGLint*)>("eglQuerySurface");
    if (!readPixels || !getError || !query) return;
    EGLint w = 0, h = 0;
    query(dpy, surface, 0x3057 /* EGL_WIDTH */, &w);
    query(dpy, surface, 0x3056 /* EGL_HEIGHT */, &h);
    if (w <= 0 || h <= 0) return;
    int fbo = -1;
    if (getIntegerv) getIntegerv(0x8CA6 /* GL_FRAMEBUFFER_BINDING */, &fbo);
    const int pts[5][2] = {{w / 2, h / 2}, {w / 4, h / 4}, {3 * w / 4, h / 4},
                           {w / 4, 3 * h / 4}, {3 * w / 4, 3 * h / 4}};
    unsigned char px[5][4] = {};
    for (int i = 0; i < 5; ++i) {
        readPixels(pts[i][0], pts[i][1], 1, 1, 0x1908 /* GL_RGBA */, 0x1401 /* GL_UNSIGNED_BYTE */, px[i]);
    }
    if (swapIndex == 120 || swapIndex == 480) {
        gpu_diag_dump_framebuffer(reinterpret_cast<void*>(readPixels), w, h, swapIndex);
    }
    const unsigned int err = getError();
    ::kudroid::log::write(::kudroid::log::kInfo, "KuDroidGPU",
        "diag swap#%llu fbo=%d surface=%dx%d center=%u,%u,%u,%u c1=%u,%u,%u,%u c2=%u,%u,%u,%u "
        "c3=%u,%u,%u,%u c4=%u,%u,%u,%u glError=0x%x",
        static_cast<unsigned long long>(swapIndex), fbo, w, h,
        px[0][0], px[0][1], px[0][2], px[0][3], px[1][0], px[1][1], px[1][2], px[1][3],
        px[2][0], px[2][1], px[2][2], px[2][3], px[3][0], px[3][1], px[3][2], px[3][3],
        px[4][0], px[4][1], px[4][2], px[4][3], err);
}

// Logs the first uploads with a non-zero byte count, so empty textures are visible.
static void gpu_diag_texture_upload(const char* what, unsigned int target, int level, int width,
                                    int height, unsigned int format, unsigned int type,
                                    const void* pixels) {
    static std::atomic<int> count{0};
    if (count.fetch_add(1, std::memory_order_relaxed) >= 128) return;
    size_t bpp = 4;
    if (type == 0x8363 || type == 0x8033 || type == 0x8034) bpp = 2;  // packed 16-bit
    else if (format == 0x1907) bpp = 3;                               // GL_RGB
    else if (format == 0x190A) bpp = 2;                               // GL_LUMINANCE_ALPHA
    else if (format == 0x1906 || format == 0x1909) bpp = 1;           // GL_ALPHA / GL_LUMINANCE
    size_t nonzero = 0;
    const size_t total = pixels && width > 0 && height > 0
        ? static_cast<size_t>(width) * static_cast<size_t>(height) * bpp : 0;
    const size_t scan = std::min<size_t>(total, 1u << 20);
    const auto* bytes = static_cast<const unsigned char*>(pixels);
    for (size_t i = 0; i < scan; ++i) nonzero += bytes[i] != 0;
    ::kudroid::log::write(::kudroid::log::kInfo, "KuDroidGPU",
        "diag %s target=0x%x level=%d %dx%d format=0x%x type=0x%x pixels=%s nonzero=%zu/%zu",
        what, target, level, width, height, format, type, pixels ? "yes" : "null", nonzero, scan);
}

extern "C" EGLBoolean bionic_eglSwapBuffers(EGLDisplay dpy, EGLSurface surface) {
    auto f = eglFn<EGLBoolean(EGLDisplay, EGLSurface)>("eglSwapBuffers");
    if (!f) { EGL_FORWARD_ERR("eglSwapBuffers", ""); return EGL_FALSE; }
    // The presenting thread is a render thread for fault isolation: losing it
    // loses the picture, so a fault on it is app-fatal. Cheap: one atomic
    // load per present once the small set is full.
    kudroid_note_render_thread();
#if defined(__APPLE__)
    if (tls_autorelease_pool) {
        objc_autoreleasePoolPop(tls_autorelease_pool);
        tls_autorelease_pool = nullptr;
    }
    tls_autorelease_pool = objc_autoreleasePoolPush();
#endif
    if (gpu_diag_enabled() && (g_swapCalls % 120) == 0) {
        gpu_diag_sample_backbuffer(dpy, surface, g_swapCalls);
    }
    const uint64_t swapT0 = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
    EGLBoolean r = f(dpy, surface);
    if (r) kudroid_note_frame_presented();
    const uint64_t swapMs = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count()) -
        swapT0;
    gpuLog("eglSwapBuffers(surface=%p) -> %s", (void*)surface, r ? "true" : "false");
    {
        // First present is the end of boot: everything before it is startup.
        static std::atomic<bool> s_first{false};
        if (!s_first.exchange(true, std::memory_order_relaxed)) {
            kudroid_boot_mark("first-swap");
        }
    }
    // Slow-swap detector: a present path that blocks (drawable starvation,
    // vsync wedge) shows up here, not in Unity timings. Rare when healthy.
    if (swapMs > 50) {
        gpuLog("eglSwapBuffers slow: %llums", (unsigned long long)swapMs);
    }
    if ((++g_swapCalls % 120) == 0) {
        gpuLog("frame census: swaps=%llu draws=%llu clears=%llu",
               (unsigned long long)g_swapCalls, (unsigned long long)g_drawCalls,
               (unsigned long long)g_clearCalls);
    }
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
    // No backend: report NOT_INITIALIZED, not SUCCESS — a SUCCESS here masks
    // every earlier failure and sends the guest down the success path blind.
    if (!f) { EGL_FORWARD_ERR("eglGetError", ""); return 0x3001; }
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
    // Same as EXT variant: native display must be 0 (EGL_DEFAULT_DISPLAY),
    // CAMetalLayer is only used for eglCreateWindowSurface.
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

// ── The EGL/GLES function was once ELF loader bind dummy (Discord log: "missing symbol
// bound to dummy: ...") because missing in table shim + ANGLE load RTLD_LOCAL.
// Real forward to ANGLE — dummy returns 0 causing the game to receive the wrong value → crash. ──

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

struct KuDroidGLSurfaceState {
    EGLDisplay dpy = nullptr;
    EGLConfig config = nullptr;
    EGLContext ctx = nullptr;
    EGLSurface surf = nullptr;
    KuDroidNativeWindow* nw = nullptr;
};

extern "C" int64_t bionic_kudroid_gl_surface_create(int32_t client_version, int32_t depth_size, int32_t stencil_size) {
    EGLDisplay dpy = bionic_eglGetDisplay((EGLNativeDisplayType)0);
    if (!dpy) {
        gpuLog("kudroid_gl_surface: eglGetDisplay failed");
        return 0;
    }
    EGLint major = 0, minor = 0;
    if (!bionic_eglInitialize(dpy, &major, &minor)) {
        gpuLog("kudroid_gl_surface: eglInitialize failed");
        return 0;
    }
    bionic_eglBindAPI(0x30A0 /* EGL_OPENGL_ES_API */);

    EGLint renderable = (client_version >= 3) ? 0x40 /* EGL_OPENGL_ES3_BIT */ : 0x4 /* EGL_OPENGL_ES2_BIT */;
    const EGLint attribs[] = {
        0x3040 /* EGL_RENDERABLE_TYPE */, renderable,
        0x3033 /* EGL_SURFACE_TYPE */, 0x4 /* EGL_WINDOW_BIT */,
        0x3024 /* EGL_RED_SIZE */, 8,
        0x3023 /* EGL_GREEN_SIZE */, 8,
        0x3022 /* EGL_BLUE_SIZE */, 8,
        0x3021 /* EGL_ALPHA_SIZE */, 8,
        0x3025 /* EGL_DEPTH_SIZE */, depth_size > 0 ? depth_size : 16,
        0x3026 /* EGL_STENCIL_SIZE */, stencil_size > 0 ? stencil_size : 8,
        0x3038 /* EGL_NONE */
    };
    EGLConfig config = nullptr;
    EGLint numConfigs = 0;
    if (!bionic_eglChooseConfig(dpy, attribs, &config, 1, &numConfigs) || numConfigs < 1) {
        const EGLint relaxedAttribs[] = {
            0x3040, renderable,
            0x3033, 0x4,
            0x3024, 8,
            0x3023, 8,
            0x3022, 8,
            0x3038
        };
        if (!bionic_eglChooseConfig(dpy, relaxedAttribs, &config, 1, &numConfigs) || numConfigs < 1) {
            gpuLog("kudroid_gl_surface: eglChooseConfig found no matching config");
            return 0;
        }
    }

    const EGLint ctxAttribs[] = {
        0x3098 /* EGL_CONTEXT_CLIENT_VERSION */, client_version > 0 ? client_version : 2,
        0x3038
    };
    EGLContext ctx = bionic_eglCreateContext(dpy, config, nullptr, ctxAttribs);
    if (!ctx) {
        gpuLog("kudroid_gl_surface: eglCreateContext failed");
        return 0;
    }

    void* win = bionic_ANativeWindow_fromSurface(nullptr, nullptr);
    EGLSurface surf = bionic_eglCreateWindowSurface(dpy, config, (EGLNativeWindowType)win, nullptr);
    if (!surf) {
        gpuLog("kudroid_gl_surface: eglCreateWindowSurface failed");
        bionic_eglDestroyContext(dpy, ctx);
        return 0;
    }

    auto* state = new KuDroidGLSurfaceState();
    state->dpy = dpy;
    state->config = config;
    state->ctx = ctx;
    state->surf = surf;
    state->nw = static_cast<KuDroidNativeWindow*>(win);
    gpuLog("kudroid_gl_surface: created state=%p (dpy=%p ctx=%p surf=%p win=%p)",
           (void*)state, (void*)dpy, (void*)ctx, (void*)surf, win);
    return reinterpret_cast<int64_t>(state);
}

extern "C" int32_t bionic_kudroid_gl_surface_make_current(int64_t handle) {
    auto* state = reinterpret_cast<KuDroidGLSurfaceState*>(handle);
    if (!state || !state->dpy || !state->surf || !state->ctx) return 0;
    if (!bionic_eglMakeCurrent(state->dpy, state->surf, state->surf, state->ctx)) return 0;
    bionic_eglSwapInterval(state->dpy, 1);
    return 1;
}

extern "C" int32_t bionic_kudroid_gl_surface_swap(int64_t handle) {
    auto* state = reinterpret_cast<KuDroidGLSurfaceState*>(handle);
    if (!state || !state->dpy || !state->surf) return 0;
    return bionic_eglSwapBuffers(state->dpy, state->surf) ? 1 : 0;
}

extern "C" void bionic_kudroid_gl_surface_destroy(int64_t handle) {
    auto* state = reinterpret_cast<KuDroidGLSurfaceState*>(handle);
    if (!state) return;
    gpuLog("kudroid_gl_surface: destroying state=%p", (void*)state);
    if (state->dpy) {
        bionic_eglMakeCurrent(state->dpy, nullptr, nullptr, nullptr);
        if (state->surf) bionic_eglDestroySurface(state->dpy, state->surf);
        if (state->ctx) bionic_eglDestroyContext(state->dpy, state->ctx);
    }
    delete state;
}

extern "C" void bionic_glMemoryBarrier(unsigned int barriers) {
    typedef void (*PFN)(unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glMemoryBarrier");
    if (!f) { EGL_FORWARD_ERR("glMemoryBarrier", ""); return; }
    gpuLog("glMemoryBarrier(barriers=0x%x)", barriers);
    f(barriers);
}

extern "C" void bionic_glBindImageTexture(unsigned int unit, unsigned int texture, int level,
                                          unsigned int layered, int layer,
                                          unsigned int access, unsigned int format) {
    typedef void (*PFN)(unsigned int, unsigned int, int, unsigned char, int,
                        unsigned int, unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glBindImageTexture");
    if (!f) { EGL_FORWARD_ERR("glBindImageTexture", ""); return; }
    gpuLog("glBindImageTexture(unit=%u texture=%u level=%d layered=%u layer=%d)",
           unit, texture, level, layered, layer);
    f(unit, texture, level, static_cast<unsigned char>(layered), layer, access, format);
}

extern "C" void bionic_glFramebufferParameteri(unsigned int target, unsigned int pname, int param) {
    typedef void (*PFN)(unsigned int, unsigned int, int);
    auto f = (PFN)GL_FUNC_CACHED("glFramebufferParameteri");
    if (!f) { EGL_FORWARD_ERR("glFramebufferParameteri", ""); return; }
    gpuLog("glFramebufferParameteri(target=0x%x pname=0x%x param=%d)", target, pname, param);
    f(target, pname, param);
}

extern "C" unsigned int bionic_glCreateShader(unsigned int type) {
    typedef unsigned int (*PFN)(unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glCreateShader");
    if (!f) { EGL_FORWARD_ERR("glCreateShader", ""); return 0; }
    gpuLog("glCreateShader(type=0x%x): calling ANGLE...", type);
    unsigned int s = f(type);
    gpuLog("glCreateShader -> %u", s);
    return s;
}

extern "C" void bionic_glShaderSource(unsigned int shader, int count, const char* const* string, const int* length) {
    typedef void (*PFN)(unsigned int, int, const char* const*, const int*);
    auto f = (PFN)GL_FUNC_CACHED("glShaderSource");
    if (!f) { EGL_FORWARD_ERR("glShaderSource", ""); return; }

    gpuLog("glShaderSource(shader=%u count=%d): marshalling guest memory to host buffer...", shader, count);
    if (!string || count <= 0) {
        f(shader, count, string, length);
        return;
    }

    // Safely copy the entire shader chain from the Guest memory (ELF/Stack) to the Host C++ heap
    std::vector<std::string> hostStrings;
    std::vector<const char*> hostPtrs;
    std::vector<int> hostLens;
    hostStrings.reserve(count);
    hostPtrs.reserve(count);
    hostLens.reserve(count);

    for (int i = 0; i < count; ++i) {
        if (string[i]) {
            int len = (length && length[i] >= 0) ? length[i] : static_cast<int>(strlen(string[i]));
            std::string src(string[i], len);
            hostStrings.push_back(std::move(src));
            hostLens.push_back(static_cast<int>(hostStrings.back().size()));
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

    std::string combined;
    for (const auto& source : hostStrings) combined += source;
    const std::string translated = surface_texture::shader(std::move(combined));
    const char* source = translated.data();
    const int sourceLength = static_cast<int>(translated.size());
    f(shader, 1, &source, &sourceLength);
    gpuLog("glShaderSource(shader=%u) -> OK", shader);
}

static void log_gl_object_failure(unsigned int object, bool program) {
    static std::atomic<unsigned> budget{0};
    if (budget.fetch_add(1, std::memory_order_relaxed) >= 32) return;
    using GetIv = void (*)(unsigned int, unsigned int, int*);
    using GetLog = void (*)(unsigned int, int, int*, char*);
    auto get_iv = reinterpret_cast<GetIv>(program ? GL_FUNC_CACHED("glGetProgramiv")
                                                  : GL_FUNC_CACHED("glGetShaderiv"));
    auto get_log = reinterpret_cast<GetLog>(program ? GL_FUNC_CACHED("glGetProgramInfoLog")
                                                    : GL_FUNC_CACHED("glGetShaderInfoLog"));
    if (!get_iv || !get_log) return;
    int length = 0;
    get_iv(object, 0x8B84 /* GL_INFO_LOG_LENGTH */, &length);
    const int capacity = length > 1 ? (length < 4096 ? length : 4096) : 1;
    std::vector<char> info(static_cast<size_t>(capacity), '\0');
    int written = 0;
    get_log(object, capacity, &written, info.data());
    info.back() = '\0';
    ::kudroid::log::write(::kudroid::log::kError, "KuDroidGPU",
                          "GL %s %u failed: %s", program ? "program link" : "shader compilation",
                          object, info.data());
}

extern "C" void bionic_glCompileShader(unsigned int shader) {
    typedef void (*PFN)(unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glCompileShader");
    if (!f) { EGL_FORWARD_ERR("glCompileShader", ""); return; }
    gpuLog("glCompileShader(shader=%u): calling ANGLE...", shader);
    f(shader);
    gpuLog("glCompileShader(shader=%u) submitted", shader);
}

extern "C" void bionic_glGetShaderiv(unsigned int shader, unsigned int pname, int* params) {
    typedef void (*PFN)(unsigned int, unsigned int, int*);
    auto f = (PFN)GL_FUNC_CACHED("glGetShaderiv");
    if (!f) { EGL_FORWARD_ERR("glGetShaderiv", ""); return; }
    f(shader, pname, params);
    if (params && pname == 0x8B81 /* GL_COMPILE_STATUS */ && !*params) {
        log_gl_object_failure(shader, false);
    }
    gpuLog("glGetShaderiv(shader=%u pname=0x%x param=%d)", shader, pname, params ? *params : 0);
}

extern "C" void bionic_glGetShaderInfoLog(unsigned int shader, int maxLength, int* length, char* infoLog) {
    typedef void (*PFN)(unsigned int, int, int*, char*);
    auto f = (PFN)GL_FUNC_CACHED("glGetShaderInfoLog");
    if (!f) { EGL_FORWARD_ERR("glGetShaderInfoLog", ""); return; }
    f(shader, maxLength, length, infoLog);
    gpuLog("glGetShaderInfoLog(shader=%u log='%s')", shader, infoLog ? infoLog : "");
}

extern "C" unsigned int bionic_glCreateProgram(void) {
    typedef unsigned int (*PFN)(void);
    auto f = (PFN)GL_FUNC_CACHED("glCreateProgram");
    if (!f) { EGL_FORWARD_ERR("glCreateProgram", ""); return 0; }
    gpuLog("glCreateProgram: calling ANGLE...");
    unsigned int p = f();
    gpuLog("glCreateProgram -> %u", p);
    return p;
}

extern "C" void bionic_glAttachShader(unsigned int program, unsigned int shader) {
    typedef void (*PFN)(unsigned int, unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glAttachShader");
    if (!f) { EGL_FORWARD_ERR("glAttachShader", ""); return; }
    gpuLog("glAttachShader(prog=%u, shader=%u)", program, shader);
    f(program, shader);
}

extern "C" void bionic_glLinkProgram(unsigned int program) {
    typedef void (*PFN)(unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glLinkProgram");
    if (!f) { EGL_FORWARD_ERR("glLinkProgram", ""); return; }
    surface_texture::program_changed(program);
    gpuLog("glLinkProgram(program=%u): calling ANGLE...", program);
#if defined(__APPLE__)
    void* pool = objc_autoreleasePoolPush();
    f(program);
    objc_autoreleasePoolPop(pool);
#else
    f(program);
#endif
    gpuLog("glLinkProgram(program=%u) submitted", program);
}

extern "C" void bionic_glGetProgramiv(unsigned int program, unsigned int pname, int* params) {
    typedef void (*PFN)(unsigned int, unsigned int, int*);
    auto f = (PFN)GL_FUNC_CACHED("glGetProgramiv");
    if (!f) { EGL_FORWARD_ERR("glGetProgramiv", ""); return; }
    f(program, pname, params);
    if (params && pname == 0x8B82 /* GL_LINK_STATUS */ && !*params) {
        log_gl_object_failure(program, true);
    }
    gpuLog("glGetProgramiv(prog=%u pname=0x%x param=%d)", program, pname, params ? *params : 0);
}

namespace {
struct MaterialAudit {
    unsigned epoch = ~0u;
    bool pending = false;
    void* context = nullptr;
    unsigned selected = 0;
    std::unordered_map<void*, std::unordered_set<unsigned>> programs;
};
thread_local MaterialAudit materialAudit;

void select_material_audit(unsigned program) {
    materialAudit.pending = false;
    if (!program || g_materialAuditCount.load(std::memory_order_relaxed) >= 96) return;
    const unsigned epoch = g_materialAuditEpoch.load(std::memory_order_relaxed);
    if (materialAudit.epoch != epoch) {
        materialAudit.programs.clear();
        materialAudit.epoch = epoch;
    }
    using Current = void* (*)();
    static auto current = reinterpret_cast<Current>(get_egl_func("eglGetCurrentContext"));
    if (current) {
        materialAudit.context = current();
        materialAudit.selected = program;
        materialAudit.pending = !materialAudit.programs[materialAudit.context].count(program);
    }
}

// Query each program once at its first draw, after render state has been applied.
void audit_material_draw() {
    if (!materialAudit.pending) return;
    materialAudit.pending = false;
    materialAudit.programs[materialAudit.context].insert(materialAudit.selected);
    if (g_materialAuditCount.fetch_add(1, std::memory_order_relaxed) >= 96) return;
    using String = const char* (*)(unsigned);
    auto strings = reinterpret_cast<String>(GL_FUNC_CACHED("glGetString"));
    const char* version = strings ? strings(0x1f02 /* GL_VERSION */) : nullptr;
    if (!version || !std::strstr(version, "OpenGL ES 3.")) return;
    using Integer = void (*)(unsigned, int*);
    using Program = void (*)(unsigned, unsigned, int*);
    using Uniform = void (*)(unsigned, unsigned, int, int*, int*, unsigned*, char*);
    using Attachment = void (*)(unsigned, unsigned, unsigned, int*);
    using Enabled = unsigned char (*)(unsigned);
    auto integer = reinterpret_cast<Integer>(GL_FUNC_CACHED("glGetIntegerv"));
    auto program = reinterpret_cast<Program>(GL_FUNC_CACHED("glGetProgramiv"));
    auto uniform = reinterpret_cast<Uniform>(GL_FUNC_CACHED("glGetActiveUniform"));
    auto attachment = reinterpret_cast<Attachment>(GL_FUNC_CACHED("glGetFramebufferAttachmentParameteriv"));
    auto enabled = reinterpret_cast<Enabled>(GL_FUNC_CACHED("glIsEnabled"));
    if (!integer || !program || !uniform || !attachment || !enabled) return;
    int id = 0, framebuffer = 0, count = 0, maximum = 0;
    integer(0x8b8d /* GL_CURRENT_PROGRAM */, &id);
    if (!id) return;
    integer(0x8ca6 /* GL_DRAW_FRAMEBUFFER_BINDING */, &framebuffer);
    program(id, 0x8b86 /* GL_ACTIVE_UNIFORMS */, &count);
    program(id, 0x8b87 /* GL_ACTIVE_UNIFORM_MAX_LENGTH */, &maximum);
    int mask[4] = {}, source = 0, destination = 0;
    integer(0x0c23 /* GL_COLOR_WRITEMASK */, mask);
    integer(0x80c9 /* GL_BLEND_SRC_RGB */, &source);
    integer(0x80c8 /* GL_BLEND_DST_RGB */, &destination);
    std::string names;
    if (maximum > 0 && maximum <= 65536) {
        std::vector<char> name(static_cast<size_t>(maximum), '\0');
        for (int i = 0; i < std::min(count, 16); ++i) {
            int length = 0, size = 0;
            unsigned type = 0;
            uniform(id, i, maximum, &length, &size, &type, name.data());
            name.back() = '\0';
            if (!names.empty()) names += ',';
            names.append(name.data(), std::min(std::strlen(name.data()), size_t{64}));
            if (names.size() >= 384) break;
        }
    }
    std::string targets;
    if (framebuffer) {
        int buffers = 0, colors = 0;
        integer(0x8824 /* GL_MAX_DRAW_BUFFERS */, &buffers);
        integer(0x8cdf /* GL_MAX_COLOR_ATTACHMENTS */, &colors);
        for (int i = 0; i < std::min(buffers, 4); ++i) {
            int buffer = 0, object = 0, type = 0, bits[4] = {};
            integer(0x8825u + i /* GL_DRAW_BUFFER0 */, &buffer);
            if (buffer < 0x8ce0 || buffer >= 0x8ce0 + colors) continue;
            attachment(0x8ca9 /* GL_DRAW_FRAMEBUFFER */, buffer, 0x8cd0, &object);
            if (!object) continue;
            attachment(0x8ca9, buffer, 0x8211 /* GL_FRAMEBUFFER_ATTACHMENT_COMPONENT_TYPE */, &type);
            for (int c = 0; c < 4; ++c) attachment(0x8ca9, buffer, 0x8212u + c, &bits[c]);
            char value[112];
            std::snprintf(value, sizeof(value), " out%d(type=0x%x bits=%d/%d/%d/%d)",
                          i, type, bits[0], bits[1], bits[2], bits[3]);
            targets += value;
        }
    }
    KLOG(kInfo, "KuDroidGPU", "GL material program=%d blend=%u factors=0x%x/0x%x mask=%d%d%d%d fbo=%d uniforms=%d [%s]%s",
         id, static_cast<unsigned>(enabled(0x0be2 /* GL_BLEND */)),
         source, destination, mask[0], mask[1], mask[2], mask[3], framebuffer,
         count, names.c_str(), targets.c_str());
}
} // namespace

extern "C" void bionic_glUseProgram(unsigned int program) {
    typedef void (*PFN)(unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glUseProgram");
    if (!f) { EGL_FORWARD_ERR("glUseProgram", ""); return; }
    gpuLog("glUseProgram(prog=%u)", program);
    f(program);
    surface_texture::use_program(program);
    select_material_audit(program);
}

extern "C" int bionic_glGetAttribLocation(unsigned int program, const char* name) {
    typedef int (*PFN)(unsigned int, const char*);
    auto f = (PFN)GL_FUNC_CACHED("glGetAttribLocation");
    if (!f) { EGL_FORWARD_ERR("glGetAttribLocation", ""); return -1; }
    int loc = f(program, name);
    gpuLog("glGetAttribLocation(prog=%u name='%s') -> %d", program, name ? name : "", loc);
    return loc;
}

extern "C" void bionic_glEnableVertexAttribArray(unsigned int index) {
    typedef void (*PFN)(unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glEnableVertexAttribArray");
    if (!f) { EGL_FORWARD_ERR("glEnableVertexAttribArray", ""); return; }
    f(index);
}

extern "C" void bionic_glVertexAttribPointer(unsigned int index, int size, unsigned int type, unsigned int normalized, int stride, const void* pointer) {
    typedef void (*PFN)(unsigned int, int, unsigned int, unsigned char, int, const void*);
    auto f = (PFN)GL_FUNC_CACHED("glVertexAttribPointer");
    if (!f) { EGL_FORWARD_ERR("glVertexAttribPointer", ""); return; }
    // Not logged: per-attribute per-draw state, sampled or not it was 2144 of
    // 3856 lines in one run's crash buffer and pushed real faults out of it.
    // Only KUDROID_GPU_LOG_ALL=1 (debug session) records it.
    if (gpu_log_all()) {
        gpuLog("glVertexAttribPointer(index=%u size=%d stride=%d ptr=%p)", index, size, stride, pointer);
    }
    f(index, size, type, static_cast<unsigned char>(normalized), stride, pointer);
}

extern "C" void bionic_glClearColor(float red, float green, float blue, float alpha) {
    typedef void (*PFN)(float, float, float, float);
    auto f = (PFN)GL_FUNC_CACHED("glClearColor");
    if (!f) { EGL_FORWARD_ERR("glClearColor", ""); return; }
    f(red, green, blue, alpha);
}

extern "C" void bionic_glClear(unsigned int mask) {
    typedef void (*PFN)(unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glClear");
    if (!f) { EGL_FORWARD_ERR("glClear", ""); return; }
    ++g_clearCalls;
    f(mask);
}

extern "C" void bionic_glDrawArrays(unsigned int mode, int first, int count) {
    surface_texture::DrawScope external;
    typedef void (*PFN)(unsigned int, int, int);
    auto f = (PFN)GL_FUNC_CACHED("glDrawArrays");
    if (!f) { EGL_FORWARD_ERR("glDrawArrays", ""); return; }
    ++g_drawCalls;
    audit_material_draw();
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
    auto f = (PFN)GL_FUNC_CACHED("glViewport");
    if (!f) { EGL_FORWARD_ERR("glViewport", ""); return; }
    gpuLog("glViewport(%d, %d, %d, %d)", x, y, width, height);
    f(x, y, width, height);
}

extern "C" void bionic_glGenBuffers(int n, unsigned int* buffers) {
    typedef void (*PFN)(int, unsigned int*);
    auto f = (PFN)GL_FUNC_CACHED("glGenBuffers");
    if (!f) { EGL_FORWARD_ERR("glGenBuffers", ""); return; }
    f(n, buffers);
    gpuLog("glGenBuffers(n=%d) -> buf=%u", n, (buffers && n > 0) ? buffers[0] : 0);
}

extern "C" void bionic_glBindBuffer(unsigned int target, unsigned int buffer) {
    typedef void (*PFN)(unsigned int, unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glBindBuffer");
    if (!f) { EGL_FORWARD_ERR("glBindBuffer", ""); return; }
    f(target, buffer);
}

extern "C" void bionic_glBufferData(unsigned int target, long size, const void* data, unsigned int usage) {
    typedef void (*PFN)(unsigned int, long, const void*, unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glBufferData");
    if (!f) { EGL_FORWARD_ERR("glBufferData", ""); return; }
    gpuLog("glBufferData(target=0x%x size=%ld usage=0x%x)", target, size, usage);
    f(target, size, data, usage);
}

// ── Textures ────────────────────────────────────────────────────────────────
extern "C" void bionic_glGenTextures(int n, unsigned int* textures) {
    typedef void (*PFN)(int, unsigned int*);
    auto f = (PFN)GL_FUNC_CACHED("glGenTextures");
    if (f) f(n, textures);
}

extern "C" void bionic_glBindTexture(unsigned int target, unsigned int texture) {
    if (surface_texture::bind(target, texture)) return;
    typedef void (*PFN)(unsigned int, unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glBindTexture");
    if (f) f(target, texture);
}
extern "C" void bionic_glDeleteTextures(int n, const unsigned int* textures) {
    surface_texture::deleted_textures(n, textures);
    typedef void (*PFN)(int, const unsigned int*);
    auto f = (PFN)GL_FUNC_CACHED("glDeleteTextures");
    if (f) f(n, textures);
}
extern "C" void bionic_glTexImage2D(unsigned int target, int level, int internalformat, int width, int height, int border, unsigned int format, unsigned int type, const void* pixels) {
    typedef void (*PFN)(unsigned int, int, int, int, int, int, unsigned int, unsigned int, const void*);
    auto f = (PFN)GL_FUNC_CACHED("glTexImage2D");
    if (gpu_diag_enabled()) {
        gpu_diag_texture_upload("glTexImage2D", target, level, width, height, format, type, pixels);
    }
    if (f) f(target, level, internalformat, width, height, border, format, type, pixels);
}
extern "C" void bionic_glTexSubImage2D(unsigned int target, int level, int xoffset, int yoffset, int width, int height, unsigned int format, unsigned int type, const void* pixels) {
    typedef void (*PFN)(unsigned int, int, int, int, int, int, unsigned int, unsigned int, const void*);
    auto f = (PFN)GL_FUNC_CACHED("glTexSubImage2D");
    if (gpu_diag_enabled()) {
        gpu_diag_texture_upload("glTexSubImage2D", target, level, width, height, format, type, pixels);
    }
    if (f) f(target, level, xoffset, yoffset, width, height, format, type, pixels);
}
extern "C" void bionic_glTexParameteri(unsigned int target, unsigned int pname, int param) {
    typedef void (*PFN)(unsigned int, unsigned int, int);
    auto f = (PFN)GL_FUNC_CACHED("glTexParameteri");
    if (f) f(surface_texture::target(target), pname, param);
    surface_texture::restore_binding(target);
}
extern "C" void bionic_glTexParameterf(unsigned int target, unsigned int pname, float param) {
    typedef void (*PFN)(unsigned int, unsigned int, float);
    auto f = (PFN)GL_FUNC_CACHED("glTexParameterf");
    if (f) f(surface_texture::target(target), pname, param);
    surface_texture::restore_binding(target);
}
extern "C" void bionic_glTexParameteriv(unsigned int target, unsigned int pname, const int* params) {
    typedef void (*PFN)(unsigned int, unsigned int, const int*);
    auto f = (PFN)GL_FUNC_CACHED("glTexParameteriv");
    if (f) f(surface_texture::target(target), pname, params);
    surface_texture::restore_binding(target);
}
extern "C" void bionic_glTexParameterfv(unsigned int target, unsigned int pname, const float* params) {
    typedef void (*PFN)(unsigned int, unsigned int, const float*);
    auto f = (PFN)GL_FUNC_CACHED("glTexParameterfv");
    if (f) f(surface_texture::target(target), pname, params);
    surface_texture::restore_binding(target);
}
extern "C" void bionic_glActiveTexture(unsigned int texture) {
    typedef void (*PFN)(unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glActiveTexture");
    if (f) f(texture);
}
extern "C" void bionic_glGenerateMipmap(unsigned int target) {
    typedef void (*PFN)(unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glGenerateMipmap");
    if (f) f(target);
}
extern "C" void bionic_glPixelStorei(unsigned int pname, int param) {
    typedef void (*PFN)(unsigned int, int);
    auto f = (PFN)GL_FUNC_CACHED("glPixelStorei");
    if (f) f(pname, param);
}
extern "C" void bionic_glCompressedTexImage2D(unsigned int target, int level, unsigned int internalformat, int width, int height, int border, int imageSize, const void* data) {
    typedef void (*PFN)(unsigned int, int, unsigned int, int, int, int, int, const void*);
    auto f = (PFN)GL_FUNC_CACHED("glCompressedTexImage2D");
    if (f) f(target, level, internalformat, width, height, border, imageSize, data);
}
extern "C" void bionic_glCompressedTexSubImage2D(unsigned int target, int level, int xoffset, int yoffset, int width, int height, unsigned int format, int imageSize, const void* data) {
    typedef void (*PFN)(unsigned int, int, int, int, int, int, unsigned int, int, const void*);
    auto f = (PFN)GL_FUNC_CACHED("glCompressedTexSubImage2D");
    if (f) f(target, level, xoffset, yoffset, width, height, format, imageSize, data);
}

// ── Buffers & Elements ──────────────────────────────────────────────────────
extern "C" void bionic_glDeleteBuffers(int n, const unsigned int* buffers) {
    typedef void (*PFN)(int, const unsigned int*);
    auto f = (PFN)GL_FUNC_CACHED("glDeleteBuffers");
    if (f) f(n, buffers);
}
extern "C" void bionic_glBufferSubData(unsigned int target, long offset, long size, const void* data) {
    typedef void (*PFN)(unsigned int, long, long, const void*);
    auto f = (PFN)GL_FUNC_CACHED("glBufferSubData");
    if (f) f(target, offset, size, data);
}
extern "C" void bionic_glDrawElements(unsigned int mode, int count, unsigned int type, const void* indices) {
    surface_texture::DrawScope external;
    typedef void (*PFN)(unsigned int, int, unsigned int, const void*);
    auto f = (PFN)GL_FUNC_CACHED("glDrawElements");
    if (!f) return;
    ++g_drawCalls;
    audit_material_draw();
#if defined(__APPLE__)
    void* pool = objc_autoreleasePoolPush();
    f(mode, count, type, indices);
    objc_autoreleasePoolPop(pool);
#else
    f(mode, count, type, indices);
#endif
}
extern "C" void bionic_glDisableVertexAttribArray(unsigned int index) {
    typedef void (*PFN)(unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glDisableVertexAttribArray");
    if (f) f(index);
}

// ── Vertex Array Objects (VAO) ──────────────────────────────────────────────
extern "C" void bionic_glGenVertexArrays(int n, unsigned int* arrays) {
    typedef void (*PFN)(int, unsigned int*);
    auto f = (PFN)GL_FUNC_CACHED("glGenVertexArrays");
    if (f) f(n, arrays);
}
extern "C" void bionic_glGenVertexArraysOES(int n, unsigned int* arrays) {
    typedef void (*PFN)(int, unsigned int*);
    auto f = (PFN)GL_FUNC_CACHED("glGenVertexArraysOES");
    if (f) f(n, arrays);
}
extern "C" void bionic_glBindVertexArray(unsigned int array) {
    typedef void (*PFN)(unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glBindVertexArray");
    if (f) f(array);
}
extern "C" void bionic_glBindVertexArrayOES(unsigned int array) {
    typedef void (*PFN)(unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glBindVertexArrayOES");
    if (f) f(array);
}
extern "C" void bionic_glDeleteVertexArrays(int n, const unsigned int* arrays) {
    typedef void (*PFN)(int, const unsigned int*);
    auto f = (PFN)GL_FUNC_CACHED("glDeleteVertexArrays");
    if (f) f(n, arrays);
}
extern "C" void bionic_glDeleteVertexArraysOES(int n, const unsigned int* arrays) {
    typedef void (*PFN)(int, const unsigned int*);
    auto f = (PFN)GL_FUNC_CACHED("glDeleteVertexArraysOES");
    if (f) f(n, arrays);
}

// ── Framebuffers & Renderbuffers ────────────────────────────────────────────
extern "C" void bionic_glGenFramebuffers(int n, unsigned int* framebuffers) {
    typedef void (*PFN)(int, unsigned int*);
    auto f = (PFN)GL_FUNC_CACHED("glGenFramebuffers");
    if (f) f(n, framebuffers);
}
extern "C" void bionic_glBindFramebuffer(unsigned int target, unsigned int framebuffer) {
    typedef void (*PFN)(unsigned int, unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glBindFramebuffer");
    if (f) f(target, framebuffer);
}
extern "C" void bionic_glDeleteFramebuffers(int n, const unsigned int* framebuffers) {
    typedef void (*PFN)(int, const unsigned int*);
    auto f = (PFN)GL_FUNC_CACHED("glDeleteFramebuffers");
    if (f) f(n, framebuffers);
}
extern "C" void bionic_glFramebufferTexture2D(unsigned int target, unsigned int attachment, unsigned int textarget, unsigned int texture, int level) {
    typedef void (*PFN)(unsigned int, unsigned int, unsigned int, unsigned int, int);
    auto f = (PFN)GL_FUNC_CACHED("glFramebufferTexture2D");
    // External images use RGBA 2D storage in the host context.
    if (textarget == 0x8d65 /* GL_TEXTURE_EXTERNAL_OES */) textarget = 0x0de1;
    if (f) f(target, attachment, textarget, texture, level);
}
extern "C" unsigned int bionic_glCheckFramebufferStatus(unsigned int target) {
    typedef unsigned int (*PFN)(unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glCheckFramebufferStatus");
    if (f) return f(target);
    return 0;
}
extern "C" void bionic_glGenRenderbuffers(int n, unsigned int* renderbuffers) {
    typedef void (*PFN)(int, unsigned int*);
    auto f = (PFN)GL_FUNC_CACHED("glGenRenderbuffers");
    if (f) f(n, renderbuffers);
}
extern "C" void bionic_glBindRenderbuffer(unsigned int target, unsigned int renderbuffer) {
    typedef void (*PFN)(unsigned int, unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glBindRenderbuffer");
    if (f) f(target, renderbuffer);
}
extern "C" void bionic_glDeleteRenderbuffers(int n, const unsigned int* renderbuffers) {
    typedef void (*PFN)(int, const unsigned int*);
    auto f = (PFN)GL_FUNC_CACHED("glDeleteRenderbuffers");
    if (f) f(n, renderbuffers);
}
extern "C" void bionic_glRenderbufferStorage(unsigned int target, unsigned int internalformat, int width, int height) {
    typedef void (*PFN)(unsigned int, unsigned int, int, int);
    auto f = (PFN)GL_FUNC_CACHED("glRenderbufferStorage");
    if (f) f(target, internalformat, width, height);
}
extern "C" void bionic_glFramebufferRenderbuffer(unsigned int target, unsigned int attachment, unsigned int renderbuffertarget, unsigned int renderbuffer) {
    typedef void (*PFN)(unsigned int, unsigned int, unsigned int, unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glFramebufferRenderbuffer");
    if (f) f(target, attachment, renderbuffertarget, renderbuffer);
}

// ── Uniforms ────────────────────────────────────────────────────────────────
extern "C" int bionic_glGetUniformLocation(unsigned int program, const char* name) {
    typedef int (*PFN)(unsigned int, const char*);
    auto f = (PFN)GL_FUNC_CACHED("glGetUniformLocation");
    if (f) {
        const int location = f(program, name);
        if (location >= 0 || !name) return location;
        return f(program, surface_texture::uniform_name(name).c_str());
    }
    return -1;
}
extern "C" void bionic_glUniform1f(int location, float v0) {
    typedef void (*PFN)(int, float);
    auto f = (PFN)GL_FUNC_CACHED("glUniform1f");
    if (f) f(location, v0);
}
extern "C" void bionic_glUniform2f(int location, float v0, float v1) {
    typedef void (*PFN)(int, float, float);
    auto f = (PFN)GL_FUNC_CACHED("glUniform2f");
    if (f) f(location, v0, v1);
}
extern "C" void bionic_glUniform3f(int location, float v0, float v1, float v2) {
    typedef void (*PFN)(int, float, float, float);
    auto f = (PFN)GL_FUNC_CACHED("glUniform3f");
    if (f) f(location, v0, v1, v2);
}
extern "C" void bionic_glUniform4f(int location, float v0, float v1, float v2, float v3) {
    typedef void (*PFN)(int, float, float, float, float);
    auto f = (PFN)GL_FUNC_CACHED("glUniform4f");
    if (f) f(location, v0, v1, v2, v3);
}
extern "C" void bionic_glUniform1i(int location, int v0) {
    typedef void (*PFN)(int, int);
    auto f = (PFN)GL_FUNC_CACHED("glUniform1i");
    if (f) f(location, v0);
}
extern "C" void bionic_glUniform2i(int location, int v0, int v1) {
    typedef void (*PFN)(int, int, int);
    auto f = (PFN)GL_FUNC_CACHED("glUniform2i");
    if (f) f(location, v0, v1);
}
extern "C" void bionic_glUniform3i(int location, int v0, int v1, int v2) {
    typedef void (*PFN)(int, int, int, int);
    auto f = (PFN)GL_FUNC_CACHED("glUniform3i");
    if (f) f(location, v0, v1, v2);
}
extern "C" void bionic_glUniform4i(int location, int v0, int v1, int v2, int v3) {
    typedef void (*PFN)(int, int, int, int, int);
    auto f = (PFN)GL_FUNC_CACHED("glUniform4i");
    if (f) f(location, v0, v1, v2, v3);
}
extern "C" void bionic_glUniform1fv(int location, int count, const float* value) {
    typedef void (*PFN)(int, int, const float*);
    auto f = (PFN)GL_FUNC_CACHED("glUniform1fv");
    if (f) f(location, count, value);
}
extern "C" void bionic_glUniform2fv(int location, int count, const float* value) {
    typedef void (*PFN)(int, int, const float*);
    auto f = (PFN)GL_FUNC_CACHED("glUniform2fv");
    if (f) f(location, count, value);
}
extern "C" void bionic_glUniform3fv(int location, int count, const float* value) {
    typedef void (*PFN)(int, int, const float*);
    auto f = (PFN)GL_FUNC_CACHED("glUniform3fv");
    if (f) f(location, count, value);
}
extern "C" void bionic_glUniform4fv(int location, int count, const float* value) {
    typedef void (*PFN)(int, int, const float*);
    auto f = (PFN)GL_FUNC_CACHED("glUniform4fv");
    if (f) f(location, count, value);
}
extern "C" void bionic_glUniform1iv(int location, int count, const int* value) {
    typedef void (*PFN)(int, int, const int*);
    auto f = (PFN)GL_FUNC_CACHED("glUniform1iv");
    if (f) f(location, count, value);
}
extern "C" void bionic_glUniform2iv(int location, int count, const int* value) {
    typedef void (*PFN)(int, int, const int*);
    auto f = (PFN)GL_FUNC_CACHED("glUniform2iv");
    if (f) f(location, count, value);
}
extern "C" void bionic_glUniform3iv(int location, int count, const int* value) {
    typedef void (*PFN)(int, int, const int*);
    auto f = (PFN)GL_FUNC_CACHED("glUniform3iv");
    if (f) f(location, count, value);
}
extern "C" void bionic_glUniform4iv(int location, int count, const int* value) {
    typedef void (*PFN)(int, int, const int*);
    auto f = (PFN)GL_FUNC_CACHED("glUniform4iv");
    if (f) f(location, count, value);
}
extern "C" void bionic_glUniformMatrix2fv(int location, int count, unsigned int transpose, const float* value) {
    typedef void (*PFN)(int, int, unsigned char, const float*);
    auto f = (PFN)GL_FUNC_CACHED("glUniformMatrix2fv");
    if (f) f(location, count, static_cast<unsigned char>(transpose), value);
}
extern "C" void bionic_glUniformMatrix3fv(int location, int count, unsigned int transpose, const float* value) {
    typedef void (*PFN)(int, int, unsigned char, const float*);
    auto f = (PFN)GL_FUNC_CACHED("glUniformMatrix3fv");
    if (f) f(location, count, static_cast<unsigned char>(transpose), value);
}
extern "C" void bionic_glUniformMatrix4fv(int location, int count, unsigned int transpose, const float* value) {
    typedef void (*PFN)(int, int, unsigned char, const float*);
    auto f = (PFN)GL_FUNC_CACHED("glUniformMatrix4fv");
    if (f) f(location, count, static_cast<unsigned char>(transpose), value);
}

// ── Render States & Capabilities ────────────────────────────────────────────
extern "C" void bionic_glEnable(unsigned int cap) {
    typedef void (*PFN)(unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glEnable");
    if (f) f(cap);
}
extern "C" void bionic_glDisable(unsigned int cap) {
    typedef void (*PFN)(unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glDisable");
    if (f) f(cap);
}
extern "C" unsigned char bionic_glIsEnabled(unsigned int cap) {
    typedef unsigned char (*PFN)(unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glIsEnabled");
    if (f) return f(cap);
    return 0;
}
extern "C" void bionic_glBlendFunc(unsigned int sfactor, unsigned int dfactor) {
    typedef void (*PFN)(unsigned int, unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glBlendFunc");
    if (f) f(sfactor, dfactor);
}
extern "C" void bionic_glBlendFuncSeparate(unsigned int srcRGB, unsigned int dstRGB, unsigned int srcAlpha, unsigned int dstAlpha) {
    typedef void (*PFN)(unsigned int, unsigned int, unsigned int, unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glBlendFuncSeparate");
    if (f) f(srcRGB, dstRGB, srcAlpha, dstAlpha);
}
extern "C" void bionic_glBlendEquation(unsigned int mode) {
    typedef void (*PFN)(unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glBlendEquation");
    if (f) f(mode);
}
extern "C" void bionic_glBlendEquationSeparate(unsigned int modeRGB, unsigned int modeAlpha) {
    typedef void (*PFN)(unsigned int, unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glBlendEquationSeparate");
    if (f) f(modeRGB, modeAlpha);
}
extern "C" void bionic_glDepthFunc(unsigned int func) {
    typedef void (*PFN)(unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glDepthFunc");
    if (f) f(func);
}
extern "C" void bionic_glDepthMask(unsigned int flag) {
    typedef void (*PFN)(unsigned char);
    auto f = (PFN)GL_FUNC_CACHED("glDepthMask");
    if (f) f(static_cast<unsigned char>(flag));
}
extern "C" void bionic_glCullFace(unsigned int mode) {
    typedef void (*PFN)(unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glCullFace");
    if (f) f(mode);
}
extern "C" void bionic_glFrontFace(unsigned int mode) {
    typedef void (*PFN)(unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glFrontFace");
    if (f) f(mode);
}
extern "C" void bionic_glScissor(int x, int y, int width, int height) {
    typedef void (*PFN)(int, int, int, int);
    auto f = (PFN)GL_FUNC_CACHED("glScissor");
    if (f) f(x, y, width, height);
}
extern "C" void bionic_glColorMask(unsigned int red, unsigned int green, unsigned int blue, unsigned int alpha) {
    typedef void (*PFN)(unsigned char, unsigned char, unsigned char, unsigned char);
    auto f = (PFN)GL_FUNC_CACHED("glColorMask");
    if (f) f(static_cast<unsigned char>(red), static_cast<unsigned char>(green),
             static_cast<unsigned char>(blue), static_cast<unsigned char>(alpha));
}
extern "C" void bionic_glFlush(void) {
    typedef void (*PFN)(void);
    auto f = (PFN)GL_FUNC_CACHED("glFlush");
    if (f) f();
}
extern "C" void bionic_glFinish(void) {
    typedef void (*PFN)(void);
    auto f = (PFN)GL_FUNC_CACHED("glFinish");
    if (f) f();
}
extern "C" void bionic_glGetIntegerv(unsigned int pname, int* params) {
    if (surface_texture::binding(pname, params)) return;
    typedef void (*PFN)(unsigned int, int*);
    auto f = (PFN)GL_FUNC_CACHED("glGetIntegerv");
    if (f) f(pname, params);
    if (f && params && pname == 0x87ff) {
        int count = 0;
        f(0x87fe, &count);
        for (int i = 0; i < count; ++i) params[i] ^= 0x4b440000;
    }
}

extern "C" void bionic_glGetActiveUniform(unsigned program, unsigned index, int capacity,
                                          int* length, int* size, unsigned* type, char* name) {
    using Get = void (*)(unsigned, unsigned, int, int*, int*, unsigned*, char*);
    using Iv = void (*)(unsigned, unsigned, int*);
    auto f = reinterpret_cast<Get>(GL_FUNC_CACHED("glGetActiveUniform"));
    auto iv = reinterpret_cast<Iv>(GL_FUNC_CACHED("glGetProgramiv"));
    if (!f || !iv) return;
    if (capacity < 0 || !size || !type || (capacity > 0 && !name)) {
        f(program, index, capacity, length, size, type, name);
        return;
    }
    int maximum = 0;
    iv(program, 0x8b87, &maximum);
    if (maximum <= 0 || maximum > (1 << 20)) { f(program, index, capacity, length, size, type, name); return; }
    std::vector<char> full(static_cast<size_t>(maximum), '\0');
    int written = 0;
    f(program, index, maximum, &written, size, type, full.data());
    surface_texture::active_uniform(maximum, &written, type, full.data());
    const int copied = capacity > 0 ? std::min(written, capacity - 1) : 0;
    if (capacity > 0) {
        std::memcpy(name, full.data(), static_cast<size_t>(copied));
        name[copied] = '\0';
    }
    if (length) *length = copied;
}

// Binary formats identify the shader translation ABI as well as the host driver.
extern "C" void bionic_glProgramBinary(unsigned program, unsigned format, const void* binary, int length) {
    using Fn = void (*)(unsigned, unsigned, const void*, int);
    surface_texture::program_changed(program);
    surface_texture::binary_loaded();
    if (auto f = reinterpret_cast<Fn>(GL_FUNC_CACHED("glProgramBinary")))
        f(program, format ^ 0x4b440000u, binary, length);
}
extern "C" void bionic_glProgramBinaryOES(unsigned program, unsigned format, const void* binary, int length) {
    using Fn = void (*)(unsigned, unsigned, const void*, int);
    surface_texture::program_changed(program);
    surface_texture::binary_loaded();
    if (auto f = reinterpret_cast<Fn>(GL_FUNC_CACHED("glProgramBinaryOES")))
        f(program, format ^ 0x4b440000u, binary, length);
}
extern "C" void bionic_glGetProgramBinary(unsigned program, int capacity, int* length, unsigned* format, void* binary) {
    using Fn = void (*)(unsigned, int, int*, unsigned*, void*);
    if (auto f = reinterpret_cast<Fn>(GL_FUNC_CACHED("glGetProgramBinary"))) {
        unsigned native = 0;
        f(program, capacity, length, &native, binary);
        if (format) *format = native ^ 0x4b440000u;
    }
}
extern "C" void bionic_glGetProgramBinaryOES(unsigned program, int capacity, int* length, unsigned* format, void* binary) {
    using Fn = void (*)(unsigned, int, int*, unsigned*, void*);
    if (auto f = reinterpret_cast<Fn>(GL_FUNC_CACHED("glGetProgramBinaryOES"))) {
        unsigned native = 0;
        f(program, capacity, length, &native, binary);
        if (format) *format = native ^ 0x4b440000u;
    }
}
extern "C" void bionic_glGetFloatv(unsigned int pname, float* params) {
    int texture = 0;
    if (params && surface_texture::binding(pname, &texture)) { *params = static_cast<float>(texture); return; }
    typedef void (*PFN)(unsigned int, float*);
    auto f = (PFN)GL_FUNC_CACHED("glGetFloatv");
    if (f) f(pname, params);
}
extern "C" void bionic_glGetBooleanv(unsigned int pname, unsigned char* params) {
    int texture = 0;
    if (params && surface_texture::binding(pname, &texture)) { *params = texture != 0; return; }
    typedef void (*PFN)(unsigned int, unsigned char*);
    auto f = (PFN)GL_FUNC_CACHED("glGetBooleanv");
    if (f) f(pname, params);
}
extern "C" const char* bionic_glGetString(unsigned int name) {
    typedef const char* (*PFN)(unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glGetString");
    return f ? f(name) : nullptr;
}
extern "C" const char* bionic_glGetStringi(unsigned int name, unsigned int index) {
    typedef const char* (*PFN)(unsigned int, unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glGetStringi");
    return f ? f(name, index) : nullptr;
}
extern "C" unsigned int bionic_glGetError(void) {
    typedef unsigned int (*PFN)(void);
    auto f = (PFN)GL_FUNC_CACHED("glGetError");
    if (f) {
        const unsigned error = f();
        static std::atomic<unsigned> count{0};
        if (error && count.fetch_add(1, std::memory_order_relaxed) < 32)
            KLOG(kError, "KuDroidGPU", "GL error reported to guest: 0x%x", error);
        return error;
    }
    return 0;
}
extern "C" void bionic_glReadPixels(int x, int y, int width, int height, unsigned int format, unsigned int type, void* pixels) {
    typedef void (*PFN)(int, int, int, int, unsigned int, unsigned int, void*);
    auto f = (PFN)GL_FUNC_CACHED("glReadPixels");
    if (f) f(x, y, width, height, format, type, pixels);
}
extern "C" void bionic_glDeleteShader(unsigned int shader) {
    typedef void (*PFN)(unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glDeleteShader");
    if (f) f(shader);
}
extern "C" void bionic_glDeleteProgram(unsigned int program) {
    surface_texture::program_changed(program);
    typedef void (*PFN)(unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glDeleteProgram");
    if (f) f(program);
}
extern "C" void bionic_glDetachShader(unsigned int program, unsigned int shader) {
    typedef void (*PFN)(unsigned int, unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glDetachShader");
    if (f) f(program, shader);
}

// Preserve extension entry points: their validation differs from core GLES.
#define EXTERNAL_DRAW(name, parameters, arguments) \
    extern "C" void bionic_##name parameters { \
        surface_texture::DrawScope external; \
        using Fn = void (*) parameters; \
        auto f = reinterpret_cast<Fn>(GL_FUNC_CACHED(#name)); \
        if (f) { ++g_drawCalls; audit_material_draw(); f arguments; } \
    }
EXTERNAL_DRAW(glDrawArraysInstanced, (unsigned mode, int first, int count, int instances), (mode, first, count, instances))
EXTERNAL_DRAW(glDrawArraysInstancedANGLE, (unsigned mode, int first, int count, int instances), (mode, first, count, instances))
EXTERNAL_DRAW(glDrawArraysInstancedEXT, (unsigned mode, int first, int count, int instances), (mode, first, count, instances))
EXTERNAL_DRAW(glDrawElementsInstanced, (unsigned mode, int count, unsigned type, const void* indices, int instances), (mode, count, type, indices, instances))
EXTERNAL_DRAW(glDrawElementsInstancedANGLE, (unsigned mode, int count, unsigned type, const void* indices, int instances), (mode, count, type, indices, instances))
EXTERNAL_DRAW(glDrawElementsInstancedEXT, (unsigned mode, int count, unsigned type, const void* indices, int instances), (mode, count, type, indices, instances))
EXTERNAL_DRAW(glDrawElementsBaseVertex, (unsigned mode, int count, unsigned type, const void* indices, int base), (mode, count, type, indices, base))
EXTERNAL_DRAW(glDrawElementsBaseVertexOES, (unsigned mode, int count, unsigned type, const void* indices, int base), (mode, count, type, indices, base))
EXTERNAL_DRAW(glDrawElementsBaseVertexEXT, (unsigned mode, int count, unsigned type, const void* indices, int base), (mode, count, type, indices, base))
EXTERNAL_DRAW(glDrawRangeElements, (unsigned mode, unsigned start, unsigned end, int count, unsigned type, const void* indices), (mode, start, end, count, type, indices))
EXTERNAL_DRAW(glDrawRangeElementsBaseVertex, (unsigned mode, unsigned start, unsigned end, int count, unsigned type, const void* indices, int base), (mode, start, end, count, type, indices, base))
EXTERNAL_DRAW(glDrawRangeElementsBaseVertexOES, (unsigned mode, unsigned start, unsigned end, int count, unsigned type, const void* indices, int base), (mode, start, end, count, type, indices, base))
EXTERNAL_DRAW(glDrawRangeElementsBaseVertexEXT, (unsigned mode, unsigned start, unsigned end, int count, unsigned type, const void* indices, int base), (mode, start, end, count, type, indices, base))
EXTERNAL_DRAW(glDrawElementsInstancedBaseVertex, (unsigned mode, int count, unsigned type, const void* indices, int instances, int base), (mode, count, type, indices, instances, base))
EXTERNAL_DRAW(glDrawElementsInstancedBaseVertexOES, (unsigned mode, int count, unsigned type, const void* indices, int instances, int base), (mode, count, type, indices, instances, base))
EXTERNAL_DRAW(glDrawElementsInstancedBaseVertexEXT, (unsigned mode, int count, unsigned type, const void* indices, int instances, int base), (mode, count, type, indices, instances, base))
EXTERNAL_DRAW(glDrawElementsInstancedBaseInstanceEXT, (unsigned mode, int count, unsigned type, const void* indices, int instances, unsigned base), (mode, count, type, indices, instances, base))
EXTERNAL_DRAW(glDrawArraysInstancedBaseInstanceEXT, (unsigned mode, int first, int count, int instances, unsigned base), (mode, first, count, instances, base))
EXTERNAL_DRAW(glDrawArraysInstancedBaseInstanceANGLE, (unsigned mode, int first, int count, int instances, unsigned base), (mode, first, count, instances, base))
EXTERNAL_DRAW(glDrawElementsInstancedBaseVertexBaseInstanceEXT, (unsigned mode, int count, unsigned type, const void* indices, int instances, int baseVertex, unsigned baseInstance), (mode, count, type, indices, instances, baseVertex, baseInstance))
EXTERNAL_DRAW(glDrawElementsInstancedBaseVertexBaseInstanceANGLE, (unsigned mode, int count, unsigned type, const void* indices, int instances, int baseVertex, unsigned baseInstance), (mode, count, type, indices, instances, baseVertex, baseInstance))
EXTERNAL_DRAW(glDrawArraysIndirect, (unsigned mode, const void* indirect), (mode, indirect))
EXTERNAL_DRAW(glDrawElementsIndirect, (unsigned mode, unsigned type, const void* indirect), (mode, type, indirect))
EXTERNAL_DRAW(glMultiDrawArraysEXT, (unsigned mode, const int* first, const int* count, int draws), (mode, first, count, draws))
EXTERNAL_DRAW(glMultiDrawArraysANGLE, (unsigned mode, const int* first, const int* count, int draws), (mode, first, count, draws))
EXTERNAL_DRAW(glMultiDrawElementsEXT, (unsigned mode, const int* count, unsigned type, const void* const* indices, int draws), (mode, count, type, indices, draws))
EXTERNAL_DRAW(glMultiDrawElementsANGLE, (unsigned mode, const int* count, unsigned type, const void* const* indices, int draws), (mode, count, type, indices, draws))
EXTERNAL_DRAW(glMultiDrawElementsBaseVertexEXT, (unsigned mode, const int* count, unsigned type, const void* const* indices, int draws, const int* base), (mode, count, type, indices, draws, base))
EXTERNAL_DRAW(glMultiDrawArraysIndirectEXT, (unsigned mode, const void* indirect, int draws, int stride), (mode, indirect, draws, stride))
EXTERNAL_DRAW(glMultiDrawElementsIndirectEXT, (unsigned mode, unsigned type, const void* indirect, int draws, int stride), (mode, type, indirect, draws, stride))
EXTERNAL_DRAW(glMultiDrawArraysInstancedANGLE, (unsigned mode, const int* first, const int* count, const int* instances, int draws), (mode, first, count, instances, draws))
EXTERNAL_DRAW(glMultiDrawElementsInstancedANGLE, (unsigned mode, const int* count, unsigned type, const void* const* indices, const int* instances, int draws), (mode, count, type, indices, instances, draws))
EXTERNAL_DRAW(glMultiDrawArraysInstancedBaseInstanceANGLE, (unsigned mode, const int* first, const int* count, const int* instances, const unsigned* bases, int draws), (mode, first, count, instances, bases, draws))
EXTERNAL_DRAW(glMultiDrawElementsInstancedBaseVertexBaseInstanceANGLE, (unsigned mode, const int* count, unsigned type, const void* const* indices, const int* instances, const int* vertices, const unsigned* bases, int draws), (mode, count, type, indices, instances, vertices, bases, draws))
#undef EXTERNAL_DRAW

extern "C" void bionic_glGetTexParameteriv(unsigned target, unsigned pname, int* params) {
    using Fn = void (*)(unsigned, unsigned, int*);
    if (auto f = reinterpret_cast<Fn>(GL_FUNC_CACHED("glGetTexParameteriv")))
        f(surface_texture::target(target), pname, params);
    surface_texture::restore_binding(target);
}
extern "C" void bionic_glGetTexParameterfv(unsigned target, unsigned pname, float* params) {
    using Fn = void (*)(unsigned, unsigned, float*);
    if (auto f = reinterpret_cast<Fn>(GL_FUNC_CACHED("glGetTexParameterfv")))
        f(surface_texture::target(target), pname, params);
    surface_texture::restore_binding(target);
}
extern "C" void bionic_glVertexAttribDivisor(unsigned int index, unsigned int divisor) {
    typedef void (*PFN)(unsigned int, unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glVertexAttribDivisor");
    if (!f) f = (PFN)GL_FUNC_CACHED("glVertexAttribDivisorANGLE");
    if (f) f(index, divisor);
}

// ── GLES 3.0: Buffer Mapping ────────────────────────────────────────────────
extern "C" void* bionic_glMapBufferRange(unsigned int target, long offset, long length, unsigned int access) {
    typedef void* (*PFN)(unsigned int, long, long, unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glMapBufferRange");
    if (f) return f(target, offset, length, access);
    return nullptr;
}
extern "C" unsigned char bionic_glUnmapBuffer(unsigned int target) {
    typedef unsigned char (*PFN)(unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glUnmapBuffer");
    if (f) return f(target);
    return 0;
}
extern "C" void bionic_glFlushMappedBufferRange(unsigned int target, long offset, long length) {
    typedef void (*PFN)(unsigned int, long, long);
    auto f = (PFN)GL_FUNC_CACHED("glFlushMappedBufferRange");
    if (f) f(target, offset, length);
}

// Guest code follows Linux AAPCS64: every stack-passed argument owns an 8-byte slot. Apple
// arm64 packs stack arguments at natural size, so a 4-byte stack argument followed by another
// 4-byte one would be read at the wrong offset. GL_STACK_PAD adds the missing 4 bytes after
// each 4-byte stack argument so these wrappers (and only them) read the guest layout.
#if defined(__APPLE__) && defined(__aarch64__)
#define GL_STACK_PAD , unsigned int /* unused slot half */
#else
#define GL_STACK_PAD
#endif

// ── GLES 3.0: 3D Textures & Immutable Storage & Samplers ────────────────────
extern "C" void bionic_glTexImage3D(unsigned int target, int level, int internalformat, int width, int height, int depth, int border, unsigned int format, unsigned int type, const void* pixels) {
    typedef void (*PFN)(unsigned int, int, int, int, int, int, int, unsigned int, unsigned int, const void*);
    auto f = (PFN)GL_FUNC_CACHED("glTexImage3D");
    if (f) f(target, level, internalformat, width, height, depth, border, format, type, pixels);
}
extern "C" void bionic_glTexSubImage3D(unsigned int target, int level, int xoffset, int yoffset, int zoffset, int width, int height, int depth, unsigned int format GL_STACK_PAD, unsigned int type GL_STACK_PAD, const void* pixels) {
    typedef void (*PFN)(unsigned int, int, int, int, int, int, int, int, unsigned int, unsigned int, const void*);
    auto f = (PFN)GL_FUNC_CACHED("glTexSubImage3D");
    if (f) f(target, level, xoffset, yoffset, zoffset, width, height, depth, format, type, pixels);
}
extern "C" void bionic_glTexSubImage3DOES(unsigned int target, int level, int xoffset, int yoffset, int zoffset, int width, int height, int depth, unsigned int format GL_STACK_PAD, unsigned int type GL_STACK_PAD, const void* pixels) {
    typedef void (*PFN)(unsigned int, int, int, int, int, int, int, int, unsigned int, unsigned int, const void*);
    auto f = (PFN)GL_FUNC_CACHED("glTexSubImage3DOES");
    if (f) f(target, level, xoffset, yoffset, zoffset, width, height, depth, format, type, pixels);
}
extern "C" void bionic_glCompressedTexSubImage3D(unsigned int target, int level, int xoffset, int yoffset, int zoffset, int width, int height, int depth, unsigned int format GL_STACK_PAD, int imageSize GL_STACK_PAD, const void* data) {
    typedef void (*PFN)(unsigned int, int, int, int, int, int, int, int, unsigned int, int, const void*);
    auto f = (PFN)GL_FUNC_CACHED("glCompressedTexSubImage3D");
    if (f) f(target, level, xoffset, yoffset, zoffset, width, height, depth, format, imageSize, data);
}
extern "C" void bionic_glCompressedTexSubImage3DOES(unsigned int target, int level, int xoffset, int yoffset, int zoffset, int width, int height, int depth, unsigned int format GL_STACK_PAD, int imageSize GL_STACK_PAD, const void* data) {
    typedef void (*PFN)(unsigned int, int, int, int, int, int, int, int, unsigned int, int, const void*);
    auto f = (PFN)GL_FUNC_CACHED("glCompressedTexSubImage3DOES");
    if (f) f(target, level, xoffset, yoffset, zoffset, width, height, depth, format, imageSize, data);
}
extern "C" void bionic_glTexStorage2D(unsigned int target, int levels, unsigned int internalformat, int width, int height) {
    typedef void (*PFN)(unsigned int, int, unsigned int, int, int);
    auto f = (PFN)GL_FUNC_CACHED("glTexStorage2D");
    if (f) f(target, levels, internalformat, width, height);
}
extern "C" void bionic_glCopyImageSubData(unsigned int srcName, unsigned int srcTarget, int srcLevel, int srcX, int srcY, int srcZ, unsigned int dstName, unsigned int dstTarget, int dstLevel GL_STACK_PAD, int dstX GL_STACK_PAD, int dstY GL_STACK_PAD, int dstZ GL_STACK_PAD, int width GL_STACK_PAD, int height GL_STACK_PAD, int depth) {
    using PFN = void (*)(unsigned int, unsigned int, int, int, int, int, unsigned int, unsigned int, int, int, int, int, int, int, int);
    auto f = reinterpret_cast<PFN>(GL_FUNC_CACHED("glCopyImageSubData"));
    if (f) f(srcName, srcTarget, srcLevel, srcX, srcY, srcZ, dstName, dstTarget, dstLevel, dstX, dstY, dstZ, width, height, depth);
}
extern "C" void bionic_glCopyImageSubDataEXT(unsigned int srcName, unsigned int srcTarget, int srcLevel, int srcX, int srcY, int srcZ, unsigned int dstName, unsigned int dstTarget, int dstLevel GL_STACK_PAD, int dstX GL_STACK_PAD, int dstY GL_STACK_PAD, int dstZ GL_STACK_PAD, int width GL_STACK_PAD, int height GL_STACK_PAD, int depth) {
    using PFN = void (*)(unsigned int, unsigned int, int, int, int, int, unsigned int, unsigned int, int, int, int, int, int, int, int);
    auto f = reinterpret_cast<PFN>(GL_FUNC_CACHED("glCopyImageSubDataEXT"));
    if (f) f(srcName, srcTarget, srcLevel, srcX, srcY, srcZ, dstName, dstTarget, dstLevel, dstX, dstY, dstZ, width, height, depth);
}
extern "C" void bionic_glCopyImageSubDataOES(unsigned int srcName, unsigned int srcTarget, int srcLevel, int srcX, int srcY, int srcZ, unsigned int dstName, unsigned int dstTarget, int dstLevel GL_STACK_PAD, int dstX GL_STACK_PAD, int dstY GL_STACK_PAD, int dstZ GL_STACK_PAD, int width GL_STACK_PAD, int height GL_STACK_PAD, int depth) {
    using PFN = void (*)(unsigned int, unsigned int, int, int, int, int, unsigned int, unsigned int, int, int, int, int, int, int, int);
    auto f = reinterpret_cast<PFN>(GL_FUNC_CACHED("glCopyImageSubDataOES"));
    if (f) f(srcName, srcTarget, srcLevel, srcX, srcY, srcZ, dstName, dstTarget, dstLevel, dstX, dstY, dstZ, width, height, depth);
}
extern "C" void bionic_glTexStorage3D(unsigned int target, int levels, unsigned int internalformat, int width, int height, int depth) {
    typedef void (*PFN)(unsigned int, int, unsigned int, int, int, int);
    auto f = (PFN)GL_FUNC_CACHED("glTexStorage3D");
    if (f) f(target, levels, internalformat, width, height, depth);
}
extern "C" void bionic_glGenSamplers(int count, unsigned int* samplers) {
    typedef void (*PFN)(int, unsigned int*);
    auto f = (PFN)GL_FUNC_CACHED("glGenSamplers");
    if (f) f(count, samplers);
}
extern "C" void bionic_glBindSampler(unsigned int unit, unsigned int sampler) {
    typedef void (*PFN)(unsigned int, unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glBindSampler");
    if (f) f(unit, sampler);
}
extern "C" void bionic_glDeleteSamplers(int count, const unsigned int* samplers) {
    typedef void (*PFN)(int, const unsigned int*);
    auto f = (PFN)GL_FUNC_CACHED("glDeleteSamplers");
    if (f) f(count, samplers);
}
extern "C" void bionic_glSamplerParameteri(unsigned int sampler, unsigned int pname, int param) {
    typedef void (*PFN)(unsigned int, unsigned int, int);
    auto f = (PFN)GL_FUNC_CACHED("glSamplerParameteri");
    if (f) f(sampler, pname, param);
}
extern "C" void bionic_glSamplerParameterf(unsigned int sampler, unsigned int pname, float param) {
    typedef void (*PFN)(unsigned int, unsigned int, float);
    auto f = (PFN)GL_FUNC_CACHED("glSamplerParameterf");
    if (f) f(sampler, pname, param);
}

// ── GLES 3.0: Sync Objects & Fences ─────────────────────────────────────────
extern "C" void* bionic_glFenceSync(unsigned int condition, unsigned int flags) {
    typedef void* (*PFN)(unsigned int, unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glFenceSync");
    if (f) return f(condition, flags);
    return nullptr;
}
extern "C" unsigned int bionic_glClientWaitSync(void* sync, unsigned int flags, uint64_t timeout) {
    typedef unsigned int (*PFN)(void*, unsigned int, uint64_t);
    auto f = (PFN)GL_FUNC_CACHED("glClientWaitSync");
    if (f) return f(sync, flags, timeout);
    return 0;
}
extern "C" void bionic_glWaitSync(void* sync, unsigned int flags, uint64_t timeout) {
    typedef void (*PFN)(void*, unsigned int, uint64_t);
    auto f = (PFN)GL_FUNC_CACHED("glWaitSync");
    if (f) f(sync, flags, timeout);
}
extern "C" void bionic_glDeleteSync(void* sync) {
    typedef void (*PFN)(void*);
    auto f = (PFN)GL_FUNC_CACHED("glDeleteSync");
    if (f) f(sync);
}
extern "C" unsigned char bionic_glIsSync(void* sync) {
    typedef unsigned char (*PFN)(void*);
    auto f = (PFN)GL_FUNC_CACHED("glIsSync");
    if (f) return f(sync);
    return 0;
}

// ── GLES 3.0: MRT & Blit Framebuffer ────────────────────────────────────────
extern "C" void bionic_glDrawBuffers(int n, const unsigned int* bufs) {
    typedef void (*PFN)(int, const unsigned int*);
    auto f = (PFN)GL_FUNC_CACHED("glDrawBuffers");
    if (f) f(n, bufs);
}
extern "C" void bionic_glReadBuffer(unsigned int src) {
    typedef void (*PFN)(unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glReadBuffer");
    if (f) f(src);
}
extern "C" void bionic_glBlitFramebuffer(int srcX0, int srcY0, int srcX1, int srcY1, int dstX0, int dstY0, int dstX1, int dstY1, unsigned int mask GL_STACK_PAD, unsigned int filter) {
    typedef void (*PFN)(int, int, int, int, int, int, int, int, unsigned int, unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glBlitFramebuffer");
    if (f) f(srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1, dstY1, mask, filter);
}
extern "C" void bionic_glBlitFramebufferNV(int srcX0, int srcY0, int srcX1, int srcY1, int dstX0, int dstY0, int dstX1, int dstY1, unsigned int mask GL_STACK_PAD, unsigned int filter) {
    typedef void (*PFN)(int, int, int, int, int, int, int, int, unsigned int, unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glBlitFramebufferNV");
    if (f) f(srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1, dstY1, mask, filter);
}
extern "C" void bionic_glBlitFramebufferANGLE(int srcX0, int srcY0, int srcX1, int srcY1, int dstX0, int dstY0, int dstX1, int dstY1, unsigned int mask GL_STACK_PAD, unsigned int filter) {
    typedef void (*PFN)(int, int, int, int, int, int, int, int, unsigned int, unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glBlitFramebufferANGLE");
    if (f) f(srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1, dstY1, mask, filter);
}
extern "C" void bionic_glRenderbufferStorageMultisample(unsigned int target, int samples, unsigned int internalformat, int width, int height) {
    typedef void (*PFN)(unsigned int, int, unsigned int, int, int);
    auto f = (PFN)GL_FUNC_CACHED("glRenderbufferStorageMultisample");
    if (f) f(target, samples, internalformat, width, height);
}

// ── GLES 3.0/3.1: Uniform Buffer Objects (UBO) & SSBO ───────────────────────
extern "C" unsigned int bionic_glGetUniformBlockIndex(unsigned int program, const char* uniformBlockName) {
    typedef unsigned int (*PFN)(unsigned int, const char*);
    auto f = (PFN)GL_FUNC_CACHED("glGetUniformBlockIndex");
    if (f) return f(program, uniformBlockName);
    return 0xFFFFFFFF;
}
extern "C" void bionic_glUniformBlockBinding(unsigned int program, unsigned int uniformBlockIndex, unsigned int uniformBlockBinding) {
    typedef void (*PFN)(unsigned int, unsigned int, unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glUniformBlockBinding");
    if (f) f(program, uniformBlockIndex, uniformBlockBinding);
}
extern "C" void bionic_glBindBufferBase(unsigned int target, unsigned int index, unsigned int buffer) {
    typedef void (*PFN)(unsigned int, unsigned int, unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glBindBufferBase");
    if (f) f(target, index, buffer);
}
extern "C" void bionic_glBindBufferRange(unsigned int target, unsigned int index, unsigned int buffer, long offset, long size) {
    typedef void (*PFN)(unsigned int, unsigned int, unsigned int, long, long);
    auto f = (PFN)GL_FUNC_CACHED("glBindBufferRange");
    if (f) f(target, index, buffer, offset, size);
}

// ── GLES 3.1: Compute Shaders ───────────────────────────────────────────────
extern "C" void bionic_glDispatchCompute(unsigned int num_groups_x, unsigned int num_groups_y, unsigned int num_groups_z) {
    typedef void (*PFN)(unsigned int, unsigned int, unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glDispatchCompute");
    if (f) f(num_groups_x, num_groups_y, num_groups_z);
}
extern "C" void bionic_glDispatchComputeIndirect(long indirect) {
    typedef void (*PFN)(long);
    auto f = (PFN)GL_FUNC_CACHED("glDispatchComputeIndirect");
    if (f) f(indirect);
}

// ── GLES 3.0: Transform Feedback ────────────────────────────────────────────
extern "C" void bionic_glGenTransformFeedbacks(int n, unsigned int* ids) {
    typedef void (*PFN)(int, unsigned int*);
    auto f = (PFN)GL_FUNC_CACHED("glGenTransformFeedbacks");
    if (f) f(n, ids);
}
extern "C" void bionic_glBindTransformFeedback(unsigned int target, unsigned int id) {
    typedef void (*PFN)(unsigned int, unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glBindTransformFeedback");
    if (f) f(target, id);
}
extern "C" void bionic_glDeleteTransformFeedbacks(int n, const unsigned int* ids) {
    typedef void (*PFN)(int, const unsigned int*);
    auto f = (PFN)GL_FUNC_CACHED("glDeleteTransformFeedbacks");
    if (f) f(n, ids);
}
extern "C" void bionic_glBeginTransformFeedback(unsigned int primitiveMode) {
    typedef void (*PFN)(unsigned int);
    auto f = (PFN)GL_FUNC_CACHED("glBeginTransformFeedback");
    if (f) f(primitiveMode);
}
extern "C" void bionic_glEndTransformFeedback(void) {
    typedef void (*PFN)(void);
    auto f = (PFN)GL_FUNC_CACHED("glEndTransformFeedback");
    if (f) f();
}

// Receive guest GLboolean registers at full width before calling the host ABI.
#define GL_BOOLEAN_ENTRY(name, guest, host, arguments) \
    extern "C" void bionic_##name guest { \
        using Fn = void (*) host; \
        if (auto f = reinterpret_cast<Fn>(GL_FUNC_CACHED(#name))) f arguments; \
    }
#include "GraphicsBooleanEntries.inc"
#undef GL_BOOLEAN_ENTRY

const SymbolEntry kGraphicsSymbols[] = {
#define GL_BOOLEAN_ENTRY(name, guest, host, arguments) {#name, reinterpret_cast<void*>(&bionic_##name)},
#include "GraphicsBooleanEntries.inc"
#undef GL_BOOLEAN_ENTRY
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
    {"glDeleteBuffers", reinterpret_cast<void*>(&bionic_glDeleteBuffers)},
    {"glBufferSubData", reinterpret_cast<void*>(&bionic_glBufferSubData)},
    {"glDrawElements", reinterpret_cast<void*>(&bionic_glDrawElements)},
    {"glDisableVertexAttribArray", reinterpret_cast<void*>(&bionic_glDisableVertexAttribArray)},
    {"glGenTextures", reinterpret_cast<void*>(&bionic_glGenTextures)},
    {"glBindTexture", reinterpret_cast<void*>(&bionic_glBindTexture)},
    {"glDeleteTextures", reinterpret_cast<void*>(&bionic_glDeleteTextures)},
    {"glTexImage2D", reinterpret_cast<void*>(&bionic_glTexImage2D)},
    {"glTexSubImage2D", reinterpret_cast<void*>(&bionic_glTexSubImage2D)},
    {"glTexParameteri", reinterpret_cast<void*>(&bionic_glTexParameteri)},
    {"glTexParameterf", reinterpret_cast<void*>(&bionic_glTexParameterf)},
    {"glTexParameteriv", reinterpret_cast<void*>(&bionic_glTexParameteriv)},
    {"glTexParameterfv", reinterpret_cast<void*>(&bionic_glTexParameterfv)},
    {"glActiveTexture", reinterpret_cast<void*>(&bionic_glActiveTexture)},
    {"glGenerateMipmap", reinterpret_cast<void*>(&bionic_glGenerateMipmap)},
    {"glPixelStorei", reinterpret_cast<void*>(&bionic_glPixelStorei)},
    {"glCompressedTexImage2D", reinterpret_cast<void*>(&bionic_glCompressedTexImage2D)},
    {"glCompressedTexSubImage2D", reinterpret_cast<void*>(&bionic_glCompressedTexSubImage2D)},
    {"glGenVertexArrays", reinterpret_cast<void*>(&bionic_glGenVertexArrays)},
    {"glBindVertexArray", reinterpret_cast<void*>(&bionic_glBindVertexArray)},
    {"glDeleteVertexArrays", reinterpret_cast<void*>(&bionic_glDeleteVertexArrays)},
    {"glGenVertexArraysOES", reinterpret_cast<void*>(&bionic_glGenVertexArraysOES)},
    {"glBindVertexArrayOES", reinterpret_cast<void*>(&bionic_glBindVertexArrayOES)},
    {"glDeleteVertexArraysOES", reinterpret_cast<void*>(&bionic_glDeleteVertexArraysOES)},
    {"glGenFramebuffers", reinterpret_cast<void*>(&bionic_glGenFramebuffers)},
    {"glBindFramebuffer", reinterpret_cast<void*>(&bionic_glBindFramebuffer)},
    {"glDeleteFramebuffers", reinterpret_cast<void*>(&bionic_glDeleteFramebuffers)},
    {"glFramebufferTexture2D", reinterpret_cast<void*>(&bionic_glFramebufferTexture2D)},
    {"glCheckFramebufferStatus", reinterpret_cast<void*>(&bionic_glCheckFramebufferStatus)},
    {"glGenRenderbuffers", reinterpret_cast<void*>(&bionic_glGenRenderbuffers)},
    {"glBindRenderbuffer", reinterpret_cast<void*>(&bionic_glBindRenderbuffer)},
    {"glDeleteRenderbuffers", reinterpret_cast<void*>(&bionic_glDeleteRenderbuffers)},
    {"glRenderbufferStorage", reinterpret_cast<void*>(&bionic_glRenderbufferStorage)},
    {"glFramebufferRenderbuffer", reinterpret_cast<void*>(&bionic_glFramebufferRenderbuffer)},
    {"glGetUniformLocation", reinterpret_cast<void*>(&bionic_glGetUniformLocation)},
    {"glGetActiveUniform", reinterpret_cast<void*>(&bionic_glGetActiveUniform)},
    {"glProgramBinary", reinterpret_cast<void*>(&bionic_glProgramBinary)},
    {"glProgramBinaryOES", reinterpret_cast<void*>(&bionic_glProgramBinaryOES)},
    {"glGetProgramBinary", reinterpret_cast<void*>(&bionic_glGetProgramBinary)},
    {"glGetProgramBinaryOES", reinterpret_cast<void*>(&bionic_glGetProgramBinaryOES)},
    {"glUniform1f", reinterpret_cast<void*>(&bionic_glUniform1f)},
    {"glUniform2f", reinterpret_cast<void*>(&bionic_glUniform2f)},
    {"glUniform3f", reinterpret_cast<void*>(&bionic_glUniform3f)},
    {"glUniform4f", reinterpret_cast<void*>(&bionic_glUniform4f)},
    {"glUniform1i", reinterpret_cast<void*>(&bionic_glUniform1i)},
    {"glUniform2i", reinterpret_cast<void*>(&bionic_glUniform2i)},
    {"glUniform3i", reinterpret_cast<void*>(&bionic_glUniform3i)},
    {"glUniform4i", reinterpret_cast<void*>(&bionic_glUniform4i)},
    {"glUniform1fv", reinterpret_cast<void*>(&bionic_glUniform1fv)},
    {"glUniform2fv", reinterpret_cast<void*>(&bionic_glUniform2fv)},
    {"glUniform3fv", reinterpret_cast<void*>(&bionic_glUniform3fv)},
    {"glUniform4fv", reinterpret_cast<void*>(&bionic_glUniform4fv)},
    {"glUniform1iv", reinterpret_cast<void*>(&bionic_glUniform1iv)},
    {"glUniform2iv", reinterpret_cast<void*>(&bionic_glUniform2iv)},
    {"glUniform3iv", reinterpret_cast<void*>(&bionic_glUniform3iv)},
    {"glUniform4iv", reinterpret_cast<void*>(&bionic_glUniform4iv)},
    {"glUniformMatrix2fv", reinterpret_cast<void*>(&bionic_glUniformMatrix2fv)},
    {"glUniformMatrix3fv", reinterpret_cast<void*>(&bionic_glUniformMatrix3fv)},
    {"glUniformMatrix4fv", reinterpret_cast<void*>(&bionic_glUniformMatrix4fv)},
    {"glEnable", reinterpret_cast<void*>(&bionic_glEnable)},
    {"glDisable", reinterpret_cast<void*>(&bionic_glDisable)},
    {"glIsEnabled", reinterpret_cast<void*>(&bionic_glIsEnabled)},
    {"glBlendFunc", reinterpret_cast<void*>(&bionic_glBlendFunc)},
    {"glBlendFuncSeparate", reinterpret_cast<void*>(&bionic_glBlendFuncSeparate)},
    {"glBlendEquation", reinterpret_cast<void*>(&bionic_glBlendEquation)},
    {"glBlendEquationSeparate", reinterpret_cast<void*>(&bionic_glBlendEquationSeparate)},
    {"glDepthFunc", reinterpret_cast<void*>(&bionic_glDepthFunc)},
    {"glDepthMask", reinterpret_cast<void*>(&bionic_glDepthMask)},
    {"glCullFace", reinterpret_cast<void*>(&bionic_glCullFace)},
    {"glFrontFace", reinterpret_cast<void*>(&bionic_glFrontFace)},
    {"glScissor", reinterpret_cast<void*>(&bionic_glScissor)},
    {"glColorMask", reinterpret_cast<void*>(&bionic_glColorMask)},
    {"glFlush", reinterpret_cast<void*>(&bionic_glFlush)},
    {"glFinish", reinterpret_cast<void*>(&bionic_glFinish)},
    {"glGetIntegerv", reinterpret_cast<void*>(&bionic_glGetIntegerv)},
    {"glGetFloatv", reinterpret_cast<void*>(&bionic_glGetFloatv)},
    {"glGetBooleanv", reinterpret_cast<void*>(&bionic_glGetBooleanv)},
    {"glGetString", reinterpret_cast<void*>(&bionic_glGetString)},
    {"glGetStringi", reinterpret_cast<void*>(&bionic_glGetStringi)},
    {"glGetError", reinterpret_cast<void*>(&bionic_glGetError)},
    {"glReadPixels", reinterpret_cast<void*>(&bionic_glReadPixels)},
    {"glDeleteShader", reinterpret_cast<void*>(&bionic_glDeleteShader)},
    {"glDeleteProgram", reinterpret_cast<void*>(&bionic_glDeleteProgram)},
    {"glDetachShader", reinterpret_cast<void*>(&bionic_glDetachShader)},
    {"glDrawArraysInstanced", reinterpret_cast<void*>(&bionic_glDrawArraysInstanced)},
    {"glDrawElementsInstanced", reinterpret_cast<void*>(&bionic_glDrawElementsInstanced)},
    {"glDrawArraysInstancedANGLE", reinterpret_cast<void*>(&bionic_glDrawArraysInstancedANGLE)},
    {"glDrawArraysInstancedEXT", reinterpret_cast<void*>(&bionic_glDrawArraysInstancedEXT)},
    {"glDrawElementsInstancedANGLE", reinterpret_cast<void*>(&bionic_glDrawElementsInstancedANGLE)},
    {"glDrawElementsInstancedEXT", reinterpret_cast<void*>(&bionic_glDrawElementsInstancedEXT)},
    {"glGetTexParameteriv", reinterpret_cast<void*>(&bionic_glGetTexParameteriv)},
    {"glGetTexParameterfv", reinterpret_cast<void*>(&bionic_glGetTexParameterfv)},
#define DRAW_ENTRY(name) {#name, reinterpret_cast<void*>(&bionic_##name)}
#define DRAW_ALIAS(name, suffix) {#name #suffix, reinterpret_cast<void*>(&bionic_##name##suffix)}
    DRAW_ENTRY(glDrawElementsBaseVertex), DRAW_ALIAS(glDrawElementsBaseVertex, OES), DRAW_ALIAS(glDrawElementsBaseVertex, EXT),
    DRAW_ENTRY(glDrawRangeElements),
    DRAW_ENTRY(glDrawRangeElementsBaseVertex), DRAW_ALIAS(glDrawRangeElementsBaseVertex, OES), DRAW_ALIAS(glDrawRangeElementsBaseVertex, EXT),
    DRAW_ENTRY(glDrawElementsInstancedBaseVertex), DRAW_ALIAS(glDrawElementsInstancedBaseVertex, OES), DRAW_ALIAS(glDrawElementsInstancedBaseVertex, EXT),
    DRAW_ALIAS(glDrawElementsInstancedBaseInstance, EXT),
    DRAW_ALIAS(glDrawArraysInstancedBaseInstance, EXT), DRAW_ALIAS(glDrawArraysInstancedBaseInstance, ANGLE),
    DRAW_ALIAS(glDrawElementsInstancedBaseVertexBaseInstance, EXT), DRAW_ALIAS(glDrawElementsInstancedBaseVertexBaseInstance, ANGLE),
    DRAW_ENTRY(glDrawArraysIndirect), DRAW_ENTRY(glDrawElementsIndirect),
    DRAW_ALIAS(glMultiDrawArrays, EXT), DRAW_ALIAS(glMultiDrawArrays, ANGLE),
    DRAW_ALIAS(glMultiDrawElements, EXT), DRAW_ALIAS(glMultiDrawElements, ANGLE),
    DRAW_ALIAS(glMultiDrawElementsBaseVertex, EXT),
    DRAW_ALIAS(glMultiDrawArraysIndirect, EXT), DRAW_ALIAS(glMultiDrawElementsIndirect, EXT),
    DRAW_ENTRY(glMultiDrawArraysInstancedANGLE), DRAW_ENTRY(glMultiDrawElementsInstancedANGLE),
    DRAW_ENTRY(glMultiDrawArraysInstancedBaseInstanceANGLE), DRAW_ENTRY(glMultiDrawElementsInstancedBaseVertexBaseInstanceANGLE),
#undef DRAW_ALIAS
#undef DRAW_ENTRY
    {"glVertexAttribDivisor", reinterpret_cast<void*>(&bionic_glVertexAttribDivisor)},
    {"glMapBufferRange", reinterpret_cast<void*>(&bionic_glMapBufferRange)},
    {"glUnmapBuffer", reinterpret_cast<void*>(&bionic_glUnmapBuffer)},
    {"glFlushMappedBufferRange", reinterpret_cast<void*>(&bionic_glFlushMappedBufferRange)},
    {"glTexImage3D", reinterpret_cast<void*>(&bionic_glTexImage3D)},
    {"glTexSubImage3D", reinterpret_cast<void*>(&bionic_glTexSubImage3D)},
    {"glTexSubImage3DOES", reinterpret_cast<void*>(&bionic_glTexSubImage3DOES)},
    {"glCompressedTexSubImage3D", reinterpret_cast<void*>(&bionic_glCompressedTexSubImage3D)},
    {"glCompressedTexSubImage3DOES", reinterpret_cast<void*>(&bionic_glCompressedTexSubImage3DOES)},
    {"glCopyImageSubData", reinterpret_cast<void*>(&bionic_glCopyImageSubData)},
    {"glCopyImageSubDataOES", reinterpret_cast<void*>(&bionic_glCopyImageSubDataOES)},
    {"glCopyImageSubDataEXT", reinterpret_cast<void*>(&bionic_glCopyImageSubDataEXT)},
    {"glTexStorage2D", reinterpret_cast<void*>(&bionic_glTexStorage2D)},
    {"glTexStorage3D", reinterpret_cast<void*>(&bionic_glTexStorage3D)},
    {"glGenSamplers", reinterpret_cast<void*>(&bionic_glGenSamplers)},
    {"glBindSampler", reinterpret_cast<void*>(&bionic_glBindSampler)},
    {"glDeleteSamplers", reinterpret_cast<void*>(&bionic_glDeleteSamplers)},
    {"glSamplerParameteri", reinterpret_cast<void*>(&bionic_glSamplerParameteri)},
    {"glSamplerParameterf", reinterpret_cast<void*>(&bionic_glSamplerParameterf)},
    {"glFenceSync", reinterpret_cast<void*>(&bionic_glFenceSync)},
    {"glClientWaitSync", reinterpret_cast<void*>(&bionic_glClientWaitSync)},
    {"glWaitSync", reinterpret_cast<void*>(&bionic_glWaitSync)},
    {"glDeleteSync", reinterpret_cast<void*>(&bionic_glDeleteSync)},
    {"glIsSync", reinterpret_cast<void*>(&bionic_glIsSync)},
    {"glDrawBuffers", reinterpret_cast<void*>(&bionic_glDrawBuffers)},
    {"glReadBuffer", reinterpret_cast<void*>(&bionic_glReadBuffer)},
    {"glBlitFramebuffer", reinterpret_cast<void*>(&bionic_glBlitFramebuffer)},
    {"glBlitFramebufferANGLE", reinterpret_cast<void*>(&bionic_glBlitFramebufferANGLE)},
    {"glBlitFramebufferNV", reinterpret_cast<void*>(&bionic_glBlitFramebufferNV)},
    {"glRenderbufferStorageMultisample", reinterpret_cast<void*>(&bionic_glRenderbufferStorageMultisample)},
    {"glGetUniformBlockIndex", reinterpret_cast<void*>(&bionic_glGetUniformBlockIndex)},
    {"glUniformBlockBinding", reinterpret_cast<void*>(&bionic_glUniformBlockBinding)},
    {"glBindBufferBase", reinterpret_cast<void*>(&bionic_glBindBufferBase)},
    {"glBindBufferRange", reinterpret_cast<void*>(&bionic_glBindBufferRange)},
    {"glDispatchCompute", reinterpret_cast<void*>(&bionic_glDispatchCompute)},
    {"glDispatchComputeIndirect", reinterpret_cast<void*>(&bionic_glDispatchComputeIndirect)},
    {"glGenTransformFeedbacks", reinterpret_cast<void*>(&bionic_glGenTransformFeedbacks)},
    {"glBindTransformFeedback", reinterpret_cast<void*>(&bionic_glBindTransformFeedback)},
    {"glDeleteTransformFeedbacks", reinterpret_cast<void*>(&bionic_glDeleteTransformFeedbacks)},
    {"glBeginTransformFeedback", reinterpret_cast<void*>(&bionic_glBeginTransformFeedback)},
    {"glEndTransformFeedback", reinterpret_cast<void*>(&bionic_glEndTransformFeedback)},
    {"vkGetInstanceProcAddr", reinterpret_cast<void*>(&bionic_vkGetInstanceProcAddr)},
    {"vkGetDeviceProcAddr", reinterpret_cast<void*>(&bionic_vkGetDeviceProcAddr)},
    {"vkCreateInstance", reinterpret_cast<void*>(&bionic_vkCreateInstance)},
    {"vkCreateAndroidSurfaceKHR", reinterpret_cast<void*>(&bionic_vkCreateAndroidSurfaceKHR)},
    {"vkEnumerateInstanceExtensionProperties", reinterpret_cast<void*>(&bionic_vkEnumerateInstanceExtensionProperties)},

    // Frame pacing, implemented in FramePacer.cpp.
    //
    // These were the six symbols that wedged ULTRAKILL. All of them bound to
    // kudroid_universal_dummy, which returns 0 — so AChoreographer_getInstance
    // answered NULL, Unity's pacer thread read that as "no Choreographer on this
    // thread" and exited without signalling the condition variable its render thread
    // was waiting on. The app did not crash; it stopped, with the log ending on the
    // dlsym that resolved the last of these.
    //
    // ANativeWindow_setFrameRate belongs with them rather than with the other
    // ANativeWindow entries: it sets the interval the Choreographer paces at, so its
    // state is the pacer's.
    {"AChoreographer_getInstance", reinterpret_cast<void*>(&bionic_AChoreographer_getInstance)},
    {"AChoreographer_postFrameCallback", reinterpret_cast<void*>(&bionic_AChoreographer_postFrameCallback)},
    {"AChoreographer_postFrameCallbackDelayed", reinterpret_cast<void*>(&bionic_AChoreographer_postFrameCallbackDelayed)},
    {"AChoreographer_postFrameCallback64", reinterpret_cast<void*>(&bionic_AChoreographer_postFrameCallback64)},
    {"AChoreographer_postFrameCallbackDelayed64", reinterpret_cast<void*>(&bionic_AChoreographer_postFrameCallbackDelayed64)},
    {"AChoreographer_registerRefreshRateCallback", reinterpret_cast<void*>(&bionic_AChoreographer_registerRefreshRateCallback)},
    {"AChoreographer_unregisterRefreshRateCallback", reinterpret_cast<void*>(&bionic_AChoreographer_unregisterRefreshRateCallback)},
    {"ANativeWindow_setFrameRate", reinterpret_cast<void*>(&bionic_ANativeWindow_setFrameRate)},
    {"ANativeWindow_setFrameRateWithChangeStrategy", reinterpret_cast<void*>(&bionic_ANativeWindow_setFrameRateWithChangeStrategy)},
};

} // namespace

const SymbolEntry* get_graphics_symbols(size_t* count) {
    if (count) {
        *count = sizeof(kGraphicsSymbols) / sizeof(SymbolEntry);
    }
    return kGraphicsSymbols;
}

} // namespace kudroid
