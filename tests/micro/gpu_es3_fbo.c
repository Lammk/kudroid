typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long size_t;
#define NULL ((void*)0)

// Android Log
int __android_log_print(int prio, const char* tag, const char* fmt, ...);
#define LOGI(...) __android_log_print(3, "GpuES3Test", __VA_ARGS__)
#define LOGE(...) __android_log_print(6, "GpuES3Test", __VA_ARGS__)

// Bionic GLES2/3 / EGL types & function prototypes
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

// GLES 2/3
unsigned int glCreateShader(unsigned int);
void glShaderSource(unsigned int, int, const char* const*, const int*);
void glCompileShader(unsigned int);
void glGetShaderiv(unsigned int, unsigned int, int*);
void glGetShaderInfoLog(unsigned int, int, int*, char*);
unsigned int glCreateProgram(void);
void glAttachShader(unsigned int, unsigned int);
void glLinkProgram(unsigned int);
void glGetProgramiv(unsigned int, unsigned int, int*);
void glGetProgramInfoLog(unsigned int, int, int*, char*);
void glUseProgram(unsigned int);
int glGetAttribLocation(unsigned int, const char*);
void glEnableVertexAttribArray(unsigned int);
void glVertexAttribPointer(unsigned int, int, unsigned int, unsigned char, int, const void*);
void glViewport(int, int, int, int);
void glClearColor(float, float, float, float);
void glClear(unsigned int);
void glDrawArrays(unsigned int, int, int);
void glReadPixels(int, int, int, int, unsigned int, unsigned int, void*);

// Textures & FBO
void glGenTextures(int, unsigned int*);
void glBindTexture(unsigned int, unsigned int);
void glTexImage2D(unsigned int, int, int, int, int, int, unsigned int, unsigned int, const void*);
void glTexParameteri(unsigned int, unsigned int, int);
void glGenFramebuffers(int, unsigned int*);
void glBindFramebuffer(unsigned int, unsigned int);
void glFramebufferTexture2D(unsigned int, unsigned int, unsigned int, unsigned int, int);
unsigned int glCheckFramebufferStatus(unsigned int);
void glDeleteFramebuffers(int, const unsigned int*);
void glDeleteTextures(int, const unsigned int*);
const char* glGetString(unsigned int);

int kudroid_test_main(void) {
    LOGI("=================================================");
    LOGI("🧪 [OpenGL ES 3.0 FBO & Render-To-Texture Test]");
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
        0x3040, 0x0040, // EGL_OPENGL_ES3_BIT (0x0040)
        0x3038
    };
    EGLConfig config = NULL;
    int numConfigs = 0;
    eglChooseConfig(dpy, configAttribs, &config, 1, &numConfigs);

    const int pbufAttribs[] = { 0x3057, 256, 0x3056, 256, 0x3038 };
    EGLSurface surf = eglCreatePbufferSurface(dpy, config, pbufAttribs);

    // Request OpenGL ES 3.0 Context (0x3098 = EGL_CONTEXT_CLIENT_VERSION)
    const int ctxAttribs[] = { 0x3098, 3, 0x3038 };
    EGLContext ctx = eglCreateContext(dpy, config, NULL, ctxAttribs);
    if (!ctx) {
        LOGE("❌ Failed to create OpenGL ES 3.0 Context");
        return 2;
    }
    eglMakeCurrent(dpy, surf, surf, ctx);

    const char* glVer = glGetString(0x1F02); // GL_VERSION
    const char* glSlVer = glGetString(0x8B8C); // GL_SHADING_LANGUAGE_VERSION
    LOGI("✔ OpenGL Context Active: %s (GLSL: %s)", glVer ? glVer : "Unknown", glSlVer ? glSlVer : "Unknown");

    // 1. Tạo Texture đích
    unsigned int tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(0x0DE1, tex); // GL_TEXTURE_2D
    glTexParameteri(0x0DE1, 0x2801, 0x2601); // GL_TEXTURE_MIN_FILTER, GL_LINEAR
    glTexParameteri(0x0DE1, 0x2800, 0x2601); // GL_TEXTURE_MAG_FILTER, GL_LINEAR
    glTexImage2D(0x0DE1, 0, 0x1908, 256, 256, 0, 0x1908, 0x1401, NULL); // 256x256 RGBA8
    LOGI("✔ Created 256x256 RGBA8 Render Target Texture (ID=%u)", tex);

    // 2. Tạo Framebuffer Object (FBO) và gắn Texture vào
    unsigned int fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(0x8D40, fbo); // GL_FRAMEBUFFER
    glFramebufferTexture2D(0x8D40, 0x8CE0, 0x0DE1, tex, 0); // GL_COLOR_ATTACHMENT0

    unsigned int fboStatus = glCheckFramebufferStatus(0x8D40);
    if (fboStatus != 0x8CD5) { // GL_FRAMEBUFFER_COMPLETE = 0x8CD5
        LOGE("❌ Framebuffer incomplete! Status = 0x%x", fboStatus);
        return 3;
    }
    LOGI("✔ Framebuffer Complete & Bound (FBO ID=%u, Status=0x8CD5)", fbo);

    // 3. Biên dịch GLSL ES 3.00 Shader
    const char* vSrc =
        "#version 300 es\n"
        "layout(location = 0) in vec4 aPosition;\n"
        "void main() {\n"
        "  gl_Position = aPosition;\n"
        "}\n";

    const char* fSrc =
        "#version 300 es\n"
        "precision highp float;\n"
        "out vec4 fragColor;\n"
        "void main() {\n"
        "  fragColor = vec4(1.0, 0.0, 0.5, 1.0);\n" // Bright Magenta Pink
        "}\n";

    unsigned int vs = glCreateShader(0x8B31);
    const char* vsList[] = { vSrc };
    glShaderSource(vs, 1, vsList, NULL);
    glCompileShader(vs);

    int vStat = 0;
    glGetShaderiv(vs, 0x8B81, &vStat);
    if (!vStat) {
        char errLog[512] = {0};
        glGetShaderInfoLog(vs, sizeof(errLog), NULL, errLog);
        LOGE("❌ GLSL 3.0 VS compile failed: %s", errLog);
        return 4;
    }

    unsigned int fs = glCreateShader(0x8B30);
    const char* fsList[] = { fSrc };
    glShaderSource(fs, 1, fsList, NULL);
    glCompileShader(fs);

    int fStat = 0;
    glGetShaderiv(fs, 0x8B81, &fStat);
    if (!fStat) {
        char errLog[512] = {0};
        glGetShaderInfoLog(fs, sizeof(errLog), NULL, errLog);
        LOGE("❌ GLSL 3.0 FS compile failed: %s", errLog);
        return 5;
    }

    unsigned int prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);

    int pStat = 0;
    glGetProgramiv(prog, 0x8B82, &pStat);
    if (!pStat) {
        char errLog[512] = {0};
        glGetProgramInfoLog(prog, sizeof(errLog), NULL, errLog);
        LOGE("❌ GLSL 3.0 Program link failed: %s", errLog);
        return 6;
    }
    LOGI("✔ GLSL ES 3.00 Shader Compiled and Program Linked successfully!");

    // 4. Render vào Texture qua FBO
    glViewport(0, 0, 256, 256);
    glClearColor(0.0f, 0.8f, 1.0f, 1.0f); // Cyan blue background
    glClear(0x4000);

    glUseProgram(prog);
    const float tri[] = {
         0.0f,  0.8f, 0.0f,
        -0.8f, -0.8f, 0.0f,
         0.8f, -0.8f, 0.0f
    };
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, 0x1406, 0, 0, tri);
    glDrawArrays(0x0004, 0, 3);
    LOGI("✔ Render-To-Texture Draw Call executed via FBO!");

    // 5. Đọc pixel từ Texture FBO để xác thực
    // Đọc tại tâm (vị trí có tam giác Magenta Pink: R=255, G=0, B=128)
    uint8_t centerPix[4] = {0};
    glReadPixels(128, 128, 1, 1, 0x1908, 0x1401, centerPix);
    LOGI("🔍 Center Pixel (Triangle): RGBA=(%d, %d, %d, %d)",
         (int)centerPix[0], (int)centerPix[1], (int)centerPix[2], (int)centerPix[3]);

    // Đọc tại góc (vị trí nền Cyan Blue: R=0, G=204, B=255)
    uint8_t cornerPix[4] = {0};
    glReadPixels(10, 10, 1, 1, 0x1908, 0x1401, cornerPix);
    LOGI("🔍 Corner Pixel (Background): RGBA=(%d, %d, %d, %d)",
         (int)cornerPix[0], (int)cornerPix[1], (int)cornerPix[2], (int)cornerPix[3]);

    // Kiểm tra kết quả
    if (centerPix[0] > 200 && centerPix[1] < 50 && centerPix[2] > 100 &&
        cornerPix[0] < 50 && cornerPix[1] > 150 && cornerPix[2] > 200) {
        LOGI("🎉 SUCCESS: OpenGL ES 3.0 FBO Render-To-Texture PASSED WITH PERFECT ACCURACY!");
        return 0;
    } else {
        LOGE("⚠ Color validation failed on FBO readback");
        return 7;
    }
}
