typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long uint64_t;
typedef unsigned long size_t;
typedef unsigned long pthread_t;
#define NULL ((void*)0)

// Android Log
int __android_log_print(int prio, const char* tag, const char* fmt, ...);
#define LOGI(...) __android_log_print(3, "GpuThreadShared", __VA_ARGS__)
#define LOGE(...) __android_log_print(6, "GpuThreadShared", __VA_ARGS__)

// Pthread Bionic Shims
int pthread_create(pthread_t*, const void*, void* (*)(void*), void*);
int pthread_join(pthread_t, void**);

// EGL
typedef void* EGLDisplay;
typedef void* EGLConfig;
typedef void* EGLSurface;
typedef void* EGLContext;

EGLDisplay eglGetDisplay(void*);
unsigned int eglInitialize(EGLDisplay, int*, int*);
int eglChooseConfig(EGLDisplay, const int*, EGLConfig*, int, int*);
EGLSurface eglCreatePbufferSurface(EGLDisplay, EGLConfig, const int*);
EGLContext eglCreateContext(EGLDisplay, EGLConfig, EGLContext, const int*);
unsigned int eglMakeCurrent(EGLDisplay, EGLSurface, EGLSurface, EGLContext);

// GLES
unsigned int glCreateShader(unsigned int);
void glShaderSource(unsigned int, int, const char* const*, const int*);
void glCompileShader(unsigned int);
void glGetShaderiv(unsigned int, unsigned int, int*);
unsigned int glCreateProgram(void);
void glAttachShader(unsigned int, unsigned int);
void glLinkProgram(unsigned int);
void glUseProgram(unsigned int);
int glGetUniformLocation(unsigned int, const char*);
void glUniform1i(int, int);
void glViewport(int, int, int, int);
void glClearColor(float, float, float, float);
void glClear(unsigned int);
void glDrawArrays(unsigned int, int, int);
void glReadPixels(int, int, int, int, unsigned int, unsigned int, void*);
void glEnableVertexAttribArray(unsigned int);
void glVertexAttribPointer(unsigned int, int, unsigned int, unsigned char, int, const void*);

void glGenTextures(int, unsigned int*);
void glBindTexture(unsigned int, unsigned int);
void glTexImage2D(unsigned int, int, int, int, int, int, unsigned int, unsigned int, const void*);
void glTexParameteri(unsigned int, unsigned int, int);
void glGenerateMipmap(unsigned int);
void glGenFramebuffers(int, unsigned int*);
void glBindFramebuffer(unsigned int, unsigned int);
void glFramebufferTexture2D(unsigned int, unsigned int, unsigned int, unsigned int, int);
unsigned int glCheckFramebufferStatus(unsigned int);

void* glFenceSync(unsigned int, unsigned int);
unsigned int glClientWaitSync(void*, unsigned int, uint64_t);
void glDeleteSync(void*);

// Shared Context Worker Thread Data
struct WorkerData {
    EGLDisplay dpy;
    EGLConfig config;
    EGLContext mainCtx;
    unsigned int textureId;
    int success;
};

static void* worker_thread_fn(void* arg) {
    struct WorkerData* data = (struct WorkerData*)arg;
    LOGI("🧵 [Worker Thread] Background Thread started (pthread_self running)!");

    // 1. Tạo Pbuffer surface riêng cho worker thread
    const int pbufAttribs[] = { 0x3057, 16, 0x3056, 16, 0x3038 };
    EGLSurface workerSurf = eglCreatePbufferSurface(data->dpy, data->config, pbufAttribs);

    // 2. Tạo Shared Context chia sẻ tài nguyên với mainCtx
    const int ctxAttribs[] = { 0x3098, 3, 0x3038 }; // ES 3.0
    EGLContext workerCtx = eglCreateContext(data->dpy, data->config, data->mainCtx, ctxAttribs);
    if (!workerCtx) {
        LOGE("❌ [Worker Thread] Failed to create shared context!");
        data->success = 0;
        return NULL;
    }

    if (!eglMakeCurrent(data->dpy, workerSurf, workerSurf, workerCtx)) {
        LOGE("❌ [Worker Thread] eglMakeCurrent failed on worker thread!");
        data->success = 0;
        return NULL;
    }
    LOGI("✔ [Worker Thread] EGL Shared Context active on worker thread!");

    // 3. Nạp Texture ở luồng nền: Tạo ảnh 128x128 màu Vàng Neon (R=255, G=255, B=0, A=255)
    uint8_t imgData[128 * 128 * 4];
    for (int i = 0; i < 128 * 128; ++i) {
        imgData[i * 4 + 0] = 255; // R
        imgData[i * 4 + 1] = 255; // G
        imgData[i * 4 + 2] = 0;   // B
        imgData[i * 4 + 3] = 255; // A
    }

    glGenTextures(1, &data->textureId);
    glBindTexture(0x0DE1, data->textureId);
    glTexParameteri(0x0DE1, 0x2801, 0x2601); // GL_LINEAR
    glTexParameteri(0x0DE1, 0x2800, 0x2601); // GL_LINEAR
    glTexImage2D(0x0DE1, 0, 0x1908, 128, 128, 0, 0x1908, 0x1401, imgData);
    glGenerateMipmap(0x0DE1);

    // Đặt Fence Sync để đảm bảo GPU nạp xong texture
    void* fence = glFenceSync(0x9117, 0);
    glClientWaitSync(fence, 0x00000001, 1000000000ULL);
    glDeleteSync(fence);

    LOGI("✔ [Worker Thread] Texture (ID=%u) successfully loaded & mipmapped in background!", data->textureId);
    data->success = 1;
    return NULL;
}

int kudroid_test_main(void) {
    LOGI("=================================================");
    LOGI("🧵 [Multi-Threaded EGL Shared Context Texture Test]");
    LOGI("=================================================");

    EGLDisplay dpy = eglGetDisplay(NULL);
    int maj = 0, min = 0;
    eglInitialize(dpy, &maj, &min);

    const int configAttribs[] = {
        0x3033, 0x0001, // EGL_PBUFFER_BIT
        0x3040, 0x0040, // EGL_OPENGL_ES3_BIT
        0x3038
    };
    EGLConfig config = NULL;
    int numConfigs = 0;
    eglChooseConfig(dpy, configAttribs, &config, 1, &numConfigs);

    const int pbufAttribs[] = { 0x3057, 256, 0x3056, 256, 0x3038 };
    EGLSurface mainSurf = eglCreatePbufferSurface(dpy, config, pbufAttribs);
    const int ctxAttribs[] = { 0x3098, 3, 0x3038 };
    EGLContext mainCtx = eglCreateContext(dpy, config, NULL, ctxAttribs);
    eglMakeCurrent(dpy, mainSurf, mainSurf, mainCtx);
    LOGI("✔ [Main Thread] Main EGL Context active");

    // 1. Khởi chạy luồng nền để nạp Texture
    struct WorkerData workerData;
    workerData.dpy = dpy;
    workerData.config = config;
    workerData.mainCtx = mainCtx;
    workerData.textureId = 0;
    workerData.success = 0;

    pthread_t threadId = 0;
    LOGI("🚀 [Main Thread] Spawning Background Worker Thread for Texture Upload...");
    int rc = pthread_create(&threadId, NULL, worker_thread_fn, &workerData);
    if (rc != 0) {
        LOGE("❌ Failed to create worker pthread (rc=%d)", rc);
        return 1;
    }

    // 2. Chờ luồng nền hoàn tất
    pthread_join(threadId, NULL);
    if (!workerData.success || workerData.textureId == 0) {
        LOGE("❌ Worker thread failed to load shared texture");
        return 2;
    }
    LOGI("✔ [Main Thread] Worker Thread joined! Shared Texture ID = %u", workerData.textureId);

    // 3. Luồng chính tạo FBO và render bằng Texture mà luồng phụ vừa nạp
    unsigned int fboTex = 0, fbo = 0;
    glGenTextures(1, &fboTex);
    glBindTexture(0x0DE1, fboTex);
    glTexImage2D(0x0DE1, 0, 0x1908, 256, 256, 0, 0x1908, 0x1401, NULL);

    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(0x8D40, fbo);
    glFramebufferTexture2D(0x8D40, 0x8CE0, 0x0DE1, fboTex, 0);

    // Shader lấy mẫu Texture (Sampling)
    const char* vSrc =
        "#version 300 es\n"
        "layout(location = 0) in vec2 aPos;\n"
        "layout(location = 1) in vec2 aUV;\n"
        "out vec2 vUV;\n"
        "void main() {\n"
        "  gl_Position = vec4(aPos, 0.0, 1.0);\n"
        "  vUV = aUV;\n"
        "}\n";

    const char* fSrc =
        "#version 300 es\n"
        "precision mediump float;\n"
        "in vec2 vUV;\n"
        "uniform sampler2D uSampler;\n"
        "out vec4 fragColor;\n"
        "void main() {\n"
        "  fragColor = texture(uSampler, vUV);\n"
        "}\n";

    unsigned int vs = glCreateShader(0x8B31);
    const char* vsL[] = { vSrc };
    glShaderSource(vs, 1, vsL, NULL);
    glCompileShader(vs);

    unsigned int fs = glCreateShader(0x8B30);
    const char* fsL[] = { fSrc };
    glShaderSource(fs, 1, fsL, NULL);
    glCompileShader(fs);

    unsigned int prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glUseProgram(prog);

    int samplerLoc = glGetUniformLocation(prog, "uSampler");
    glUniform1i(samplerLoc, 0);

    // Bind Texture được tạo từ luồng phụ
    glBindTexture(0x0DE1, workerData.textureId);

    // Quad full màn hình FBO
    const float quad[] = {
        -1.0f,  1.0f,  0.0f, 1.0f,
        -1.0f, -1.0f,  0.0f, 0.0f,
         1.0f,  1.0f,  1.0f, 1.0f,
         1.0f, -1.0f,  1.0f, 0.0f
    };
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, 0x1406, 0, 4 * sizeof(float), quad);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, 0x1406, 0, 4 * sizeof(float), &quad[2]);

    glViewport(0, 0, 256, 256);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(0x4000);
    glDrawArrays(0x0005, 0, 4); // GL_TRIANGLE_STRIP

    // 4. Đọc Pixel để kiểm chứng: Màu phải là màu Vàng Neon do worker nạp (R=255, G=255, B=0, A=255)
    uint8_t pix[4] = {0};
    glReadPixels(128, 128, 1, 1, 0x1908, 0x1401, pix);
    LOGI("🔍 Rendered Sampled Pixel: RGBA=(%d, %d, %d, %d)",
         (int)pix[0], (int)pix[1], (int)pix[2], (int)pix[3]);

    if (pix[0] > 200 && pix[1] > 200 && pix[2] == 0) {
        LOGI("🎉 SUCCESS: MULTI-THREADED PTHREAD EGL SHARED CONTEXT TEST PASSED 100%!");
        return 0;
    } else {
        LOGE("⚠ Shared texture sampling verification failed");
        return 3;
    }
}
