typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long size_t;
#define NULL ((void*)0)

// Android log
int __android_log_print(int prio, const char* tag, const char* fmt, ...);

// Bionic GLES2 / EGL types & function prototypes
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

// GLES
unsigned int glCreateShader(unsigned int);
void glShaderSource(unsigned int, int, const char* const*, const int*);
void glCompileShader(unsigned int);
void glGetShaderiv(unsigned int, unsigned int, int*);
unsigned int glCreateProgram(void);
void glAttachShader(unsigned int, unsigned int);
void glLinkProgram(unsigned int);
void glGetProgramiv(unsigned int, unsigned int, int*);
void glUseProgram(unsigned int);
int glGetAttribLocation(unsigned int, const char*);
void glEnableVertexAttribArray(unsigned int);
void glVertexAttribPointer(unsigned int, int, unsigned int, unsigned char, int, const void*);
void glViewport(int, int, int, int);
void glClearColor(float, float, float, float);
void glClear(unsigned int);
void glDrawArrays(unsigned int, int, int);
void glReadPixels(int, int, int, int, unsigned int, unsigned int, void*);

#define LOGI(...) __android_log_print(3, "MicroTestGPU", __VA_ARGS__)
#define LOGE(...) __android_log_print(6, "MicroTestGPU", __VA_ARGS__)

int kudroid_test_main(void) {
    LOGI("=== [Micro-Test SO] Running Guest GPU Triangle Test ===");

    EGLDisplay dpy = eglGetDisplay(NULL);
    int maj = 0, min = 0;
    if (!eglInitialize(dpy, &maj, &min)) {
        LOGE("❌ eglInitialize failed");
        return 1;
    }
    LOGI("✔ EGL Initialized: version %d.%d", maj, min);

    const int configAttribs[] = {
        0x3033, 0x0001, // EGL_PBUFFER_BIT
        0x3040, 0x0004, // EGL_OPENGL_ES2_BIT
        0x3038
    };
    EGLConfig config = NULL;
    int num = 0;
    eglChooseConfig(dpy, configAttribs, &config, 1, &num);

    const int pbufAttribs[] = { 0x3057, 64, 0x3056, 64, 0x3038 };
    EGLSurface surf = eglCreatePbufferSurface(dpy, config, pbufAttribs);
    const int ctxAttribs[] = { 0x3098, 2, 0x3038 };
    EGLContext ctx = eglCreateContext(dpy, config, NULL, ctxAttribs);
    eglMakeCurrent(dpy, surf, surf, ctx);

    const char* vSrc =
        "attribute vec4 aPos;\n"
        "void main() {\n"
        "  gl_Position = aPos;\n"
        "}\n";

    const char* fSrc =
        "precision mediump float;\n"
        "void main() {\n"
        "  gl_FragColor = vec4(1.0, 0.5, 0.0, 1.0);\n" // Orange
        "}\n";

    unsigned int vs = glCreateShader(0x8B31);
    const char* vsList[] = { vSrc };
    glShaderSource(vs, 1, vsList, NULL);
    glCompileShader(vs);
    int vStat = 0;
    glGetShaderiv(vs, 0x8B81, &vStat);

    unsigned int fs = glCreateShader(0x8B30);
    const char* fsList[] = { fSrc };
    glShaderSource(fs, 1, fsList, NULL);
    glCompileShader(fs);
    int fStat = 0;
    glGetShaderiv(fs, 0x8B81, &fStat);

    unsigned int prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    int pStat = 0;
    glGetProgramiv(prog, 0x8B82, &pStat);

    LOGI("✔ Shader Compile (VS=%d, FS=%d) & Program Link (Prog=%d, Status=%d)",
         vStat, fStat, prog, pStat);

    glViewport(0, 0, 64, 64);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(0x4000);
    glUseProgram(prog);

    int posLoc = glGetAttribLocation(prog, "aPos");
    const float tri[] = {
        -1.0f, -1.0f, 0.0f,
         3.0f, -1.0f, 0.0f,
        -1.0f,  3.0f, 0.0f
    };
    glEnableVertexAttribArray(posLoc);
    glVertexAttribPointer(posLoc, 3, 0x1406, 0, 0, tri);
    glDrawArrays(0x0004, 0, 3);

    uint8_t pix[4] = {0};
    glReadPixels(32, 32, 1, 1, 0x1908, 0x1401, pix);
    LOGI("✔ Pixel Readback: RGBA=(%d, %d, %d, %d)",
         (int)pix[0], (int)pix[1], (int)pix[2], (int)pix[3]);

    if (pix[0] > 200 && pix[1] > 100) {
        LOGI("🎉 SUCCESS: Guest .so Rendered Orange Pixel Through KuDroid Shims!");
        return 0; // SUCCESS -> retCode = 0 (an toàn tuyệt đối)
    } else {
        LOGE("⚠ WARNING: Color mismatch");
        return 2;
    }
}
