typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long size_t;
#define NULL ((void*)0)

// Android Log
int __android_log_print(int prio, const char* tag, const char* fmt, ...);
#define LOGI(...) __android_log_print(3, "GameConsumer", __VA_ARGS__)
#define LOGE(...) __android_log_print(6, "GameConsumer", __VA_ARGS__)

// Bionic GLES2/3 & EGL Types
typedef void* EGLDisplay;
typedef void* EGLConfig;
typedef void* EGLSurface;
typedef void* EGLContext;

// EGL
EGLDisplay eglGetDisplay(void*);
unsigned int eglInitialize(EGLDisplay, int*, int*);
int eglChooseConfig(EGLDisplay, const int*, EGLConfig*, int, int*);
EGLSurface eglCreatePbufferSurface(EGLDisplay, EGLConfig, const int*);
EGLContext eglCreateContext(EGLDisplay, EGLConfig, EGLContext, const int*);
unsigned int eglMakeCurrent(EGLDisplay, EGLSurface, EGLSurface, EGLContext);

// GLES FBO & Textures
void glGenTextures(int, unsigned int*);
void glBindTexture(unsigned int, unsigned int);
void glTexImage2D(unsigned int, int, int, int, int, int, unsigned int, unsigned int, const void*);
void glTexParameteri(unsigned int, unsigned int, int);
void glGenFramebuffers(int, unsigned int*);
void glBindFramebuffer(unsigned int, unsigned int);
void glFramebufferTexture2D(unsigned int, unsigned int, unsigned int, unsigned int, int);
unsigned int glCheckFramebufferStatus(unsigned int);
void glReadPixels(int, int, int, int, unsigned int, unsigned int, void*);

// Function imported from libengine_provider.so (via DT_NEEDED dynamic link)
int engine_render_frame(void);

int kudroid_test_main(void) {
    LOGI("=================================================");
    LOGI("🔗 [Multi-ELF OpenGL Test] Consumer .so Calling Provider .so");
    LOGI("=================================================");

    EGLDisplay dpy = eglGetDisplay(NULL);
    int maj = 0, min = 0;
    if (!eglInitialize(dpy, &maj, &min)) {
        LOGE("❌ eglInitialize failed");
        return 1;
    }
    LOGI("✔ EGL Initialized: version %d.%d", maj, min);

    const int configAttribs[] = {
        0x3033, 0x0001, // EGL_PBUFFER_BIT
        0x3040, 0x0040, // EGL_OPENGL_ES3_BIT
        0x3038
    };
    EGLConfig config = NULL;
    int numConfigs = 0;
    eglChooseConfig(dpy, configAttribs, &config, 1, &numConfigs);

    const int pbufAttribs[] = { 0x3057, 256, 0x3056, 256, 0x3038 };
    EGLSurface surf = eglCreatePbufferSurface(dpy, config, pbufAttribs);
    const int ctxAttribs[] = { 0x3098, 3, 0x3038 };
    EGLContext ctx = eglCreateContext(dpy, config, NULL, ctxAttribs);
    eglMakeCurrent(dpy, surf, surf, ctx);

    unsigned int tex = 0, fbo = 0;
    glGenTextures(1, &tex);
    glBindTexture(0x0DE1, tex);
    glTexParameteri(0x0DE1, 0x2801, 0x2601);
    glTexParameteri(0x0DE1, 0x2800, 0x2601);
    glTexImage2D(0x0DE1, 0, 0x1908, 256, 256, 0, 0x1908, 0x1401, NULL);

    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(0x8D40, fbo);
    glFramebufferTexture2D(0x8D40, 0x8CE0, 0x0DE1, tex, 0);

    if (glCheckFramebufferStatus(0x8D40) != 0x8CD5) {
        LOGE("❌ FBO incomplete");
        return 2;
    }
    LOGI("✔ [libgame_consumer.so] EGL & FBO setup complete");

    // GỌI HÀM TỪ THƯ VIỆN ĐỘNG LIÊN KẾT: libengine_provider.so
    LOGI("🚀 [libgame_consumer.so] Invoking engine_render_frame() in libengine_provider.so...");
    int code = engine_render_frame();
    LOGI("✔ [libgame_consumer.so] Returned from provider: code = %d", code);

    // Xác thực Pixel màu tam giác được vẽ bởi libengine_provider.so
    uint8_t topPix[4] = {0};
    glReadPixels(128, 200, 1, 1, 0x1908, 0x1401, topPix); // Đỉnh trên (Màu Đỏ)
    LOGI("🔍 Top Vertex Pixel (Red): RGBA=(%d, %d, %d, %d)",
         (int)topPix[0], (int)topPix[1], (int)topPix[2], (int)topPix[3]);

    uint8_t bgPix[4] = {0};
    glReadPixels(10, 10, 1, 1, 0x1908, 0x1401, bgPix); // Nền đen
    LOGI("🔍 Background Pixel (Black): RGBA=(%d, %d, %d, %d)",
         (int)bgPix[0], (int)bgPix[1], (int)bgPix[2], (int)bgPix[3]);

    if (code == 42 && topPix[0] > 200 && bgPix[0] == 0) {
        LOGI("🎉 SUCCESS: MULTI-ELF 2 .SO OPENGL LINKING & RENDERING PASSED 100%!");
        return 0;
    } else {
        LOGE("⚠ Multi-ELF rendering verification failed (code=%d)", code);
        return 3;
    }
}
