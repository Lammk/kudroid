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

// Initialize the selected backend on the main thread before guest rendering.
extern "C" void kudroid_gpu_warmup_egl(void) {
#if defined(KUDROID_GLES_USE_VULKAN) && KUDROID_GLES_USE_VULKAN
    // Startup probes finish before any guest EGL calls on this thread.
    struct ProbeError {
        int (*getError)();
        ~ProbeError() {
            if (getError) {
                const int error = getError();
                if (error != 0x3000) gpuLog("warmup: EGL error=0x%x", error);
            }
        }
    } probeError{reinterpret_cast<int (*)()>(get_egl_func("eglGetError"))};
#endif
    // Full EGL typedefs not available here (declared later in file) — use local.
    typedef void* EGLConfig;
    typedef void* EGLSurface;
    typedef void* EGLContext;
    typedef unsigned int (*PFN_eglInitialize)(EGLDisplay, EGLint*, EGLint*);
    typedef int (*PFN_eglChooseConfig)(EGLDisplay, const EGLint*, EGLConfig*, EGLint, EGLint*);
    typedef EGLSurface (*PFN_eglCreatePbufferSurface)(EGLDisplay, EGLConfig, const EGLint*);
    typedef EGLContext (*PFN_eglCreateContext)(EGLDisplay, EGLConfig, EGLContext, const EGLint*);
    typedef unsigned int (*PFN_eglMakeCurrent)(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
    typedef unsigned int (*PFN_eglDestroySurface)(EGLDisplay, EGLSurface);
    typedef unsigned int (*PFN_eglDestroyContext)(EGLDisplay, EGLContext);

    auto get_display = &kudroid::egl_default_display;
    auto init = (PFN_eglInitialize) get_egl_func("eglInitialize");
    auto choose_config = (PFN_eglChooseConfig) get_egl_func("eglChooseConfig");
    auto create_pbuffer = (PFN_eglCreatePbufferSurface) get_egl_func("eglCreatePbufferSurface");
    auto create_context = (PFN_eglCreateContext) get_egl_func("eglCreateContext");
    auto make_current = (PFN_eglMakeCurrent) get_egl_func("eglMakeCurrent");
    auto destroy_surface = (PFN_eglDestroySurface) get_egl_func("eglDestroySurface");
    auto destroy_context = (PFN_eglDestroyContext) get_egl_func("eglDestroyContext");
    auto gl_clear = (void (*)(unsigned int)) GL_FUNC_CACHED("glClear");
    if (!get_display || !init || !choose_config || !create_pbuffer ||
        !create_context || !make_current || !destroy_surface || !destroy_context || !gl_clear) {
        gpuLog("warmup: egl entry points missing (get_display=%p init=%p "
               "choose_config=%p create_pbuffer=%p create_context=%p "
               "make_current=%p gl_clear=%p)",
               (void*)get_display, (void*)init, (void*)choose_config,
               (void*)create_pbuffer, (void*)create_context,
               (void*)make_current, (void*)gl_clear);
        return;
    }
    EGLDisplay dpy = get_display();
    if (!dpy) {
        gpuLog("warmup: eglGetDisplay -> NULL");
        return;
    }
    EGLint major = 0, minor = 0;
    if (!init(dpy, &major, &minor)) {
        gpuLog("warmup: eglInitialize -> false");
        return;
    }
    gpuLog("warmup: backend=%s eglInitialize -> true (major=%d minor=%d)",
           kudroid::egl_backend_name(), major, minor);

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
        destroy_surface(dpy, surface);
        return;
    }
    gpuLog("warmup: eglCreateContext -> %p", (void*)context);
    if (!make_current(dpy, surface, surface, context)) {
        gpuLog("warmup: eglMakeCurrent -> false");
        destroy_context(dpy, context);
        destroy_surface(dpy, surface);
        return;
    }
    gpuLog("warmup: eglMakeCurrent -> true");
    gl_clear(0x4000 /* GL_COLOR_BUFFER_BIT */);
    gpuLog("warmup: glClear OK");
#if defined(KUDROID_GLES_USE_VULKAN) && KUDROID_GLES_USE_VULKAN
    const std::string features = kudroid::probe_vulkan_features(dpy);
    KLOG(kInfo, "KuDroidGPU", "Vulkan backend features: %s", features.c_str());
    const std::string probe = kudroid::probe_gles31();
    KLOG(kInfo, "KuDroidGPU", "GLES backend probe: %s", probe.c_str());
#endif
    make_current(dpy, (EGLSurface)0, (EGLSurface)0, (EGLContext)0);
    destroy_context(dpy, context);
    destroy_surface(dpy, surface);
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

extern "C" void kudroid_gpu_prepare_egl(void) {
#if defined(KUDROID_GLES_USE_VULKAN) && KUDROID_GLES_USE_VULKAN
    static std::once_flag warmup;
    std::call_once(warmup, kudroid_gpu_warmup_egl);
#endif
}

extern "C" EGLDisplay bionic_eglGetDisplay(EGLNativeDisplayType display_id) {
    (void)display_id;
    gpuLog("eglGetDisplay(display_id=%p)", (void*)display_id);
    // Native windows are passed only when creating window surfaces.
    EGLDisplay dpy = kudroid::egl_default_display();
    gpuLog("eglGetDisplay: backend=%s -> %s (%p)", kudroid::egl_backend_name(),
           dpy ? "VALID" : "NULL", dpy);
    return dpy;
}
