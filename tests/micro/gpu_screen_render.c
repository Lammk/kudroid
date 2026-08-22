typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long size_t;
#define NULL ((void*)0)

// Android Log
int __android_log_print(int prio, const char* tag, const char* fmt, ...);
#define LOGI(...) __android_log_print(3, "ScreenGPU", __VA_ARGS__)
#define LOGE(...) __android_log_print(6, "ScreenGPU", __VA_ARGS__)

// Bionic GLES2 / EGL types & function prototypes
typedef void* EGLDisplay;
typedef void* EGLConfig;
typedef void* EGLSurface;
typedef void* EGLContext;
typedef void* EGLNativeWindowType;

// Native Window
void* ANativeWindow_fromSurface(void* env, void* surface);
int ANativeWindow_getWidth(void* window);
int ANativeWindow_getHeight(void* window);

// EGL
EGLDisplay eglGetDisplay(void*);
unsigned int eglInitialize(EGLDisplay, int*, int*);
int eglChooseConfig(EGLDisplay, const int*, EGLConfig*, int, int*);
EGLSurface eglCreateWindowSurface(EGLDisplay, EGLConfig, EGLNativeWindowType, const int*);
EGLContext eglCreateContext(EGLDisplay, EGLConfig, EGLContext, const int*);
unsigned int eglMakeCurrent(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
unsigned int eglSwapBuffers(EGLDisplay, EGLSurface);

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

// Simple busy-wait delay
static void simple_delay(volatile unsigned int count) {
    while (count--) {
        __asm__ volatile("" : : : "memory");
    }
}

int kudroid_test_main(void) {
    LOGI("=================================================");
    LOGI("🚀 [GPU Live Screen Test] Starting On-Screen Render Test!");
    LOGI("=================================================");

    void* win = ANativeWindow_fromSurface(NULL, NULL);
    int width = ANativeWindow_getWidth(win);
    int height = ANativeWindow_getHeight(win);
    LOGI("📱 Target Window: %p (Native Resolution: %dx%d)", win, width, height);

    EGLDisplay dpy = eglGetDisplay(NULL);
    int maj = 0, min = 0;
    if (!eglInitialize(dpy, &maj, &min)) {
        LOGE("❌ eglInitialize failed");
        return 1;
    }
    LOGI("✔ EGL Initialized: version %d.%d", maj, min);

    const int configAttribs[] = {
        0x3033, 0x0004, // EGL_WINDOW_BIT (0x0004)
        0x3040, 0x0004, // EGL_OPENGL_ES2_BIT
        0x3024, 8,      // EGL_RED_SIZE
        0x3023, 8,      // EGL_GREEN_SIZE
        0x3022, 8,      // EGL_BLUE_SIZE
        0x3021, 8,      // EGL_ALPHA_SIZE
        0x3038
    };
    EGLConfig config = NULL;
    int numConfigs = 0;
    eglChooseConfig(dpy, configAttribs, &config, 1, &numConfigs);
    LOGI("✔ eglChooseConfig returned %d configs", numConfigs);

    EGLSurface winSurf = eglCreateWindowSurface(dpy, config, win, NULL);
    if (!winSurf) {
        LOGE("❌ eglCreateWindowSurface failed!");
        return 2;
    }
    LOGI("✔ EGL Window Surface Created: %p", winSurf);

    const int ctxAttribs[] = { 0x3098, 2, 0x3038 };
    EGLContext ctx = eglCreateContext(dpy, config, NULL, ctxAttribs);
    if (!ctx) {
        LOGE("❌ eglCreateContext failed!");
        return 3;
    }
    LOGI("✔ EGL Context Created: %p", ctx);

    if (!eglMakeCurrent(dpy, winSurf, winSurf, ctx)) {
        LOGE("❌ eglMakeCurrent with Window Surface failed!");
        return 4;
    }
    LOGI("✔ eglMakeCurrent (Window Surface) SUCCESS! Ready to draw on iPhone screen.");

    const char* vSrc =
        "attribute vec4 aPos;\n"
        "void main() {\n"
        "  gl_Position = aPos;\n"
        "}\n";

    const char* fSrc =
        "precision mediump float;\n"
        "void main() {\n"
        "  gl_FragColor = vec4(1.0, 0.84, 0.0, 1.0);\n" // Golden Yellow
        "}\n";

    unsigned int vs = glCreateShader(0x8B31);
    const char* vsList[] = { vSrc };
    glShaderSource(vs, 1, vsList, NULL);
    glCompileShader(vs);

    unsigned int fs = glCreateShader(0x8B30);
    const char* fsList[] = { fSrc };
    glShaderSource(fs, 1, fsList, NULL);
    glCompileShader(fs);

    unsigned int prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glUseProgram(prog);

    int posLoc = glGetAttribLocation(prog, "aPos");
    LOGI("✔ Program linked: prog=%u, aPos=%d", prog, posLoc);

    glViewport(0, 0, width > 0 ? width : 1080, height > 0 ? height : 1920);

    const float tri[] = {
         0.0f,  0.5f, 0.0f,
        -0.5f, -0.5f, 0.0f,
         0.5f, -0.5f, 0.0f
    };
    glEnableVertexAttribArray(posLoc);
    glVertexAttribPointer(posLoc, 3, 0x1406, 0, 0, tri);

    LOGI("🎬 Rendering 60 animated frames to iPhone screen (watch your iPhone screen!)...");
    // Render 60 frames chuyển màu mượt mà
    for (int frame = 0; frame < 60; ++frame) {
        float r = 0.1f + 0.4f * (float)(frame % 20) / 20.0f;
        float g = 0.1f + 0.3f * (float)((frame + 10) % 30) / 30.0f;
        float b = 0.3f + 0.6f * (float)(frame % 15) / 15.0f;

        // Clear background with dynamic color
        glClearColor(r, g, b, 1.0f);
        glClear(0x4000);

        // Draw golden triangle
        glDrawArrays(0x0004, 0, 3);

        // Đẩy frame ra màn hình iPhone
        eglSwapBuffers(dpy, winSurf);

        if (frame % 15 == 0) {
            LOGI("  🎨 Rendered frame %d/60 (Clear Color: R=%.2f, G=%.2f, B=%.2f)", frame, r, g, b);
        }
        simple_delay(2000000); // Khoảng 16ms trên chip A16/A17/M-series
    }

    LOGI("🎉 [GPU Live Screen Test] Successfully displayed 60 frames on iPhone CAMetalLayer!");
    return 0;
}
