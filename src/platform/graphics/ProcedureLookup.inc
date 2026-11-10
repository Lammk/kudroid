extern "C" void bionic_glPushDebugGroup(unsigned source, unsigned id, int length, const char* message);
extern "C" void bionic_glObjectLabel(unsigned identifier, unsigned name, int length, const char* label);

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
    // Trace optional entry points only when the backend provides them.
    if (addr && std::strcmp(procname, "glPushDebugGroup") == 0)
        return reinterpret_cast<void*>(&bionic_glPushDebugGroup);
    if (addr && std::strcmp(procname, "glObjectLabel") == 0)
        return reinterpret_cast<void*>(&bionic_glObjectLabel);
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

