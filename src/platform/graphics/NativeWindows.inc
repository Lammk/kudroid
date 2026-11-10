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
struct SurfaceFrame {
    std::mutex mutex;
    int x = 0, y = 0, width = 0, height = 0;
    std::atomic<bool> active{true};
};

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
    std::shared_ptr<SurfaceFrame> frame;

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
static void sync_surface_frame(void* key, const std::shared_ptr<SurfaceFrame>& frame);
static void drop_surface_layers(std::shared_ptr<SurfaceFrame> frame);

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

extern "C" int64_t kudroid_surface_set_frame(int64_t handle, int x, int y, int width, int height) {
    if (width <= 0 || height <= 0) return handle;
    auto* nw = reinterpret_cast<KuDroidNativeWindow*>(handle);
    if (!nw) {
        nw = new KuDroidNativeWindow();
        nw->frame = std::make_shared<SurfaceFrame>();
        nw->layer = g_metalLayer;
        register_native_window(nw);
    }
    if (nw->magic != 0x4B554457 || !nw->frame) return handle;
    {
        std::lock_guard<std::mutex> lock(nw->frame->mutex);
        if (nw->frame->x == x && nw->frame->y == y &&
            nw->frame->width == width && nw->frame->height == height) return reinterpret_cast<int64_t>(nw);
        const bool followsFrame = nw->frame->width == 0 ||
            (nw->width == nw->frame->width && nw->height == nw->frame->height);
        nw->frame->x = x; nw->frame->y = y;
        nw->frame->width = width; nw->frame->height = height;
        if (followsFrame) { nw->width = width; nw->height = height; }
    }
    sync_surface_frame(nw->frame.get(), nw->frame);
    return reinterpret_cast<int64_t>(nw);
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
                        if (!nw->frame) {
                            nw->width = width;
                            nw->height = height;
                        }
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
        if (!nw->frame) {
            if (width > 0) nw->width = width;
            if (height > 0) nw->height = height;
        }
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
        if (nw->frame) continue;
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
        if (nw->magic == 0x4B554457) {
            std::unique_lock<std::mutex> lock;
            if (nw->frame) lock = std::unique_lock<std::mutex>(nw->frame->mutex);
            return nw->width > 0 ? nw->width : 1080;
        }
    }
    return g_metalLayerWidth > 0 ? g_metalLayerWidth : 1080;
}

extern "C" int bionic_ANativeWindow_getHeight(void* window) {
    if (window) {
        auto* nw = static_cast<KuDroidNativeWindow*>(window);
        if (nw->magic == 0x4B554457) {
            std::unique_lock<std::mutex> lock;
            if (nw->frame) lock = std::unique_lock<std::mutex>(nw->frame->mutex);
            return nw->height > 0 ? nw->height : 1920;
        }
    }
    return g_metalLayerHeight > 0 ? g_metalLayerHeight : 1920;
}

extern "C" int bionic_ANativeWindow_getFormat(void* window) {
    if (window) {
        auto* nw = static_cast<KuDroidNativeWindow*>(window);
        if (nw->magic == 0x4B554457) {
            std::unique_lock<std::mutex> lock;
            if (nw->frame) lock = std::unique_lock<std::mutex>(nw->frame->mutex);
            return nw->format;
        }
    }
    return 1; // WINDOW_FORMAT_RGBA_8888
}

extern "C" int bionic_ANativeWindow_setBuffersGeometry(void* window, int width, int height, int format) {
    if (window) {
        auto* nw = static_cast<KuDroidNativeWindow*>(window);
        if (nw->magic == 0x4B554457) {
            std::unique_lock<std::mutex> lock;
            if (nw->frame) lock = std::unique_lock<std::mutex>(nw->frame->mutex);
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
            if (nw->frame) {
                nw->frame->active.store(false);
                drop_surface_layers(nw->frame);
            }
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
std::unordered_map<void*, void*> g_eglKeys;
std::unordered_map<void*, void*> g_renderHosts;
// Host layer -> the sublayer GL presents into (see gl_layer_for_host).
using RenderLayers = std::unordered_map<void*, void*>;
RenderLayers g_glLayers;
RenderLayers g_vulkanLayers;

bool hardware_owns_layer(void* layer) {
    if (s_vulkanLayer && s_vulkanLayer == layer) return true;
    for (const auto& surface : g_surfaceLayers) {
        auto host = g_renderHosts.find(surface.layer);
        if (surface.surface && host != g_renderHosts.end() && host->second == layer) return true;
    }
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

static void register_egl_layer(void* surface, void* layer, void* key = nullptr) {
    {
        std::lock_guard<std::mutex> lock(g_surfaceLayersMtx);
        g_eglLayers[surface] = layer;
        g_eglKeys[surface] = key ? key : layer;
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
        host = g_eglKeys[surface];
        g_eglLayers.erase(it);
        g_eglKeys.erase(surface);
        for (const auto& entry : g_eglKeys) {
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
static std::unordered_map<void*, std::shared_ptr<SurfaceFrame>> g_layerFrames;

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
    double bx = 0.0, by = 0.0;
    std::shared_ptr<SurfaceFrame> frame;
    {
        std::lock_guard<std::mutex> lock(g_surfaceLayersMtx);
        auto it = g_layerFrames.find(child);
        if (it != g_layerFrames.end()) frame = it->second;
    }
    if (frame) {
        std::lock_guard<std::mutex> lock(frame->mutex);
        bx = frame->x / scale; by = frame->y / scale;
        bw = frame->width / scale; bh = frame->height / scale;
    }
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
    setRect(child, sel_registerName("setFrame:"), CGRectMake(bx, by, bw, bh));
    setFloat(child, sel_registerName("setContentsScale:"), scale);
    setSize(child, sel_registerName("setDrawableSize:"), CGSizeMake(bw * scale, bh * scale));
    if (clsTxn) voidCall(reinterpret_cast<id>(clsTxn), sel_registerName("commit"));
}

// Main thread only.
static void render_layer_create_on_main(void* hostPtr, void* key, RenderLayers* layers,
        const char* renderer, const std::shared_ptr<SurfaceFrame>& frame) {
    if (frame && !frame->active.load()) return;
    {
        std::lock_guard<std::mutex> lock(g_surfaceLayersMtx);
        if (layers->count(key)) return;
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
    if (frame) {
        std::lock_guard<std::mutex> lock(g_surfaceLayersMtx);
        g_layerFrames[child] = frame;
    }
    render_layer_sync_geometry(child, host, g_metalLayerWidth, g_metalLayerHeight, g_metalLayerDensity);
    setId(host, sel_registerName("addSublayer:"), child);
    {
        std::lock_guard<std::mutex> lock(g_surfaceLayersMtx);
        (*layers)[key] = child;
        g_renderHosts[child] = hostPtr;
    }
    gpuLog("%s layer %p attached under host %p", renderer, (void*)child, hostPtr);
}

// Create each renderer's child layer on the host UI thread.
static void* render_layer_for_host(void* host, RenderLayers* layers, const char* renderer,
        void* key = nullptr, std::shared_ptr<SurfaceFrame> frame = nullptr) {
    if (!host) return nullptr;
    if (!key) key = host;
    auto existing = [key, layers]() -> void* {
        std::lock_guard<std::mutex> lock(g_surfaceLayersMtx);
        auto it = layers->find(key);
        return it == layers->end() ? nullptr : it->second;
    };
    if (void* child = existing()) return child;
    if (pthread_main_np()) {
        render_layer_create_on_main(host, key, layers, renderer, frame);
    } else {
        dispatch_semaphore_t done = dispatch_semaphore_create(0);
        dispatch_async(dispatch_get_main_queue(), ^{
            render_layer_create_on_main(host, key, layers, renderer, frame);
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
        g_layerFrames.erase(child);
        g_renderHosts.erase(child);
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

static void drop_surface_layers(std::shared_ptr<SurfaceFrame> frame) {
    auto drop = ^{
        void* key = frame.get();
        drop_render_layer_for_host(key, &g_glLayers);
        drop_render_layer_for_host(key, &g_vulkanLayers);
    };
    if (pthread_main_np()) drop();
    else dispatch_async(dispatch_get_main_queue(), drop);
}

static void sync_surface_frame(void* key, const std::shared_ptr<SurfaceFrame>& frame) {
    const auto retainedFrame = frame;
    auto apply = ^{
        if (!retainedFrame->active.load()) return;
        std::vector<void*> children;
        {
            std::lock_guard<std::mutex> lock(g_surfaceLayersMtx);
            for (auto* layers : {&g_glLayers, &g_vulkanLayers}) {
                auto it = layers->find(key);
                if (it != layers->end()) children.push_back(it->second);
            }
        }
        for (void* child : children) {
            render_layer_sync_geometry(reinterpret_cast<id>(child), reinterpret_cast<id>(g_metalLayer),
                g_metalLayerWidth, g_metalLayerHeight, g_metalLayerDensity);
        }
    };
    if (pthread_main_np()) apply();
    else dispatch_async(dispatch_get_main_queue(), apply);
}

// Follows the host through resize/rotation. Called by kudroid_set_metal_layer.
extern "C" void kudroid_gpu_sync_gl_layers(void* host, int w, int h, float density) {
    if (!host) return;
    auto apply = ^{
        std::vector<void*> children;
        {
            std::lock_guard<std::mutex> lock(g_surfaceLayersMtx);
            for (const auto& entry : g_glLayers) children.push_back(entry.second);
        }
        for (void* gl : children)
            render_layer_sync_geometry(reinterpret_cast<id>(gl), reinterpret_cast<id>(host), w, h, density);
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
            g_renderHosts[vk_layer] = hostPtr;
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
    static std::atomic<uint64_t> s_blitGeneration{0};
    uint64_t gen = ++s_blitGeneration;
    gpuLog("blit layer=%p %dx%d gen=%llu", layer, width, height, (unsigned long long)gen);

    static CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
    if (!colorSpace) return;

    // Snapshot: the provider does not copy, and the async setContents below
    // runs later on the main thread — a realloc of s_canvasBits in between
    // would make the provider read freed memory. The copy is owned by the
    // provider and freed by its release callback.
    const size_t rowBytes = static_cast<size_t>(width) * 4;
    const size_t byteCount = rowBytes * static_cast<size_t>(height);
    void* snapshot = malloc(byteCount);
    if (!snapshot) return;
    std::memcpy(snapshot, bits, byteCount);
    CGDataProviderRef provider = CGDataProviderCreateWithData(
        snapshot, snapshot, byteCount, [](void* info, const void*, size_t) {
            std::free(info);
        });
    if (!provider) return;

    CGImageRef image = CGImageCreate(
        width, height, 8, 32, width * 4,
        colorSpace,
        kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big,
        provider, nullptr, false, kCGRenderingIntentDefault
    );

    CGDataProviderRelease(provider);

    if (!image) return;

    // Assign CGImage directly to CALayer/CAMetalLayer on Main Thread
    dispatch_async(dispatch_get_main_queue(), ^{
        if (gen < s_blitGeneration.load(std::memory_order_relaxed)) {
            CGImageRelease(image);
            return;
        }
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
            kudroid_note_frame_presented();
            gpuLog("canvas submitted layer=%p gen=%llu", layer, (unsigned long long)gen);
        }
        CGImageRelease(image);
    });
}
#else
static void sync_surface_frame(void*, const std::shared_ptr<SurfaceFrame>&) {}
static void drop_surface_layers(std::shared_ptr<SurfaceFrame>) {}
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

