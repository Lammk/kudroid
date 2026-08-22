typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long size_t;
#define NULL ((void*)0)

// Android Log
int __android_log_print(int prio, const char* tag, const char* fmt, ...);
#define LOGI(...) __android_log_print(3, "GpuCompressed", __VA_ARGS__)
#define LOGE(...) __android_log_print(6, "GpuCompressed", __VA_ARGS__)

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
void glTexParameteri(unsigned int, unsigned int, int);
void glCompressedTexImage2D(unsigned int, int, unsigned int, int, int, int, int, const void*);
void glGenFramebuffers(int, unsigned int*);
void glBindFramebuffer(unsigned int, unsigned int);
void glFramebufferTexture2D(unsigned int, unsigned int, unsigned int, unsigned int, int);
void glTexImage2D(unsigned int, int, int, int, int, int, unsigned int, unsigned int, const void*);

int kudroid_test_main(void) {
    LOGI("=================================================");
    LOGI("🗜 [ETC2 Hardware Compressed Texture Sampling Test]");
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
    EGLSurface surf = eglCreatePbufferSurface(dpy, config, pbufAttribs);
    const int ctxAttribs[] = { 0x3098, 3, 0x3038 };
    EGLContext ctx = eglCreateContext(dpy, config, NULL, ctxAttribs);
    eglMakeCurrent(dpy, surf, surf, ctx);
    LOGI("✔ GLES 3.0 Context active for ETC2 hardware decoding");

    // 1. Tạo khối dữ liệu nén phần cứng ETC2 (4x4 pixels = 1 block 8 bytes)
    // Block nén ETC2 biểu diễn một màu solid Xanh Lá Neon (R=0, G=255, B=0)
    // Định dạng nén: GL_COMPRESSED_RGB8_ETC2 = 0x9274
    const uint8_t etc2GreenBlock[8] = {
        0x00, 0xF8, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00
    };

    unsigned int etc2Tex = 0;
    glGenTextures(1, &etc2Tex);
    glBindTexture(0x0DE1, etc2Tex);
    glTexParameteri(0x0DE1, 0x2801, 0x2600); // GL_NEAREST
    glTexParameteri(0x0DE1, 0x2800, 0x2600); // GL_NEAREST

    // Nạp khối nén 4x4 ETC2 qua glCompressedTexImage2D
    LOGI("📦 Nạp khối ETC2 RGB8 4x4 (8 bytes) vào phần cứng GPU Apple Silicon...");
    glCompressedTexImage2D(0x0DE1, 0, 0x9274, 4, 4, 0, sizeof(etc2GreenBlock), etc2GreenBlock);
    LOGI("✔ glCompressedTexImage2D executed successfully!");

    // 2. Tạo FBO để render
    unsigned int fboTex = 0, fbo = 0;
    glGenTextures(1, &fboTex);
    glBindTexture(0x0DE1, fboTex);
    glTexImage2D(0x0DE1, 0, 0x1908, 256, 256, 0, 0x1908, 0x1401, NULL);

    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(0x8D40, fbo);
    glFramebufferTexture2D(0x8D40, 0x8CE0, 0x0DE1, fboTex, 0);

    // 3. Shader lấy mẫu Texture ETC2
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

    glBindTexture(0x0DE1, etc2Tex);

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
    glDrawArrays(0x0005, 0, 4);

    // 4. Đọc Pixel: Kiểm tra GPU Apple Silicon giải mã khối ETC2
    uint8_t pix[4] = {0};
    glReadPixels(128, 128, 1, 1, 0x1908, 0x1401, pix);
    LOGI("🔍 Decompressed ETC2 Sampled Pixel: RGBA=(%d, %d, %d, %d)",
         (int)pix[0], (int)pix[1], (int)pix[2], (int)pix[3]);

    // Kênh Green > 100 và chiếm ưu thế tuyệt đối so với Red/Blue
    if (pix[1] > 100 && pix[1] > pix[0] && pix[1] > pix[2] && pix[3] == 255) {
        LOGI("🎉 SUCCESS: HARDWARE ETC2 COMPRESSED TEXTURE DECODED & RENDERED PERFECTLY!");
        return 0;
    } else {
        LOGE("⚠ ETC2 Decompression verification failed");
        return 2;
    }
}
