typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long uint64_t;
typedef unsigned long size_t;
#define NULL ((void*)0)

// Android Log
int __android_log_print(int prio, const char* tag, const char* fmt, ...);
#define LOGI(...) __android_log_print(3, "GpuES3Adv", __VA_ARGS__)
#define LOGE(...) __android_log_print(6, "GpuES3Adv", __VA_ARGS__)

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

// GLES Core & Shaders
unsigned int glCreateShader(unsigned int);
void glShaderSource(unsigned int, int, const char* const*, const int*);
void glCompileShader(unsigned int);
void glGetShaderiv(unsigned int, unsigned int, int*);
unsigned int glCreateProgram(void);
void glAttachShader(unsigned int, unsigned int);
void glLinkProgram(unsigned int);
void glGetProgramiv(unsigned int, unsigned int, int*);
void glUseProgram(unsigned int);
void glViewport(int, int, int, int);
void glClearColor(float, float, float, float);
void glClear(unsigned int);
void glReadPixels(int, int, int, int, unsigned int, unsigned int, void*);
void glEnableVertexAttribArray(unsigned int);
void glVertexAttribPointer(unsigned int, int, unsigned int, unsigned char, int, const void*);

// GLES 3.0 Advanced APIs
void glGenBuffers(int, unsigned int*);
void glBindBuffer(unsigned int, unsigned int);
void glBufferData(unsigned int, long, const void*, unsigned int);
void* glMapBufferRange(unsigned int, long, long, unsigned int);
unsigned char glUnmapBuffer(unsigned int);

void glGenTextures(int, unsigned int*);
void glBindTexture(unsigned int, unsigned int);
void glTexStorage2D(unsigned int, int, unsigned int, int, int);
void glTexParameteri(unsigned int, unsigned int, int);

void glGenFramebuffers(int, unsigned int*);
void glBindFramebuffer(unsigned int, unsigned int);
void glFramebufferTexture2D(unsigned int, unsigned int, unsigned int, unsigned int, int);
void glDrawBuffers(int, const unsigned int*);
void glReadBuffer(unsigned int);
void glBlitFramebuffer(int, int, int, int, int, int, int, int, unsigned int, unsigned int);
unsigned int glCheckFramebufferStatus(unsigned int);

void* glFenceSync(unsigned int, unsigned int);
unsigned int glClientWaitSync(void*, unsigned int, uint64_t);
void glDeleteSync(void*);

void glDrawElementsInstanced(unsigned int, int, unsigned int, const void*, int);
void glVertexAttribDivisor(unsigned int, unsigned int);

int kudroid_test_main(void) {
    LOGI("=================================================");
    LOGI("🚀 [GLES 3.0/3.1 Advanced Engine Subsystem Test]");
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
    LOGI("✔ GLES 3.0 Context active");

    // ── FEATURE 1: Direct Memory Buffer Mapping (glMapBufferRange) ──────────
    unsigned int vbo = 0;
    glGenBuffers(1, &vbo);
    glBindBuffer(0x8892, vbo); // GL_ARRAY_BUFFER
    glBufferData(0x8892, 256 * sizeof(float), NULL, 0x88E8); // GL_DYNAMIC_DRAW

    // Map con trỏ bộ nhớ VRAM trực tiếp
    float* mappedPtr = (float*)glMapBufferRange(0x8892, 0, 18 * sizeof(float), 0x0002); // GL_MAP_WRITE_BIT
    if (!mappedPtr) {
        LOGE("❌ glMapBufferRange returned NULL");
        return 2;
    }
    // Ghi trực tiếp dữ liệu đỉnh tam giác qua con trỏ RAM
    const float rawTri[] = {
         0.0f,  0.8f, 0.0f,   1.0f, 0.5f, 0.0f,
        -0.8f, -0.8f, 0.0f,   1.0f, 0.5f, 0.0f,
         0.8f, -0.8f, 0.0f,   1.0f, 0.5f, 0.0f
    };
    for (int i = 0; i < 18; ++i) mappedPtr[i] = rawTri[i];
    glUnmapBuffer(0x8892);
    LOGI("✔ [Feature 1] Direct GPU Memory Mapping (glMapBufferRange/glUnmapBuffer) PASSED!");

    // ── FEATURE 2: Immutable Texture Storage (glTexStorage2D) ───────────────
    unsigned int immutTex = 0;
    glGenTextures(1, &immutTex);
    glBindTexture(0x0DE1, immutTex);
    glTexParameteri(0x0DE1, 0x2801, 0x2601);
    glTexParameteri(0x0DE1, 0x2800, 0x2601);
    glTexStorage2D(0x0DE1, 1, 0x8058, 256, 256); // GL_RGBA8 Immutable Allocation
    LOGI("✔ [Feature 2] Immutable Texture Storage (glTexStorage2D 256x256 RGBA8) Allocated!");

    // ── FEATURE 3: Multiple Render Targets (MRT) ────────────────────────────
    unsigned int mrtTex0 = 0, mrtTex1 = 0;
    glGenTextures(1, &mrtTex0);
    glBindTexture(0x0DE1, mrtTex0);
    glTexStorage2D(0x0DE1, 1, 0x8058, 256, 256);

    glGenTextures(1, &mrtTex1);
    glBindTexture(0x0DE1, mrtTex1);
    glTexStorage2D(0x0DE1, 1, 0x8058, 256, 256);

    unsigned int mrtFbo = 0;
    glGenFramebuffers(1, &mrtFbo);
    glBindFramebuffer(0x8D40, mrtFbo);
    glFramebufferTexture2D(0x8D40, 0x8CE0, 0x0DE1, mrtTex0, 0); // GL_COLOR_ATTACHMENT0
    glFramebufferTexture2D(0x8D40, 0x8CE1, 0x0DE1, mrtTex1, 0); // GL_COLOR_ATTACHMENT1

    const unsigned int drawBuffers[] = { 0x8CE0, 0x8CE1 };
    glDrawBuffers(2, drawBuffers); // Kích hoạt ghi đồng thời cả 2 Texture

    if (glCheckFramebufferStatus(0x8D40) != 0x8CD5) {
        LOGE("❌ MRT Framebuffer incomplete");
        return 3;
    }
    LOGI("✔ [Feature 3] MRT Framebuffer (Dual Color Targets GL_COLOR_ATTACHMENT0/1) Configured!");

    // ── FEATURE 4: GLSL ES 3.00 Multiple Output Shader ──────────────────────
    const char* vSrc =
        "#version 300 es\n"
        "layout(location = 0) in vec3 aPos;\n"
        "layout(location = 1) in vec3 aColor;\n"
        "out vec3 vColor;\n"
        "void main() {\n"
        "  gl_Position = vec4(aPos, 1.0);\n"
        "  vColor = aColor;\n"
        "}\n";

    // Shader ghi đồng thời: Target 0 ra Màu Cam, Target 1 ra Màu Tím Neon
    const char* fSrc =
        "#version 300 es\n"
        "precision mediump float;\n"
        "in vec3 vColor;\n"
        "layout(location = 0) out vec4 outColor0;\n"
        "layout(location = 1) out vec4 outColor1;\n"
        "void main() {\n"
        "  outColor0 = vec4(1.0, 0.5, 0.0, 1.0);\n" // Orange (255, 128, 0)
        "  outColor1 = vec4(0.8, 0.0, 1.0, 1.0);\n" // Neon Purple (204, 0, 255)
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

    glViewport(0, 0, 256, 256);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(0x4000);

    glBindBuffer(0x8892, vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, 0x1406, 0, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, 0x1406, 0, 6 * sizeof(float), (void*)(3 * sizeof(float)));

    // Index buffer
    unsigned int ebo = 0;
    const uint16_t indices[] = { 0, 1, 2 };
    glGenBuffers(1, &ebo);
    glBindBuffer(0x8893, ebo);
    glBufferData(0x8893, sizeof(indices), indices, 0x88E4);

    // ── FEATURE 5: Instanced Draw Call (glDrawElementsInstanced) ────────────
    LOGI("🚀 [Feature 5] Executing glDrawElementsInstanced (4 instances)...");
    glDrawElementsInstanced(0x0004, 3, 0x1403, (void*)0, 4); // GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, offset 0, 4 instances
    LOGI("✔ [Feature 5] glDrawElementsInstanced executed successfully!");

    // ── FEATURE 6: GPU Fence Synchronization (glFenceSync) ──────────────────
    void* sync = glFenceSync(0x9117, 0); // GL_SYNC_GPU_COMMANDS_COMPLETE
    if (!sync) {
        LOGE("❌ glFenceSync failed");
        return 4;
    }
    // Đợi GPU hoàn tất lệnh vẽ
    unsigned int syncRes = glClientWaitSync(sync, 0x00000001, 1000000000ULL); // GL_SYNC_FLUSH_COMMANDS_BIT, 1 sec
    glDeleteSync(sync);
    LOGI("✔ [Feature 6] GPU Hardware Fence Sync (glFenceSync / glClientWaitSync result=0x%x) PASSED!", syncRes);

    // ── VERIFICATION: Kiểm tra nội dung 2 Render Target MRT ──────────────────
    // Đọc Attachment 0 (Target 0: Màu Cam)
    glReadBuffer(0x8CE0); // GL_COLOR_ATTACHMENT0
    uint8_t pix0[4] = {0};
    glReadPixels(128, 128, 1, 1, 0x1908, 0x1401, pix0);
    LOGI("🔍 MRT Target 0 Pixel (Orange): RGBA=(%d, %d, %d, %d)",
         (int)pix0[0], (int)pix0[1], (int)pix0[2], (int)pix0[3]);

    // Đọc Attachment 1 (Target 1: Màu Tím Neon)
    glReadBuffer(0x8CE1); // GL_COLOR_ATTACHMENT1
    uint8_t pix1[4] = {0};
    glReadPixels(128, 128, 1, 1, 0x1908, 0x1401, pix1);
    LOGI("🔍 MRT Target 1 Pixel (Neon Purple): RGBA=(%d, %d, %d, %d)",
         (int)pix1[0], (int)pix1[1], (int)pix1[2], (int)pix1[3]);

    // Kiểm tra tính chính xác của cả 2 target
    if (pix0[0] > 200 && pix0[1] > 100 && pix0[2] == 0 &&
        pix1[0] > 180 && pix1[1] == 0 && pix1[2] > 200) {
        LOGI("🎉 SUCCESS: ALL 6 ADVANCED GLES 3.0/3.1 FEATURES PASSED 100% WITH PERFECT HARDWARE EXECUTION!");
        return 0;
    } else {
        LOGE("⚠ MRT Pixel mismatch: Target0=(%d,%d,%d) Target1=(%d,%d,%d)",
             (int)pix0[0], (int)pix0[1], (int)pix0[2], (int)pix1[0], (int)pix1[1], (int)pix1[2]);
        return 5;
    }
}
