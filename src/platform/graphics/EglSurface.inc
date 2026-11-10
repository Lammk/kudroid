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
static std::atomic<uint64_t> g_drawCalls{0};
static std::atomic<uint64_t> g_drawAttempts{0};
static std::atomic<uint64_t> g_vertexSetups{0};
static std::atomic<uint64_t> g_clearCalls{0};
static std::atomic<uint64_t> g_shaderSources{0};
static std::atomic<uint64_t> g_programLinks{0};
static std::atomic<uint64_t> g_programBinds{0};
static std::atomic<uint64_t> g_bufferUploads{0};
static std::atomic<uint64_t> g_vertexPayloadCalls{0};
static std::atomic<uint64_t> g_indexPayloadCalls{0};
static std::atomic<uint64_t> g_bufferMapWrites{0};
static std::atomic<uint64_t> g_textureUploads{0};
static std::atomic<uint64_t> g_swapCalls{0};
static std::atomic<uint64_t> g_swapFailures{0};

static void trace_buffer_payload(const char* api, unsigned target, long offset,
                                 long size, const void* data, const void* caller) {
    if (size > 0 && data) {
        if (target == 0x8892 /* GL_ARRAY_BUFFER */)
            g_vertexPayloadCalls.fetch_add(1, std::memory_order_relaxed);
        else if (target == 0x8893 /* GL_ELEMENT_ARRAY_BUFFER */)
            g_indexPayloadCalls.fetch_add(1, std::memory_order_relaxed);
    }
    static std::atomic<unsigned> calls{0};
    if (calls.fetch_add(1, std::memory_order_relaxed) < 96)
        KLOG(kInfo, "KuDroidGPU", "GL buffer input: api=%s target=0x%x offset=%ld size=%ld data=%u caller=%p",
             api, target, offset, size, data != nullptr, caller);
}

static void trace_gl_label(const char* api, unsigned kind, unsigned id, int length,
                           const char* label, std::atomic<unsigned>& calls,
                           std::atomic<unsigned>& logged) {
    const unsigned call = calls.fetch_add(1, std::memory_order_relaxed);
    if (call >= 12 && call % 120 != 0) return;
    if (logged.fetch_add(1, std::memory_order_relaxed) >= 96) return;
    const int capacity = length < 0 ? (length == -1 ? 128 : 0) : std::min(length, 128);
    KLOG(kInfo, "KuDroidGPU", "GL label: api=%s kind=0x%x id=%u length=%d text='%.*s'",
         api, kind, id, length, capacity, label && capacity > 0 ? label : "");
}

extern "C" EGLSurface bionic_eglCreateWindowSurface(EGLDisplay dpy, EGLConfig config,
                                                    EGLNativeWindowType win,
                                                    const EGLint* attrib_list) {
    gpuLog("eglCreateWindowSurface: window=%p", (void*)win);
    auto host_func = (PFN_eglCreateWindowSurface)get_egl_func("eglCreateWindowSurface");
    auto host_destroy = (PFN_eglDestroySurface)get_egl_func("eglDestroySurface");
    if (host_func) {
        void* resolvedLayer = g_metalLayer;
        void* windowKey = nullptr;
        std::shared_ptr<SurfaceFrame> windowFrame;
        if (win) {
            auto* nw = static_cast<KuDroidNativeWindow*>(win);
            if (nw->magic == 0x4B554457 && nw->layer) {
                resolvedLayer = nw->layer;
                if (nw->frame) { windowKey = nw->frame.get(); windowFrame = nw->frame; }
            }
        }
#if defined(__APPLE__)
        if (!resolvedLayer) {
            resolvedLayer = get_or_create_fallback_metal_layer();
        }
#endif
        // ANGLE presents into a sublayer of its own; resolvedLayer stays the host
        // that ownership and canvas decisions are keyed on.
        auto resolveRenderLayer = [&]() -> void* {
#if defined(__APPLE__)
            if (windowFrame) return render_layer_for_host(resolvedLayer, &g_glLayers,
                "GL", windowKey, windowFrame);
#endif
            return gl_layer_for_host(resolvedLayer);
        };
        EGLNativeWindowType nativeWin = (EGLNativeWindowType)resolveRenderLayer();
        if (!nativeWin) return nullptr;
        gpuLog("eglCreateWindowSurface: calling ANGLE with layer=%p (host=%p)...", (void*)nativeWin, resolvedLayer);
#if defined(__APPLE__)
        void* pool = objc_autoreleasePoolPush();
        EGLSurface s = host_func(dpy, config, nativeWin, attrib_list);
        if (!s && s_activeEglSurface && host_destroy) {
            gpuLog("eglCreateWindowSurface failed, auto-recycling stale EGLSurface %p on layer %p...", s_activeEglSurface, (void*)nativeWin);
            host_destroy(s_activeEglDisplay ? s_activeEglDisplay : dpy, s_activeEglSurface);
            unregister_egl_layer(s_activeEglSurface);
            s_activeEglSurface = nullptr;
            nativeWin = (EGLNativeWindowType)resolveRenderLayer();
            s = host_func(dpy, config, nativeWin, attrib_list);
        }
        objc_autoreleasePoolPop(pool);
        gpuLog("eglCreateWindowSurface returned %p", (void*)s);
        if (s) {
            register_egl_layer(s, resolvedLayer, windowKey);
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
            nativeWin = (EGLNativeWindowType)resolveRenderLayer();
            s = host_func(dpy, config, nativeWin, attrib_list);
        }
        gpuLog("eglCreateWindowSurface returned %p", (void*)s);
        if (s) {
            register_egl_layer(s, resolvedLayer, windowKey);
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

// Logs the requested context version; does not touch the EGL error state.
static void log_context_request(const EGLint* attribs) {
    int major = 1, minor = 0;
    for (int i = 0; attribs && i < 64 && attribs[i] != 0x3038 /* EGL_NONE */; i += 2) {
        if (attribs[i] == 0x3098 /* EGL_CONTEXT_MAJOR_VERSION */) major = attribs[i + 1];
        else if (attribs[i] == 0x30FB /* EGL_CONTEXT_MINOR_VERSION */) minor = attribs[i + 1];
    }
    gpuLog("eglCreateContext: requested ES %d.%d", major, minor);
}

extern "C" EGLContext bionic_eglCreateContext(EGLDisplay dpy, EGLConfig config,
                                              EGLContext share_context,
                                              const EGLint* attrib_list) {
    auto f = eglFn<EGLContext(EGLDisplay, EGLConfig, EGLContext, const EGLint*)>("eglCreateContext");
    if (!f) { EGL_FORWARD_ERR("eglCreateContext", ""); return EGL_NO_CONTEXT; }
    log_context_request(attrib_list);
    // The driver stops at ES 3.0: a 3.1+ request for an opted-in package gets the
    // 3.0 context it can make, and the version strings are raised to match.
    EGLint patched[64];
    if (es31_compat_enabled() && attrib_list) {
        int n = 0;
        for (; n < 60 && attrib_list[n] != 0x3038 /* EGL_NONE */; n += 2) {
            patched[n] = attrib_list[n];
            patched[n + 1] = attrib_list[n + 1];
            if (attrib_list[n] == 0x30FB /* EGL_CONTEXT_MINOR_VERSION */ && attrib_list[n + 1] > 0)
                patched[n + 1] = 0;
        }
        if (attrib_list[n] == 0x3038) {
            patched[n] = 0x3038;
            attrib_list = patched;
        }
    }
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
    KLOG(kInfo, "KuDroidGPU", "GL draw backend: arrays=%p elements=%p arrays_instanced=%p elements_instanced=%p",
         GL_FUNC_CACHED("glDrawArrays"), GL_FUNC_CACHED("glDrawElements"),
         GL_FUNC_CACHED("glDrawArraysInstanced"), GL_FUNC_CACHED("glDrawElementsInstanced"));
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
    const uint64_t swapIndex = g_swapCalls.fetch_add(1, std::memory_order_relaxed);
    if (gpu_diag_enabled() && (swapIndex % 120) == 0) {
        gpu_diag_sample_backbuffer(dpy, surface, swapIndex);
    }
    const uint64_t swapT0 = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
    EGLBoolean r = f(dpy, surface);
    if (r) kudroid_note_frame_presented();
    const uint64_t failures = r ? g_swapFailures.load(std::memory_order_relaxed)
        : g_swapFailures.fetch_add(1, std::memory_order_relaxed) + 1;
    const uint64_t swapMs = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count()) -
        swapT0;
    gpuLog("eglSwapBuffers(surface=%p) -> %s", (void*)surface, r ? "true" : "false");
    if (r) {
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
    if (swapIndex < 3 || ((swapIndex + 1) % 120) == 0 || (!r && failures <= 3)) {
        // Presentation summaries must survive the per-call startup log limit.
        ::kudroid::log::write(::kudroid::log::kInfo, "KuDroidGPU",
            "frame census: swaps=%llu draws=%llu clears=%llu failures=%llu surface=%p result=%u duration_ms=%llu attempts=%llu vertices=%llu",
            (unsigned long long)(swapIndex + 1),
            (unsigned long long)g_drawCalls.load(std::memory_order_relaxed),
            (unsigned long long)g_clearCalls.load(std::memory_order_relaxed),
            (unsigned long long)failures, (void*)surface, (unsigned)r,
            (unsigned long long)swapMs,
            (unsigned long long)g_drawAttempts.load(std::memory_order_relaxed),
            (unsigned long long)g_vertexSetups.load(std::memory_order_relaxed));
        if (r) {
            using Query = EGLBoolean (*)(EGLDisplay, EGLSurface, EGLint, EGLint*);
            using Integer = void (*)(unsigned, int*);
            using String = const char* (*)(unsigned);
            auto query = reinterpret_cast<Query>(get_egl_func("eglQuerySurface"));
            auto integer = reinterpret_cast<Integer>(GL_FUNC_CACHED("glGetIntegerv"));
            auto string = reinterpret_cast<String>(GL_FUNC_CACHED("glGetString"));
            int width = 0, height = 0, viewport[4] = {}, scissor[4] = {};
            int framebuffer = 0, program = 0;
            if (query) {
                query(dpy, surface, 0x3057 /* EGL_WIDTH */, &width);
                query(dpy, surface, 0x3056 /* EGL_HEIGHT */, &height);
            }
            const char* version = string ? string(0x1f02 /* GL_VERSION */) : nullptr;
            if (integer && version && (std::strstr(version, "OpenGL ES 2.") ||
                                       std::strstr(version, "OpenGL ES 3."))) {
                integer(0x0ba2 /* GL_VIEWPORT */, viewport);
                integer(0x0c10 /* GL_SCISSOR_BOX */, scissor);
                integer(0x8ca6 /* GL_FRAMEBUFFER_BINDING */, &framebuffer);
                integer(0x8b8d /* GL_CURRENT_PROGRAM */, &program);
            }
            KLOG(kInfo, "KuDroidGPU",
                 "submission census: surface=%dx%d viewport=%d,%d,%d,%d scissor=%d,%d,%d,%d "
                 "fbo=%d program=%d sources=%llu links=%llu binds=%llu buffers=%llu textures=%llu "
                 "vertex_payload_calls=%llu index_payload_calls=%llu write_maps=%llu",
                 width, height, viewport[0], viewport[1], viewport[2], viewport[3],
                 scissor[0], scissor[1], scissor[2], scissor[3], framebuffer, program,
                 (unsigned long long)g_shaderSources.load(std::memory_order_relaxed),
                 (unsigned long long)g_programLinks.load(std::memory_order_relaxed),
                 (unsigned long long)g_programBinds.load(std::memory_order_relaxed),
                 (unsigned long long)g_bufferUploads.load(std::memory_order_relaxed),
                 (unsigned long long)g_textureUploads.load(std::memory_order_relaxed),
                 (unsigned long long)g_vertexPayloadCalls.load(std::memory_order_relaxed),
                 (unsigned long long)g_indexPayloadCalls.load(std::memory_order_relaxed),
                 (unsigned long long)g_bufferMapWrites.load(std::memory_order_relaxed));
        }
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
    if (name == 0x3055 /* EGL_EXTENSIONS */) s = egl_display_extensions(dpy, s);
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

