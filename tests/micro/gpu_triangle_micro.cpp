#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// Bionic GLES2 / EGL types & function pointers
typedef void* EGLDisplay;
typedef void* EGLConfig;
typedef void* EGLSurface;
typedef void* EGLContext;

extern "C" {
    // EGL
    EGLDisplay eglGetDisplay(void*);
    unsigned int eglInitialize(EGLDisplay, int*, int*);
    int eglChooseConfig(EGLDisplay, const int*, EGLConfig*, int, int*);
    EGLSurface eglCreatePbufferSurface(EGLDisplay, EGLConfig, const int*);
    EGLContext eglCreateContext(EGLDisplay, EGLConfig, EGLContext, const int*);
    unsigned int eglMakeCurrent(EGLDisplay, EGLSurface, EGLSurface, EGLContext);

    // GLES
    const char* glGetString(unsigned int);
    unsigned int glCreateShader(unsigned int);
    void glShaderSource(unsigned int, int, const char* const*, const int*);
    void glCompileShader(unsigned int);
    void glGetShaderiv(unsigned int, unsigned int, int*);
    void glGetShaderInfoLog(unsigned int, int, int*, char*);
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
}

static char s_logBuf[4096];

extern "C" const char* kudroid_test_main(void) {
    snprintf(s_logBuf, sizeof(s_logBuf),
        "=== [Micro-Test SO] Standalone ARM64 GPU Guest Pipeline Test ===\n");

    EGLDisplay dpy = eglGetDisplay(nullptr);
    int maj = 0, min = 0;
    if (!eglInitialize(dpy, &maj, &min)) {
        strcat(s_logBuf, "❌ eglInitialize failed\n");
        return s_logBuf;
    }
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "✔ EGL Initialized: version %d.%d\n", maj, min);
    strcat(s_logBuf, tmp);

    const int configAttribs[] = {
        0x3033, 0x0001, // EGL_PBUFFER_BIT
        0x3040, 0x0004, // EGL_OPENGL_ES2_BIT
        0x3038
    };
    EGLConfig config = nullptr;
    int num = 0;
    eglChooseConfig(dpy, configAttribs, &config, 1, &num);

    const int pbufAttribs[] = { 0x3057, 64, 0x3056, 64, 0x3038 };
    EGLSurface surf = eglCreatePbufferSurface(dpy, config, pbufAttribs);
    const int ctxAttribs[] = { 0x3098, 2, 0x3038 };
    EGLContext ctx = eglCreateContext(dpy, config, nullptr, ctxAttribs);
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
    glShaderSource(vs, 1, vsList, nullptr);
    glCompileShader(vs);
    int vStat = 0;
    glGetShaderiv(vs, 0x8B81, &vStat);

    unsigned int fs = glCreateShader(0x8B30);
    const char* fsList[] = { fSrc };
    glShaderSource(fs, 1, fsList, nullptr);
    glCompileShader(fs);
    int fStat = 0;
    glGetShaderiv(fs, 0x8B81, &fStat);

    unsigned int prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    int pStat = 0;
    glGetProgramiv(prog, 0x8B82, &pStat);

    snprintf(tmp, sizeof(tmp), "✔ Shader Compile (VS=%d, FS=%d) & Program Link (Prog=%d, Status=%d)\n",
             vStat, fStat, prog, pStat);
    strcat(s_logBuf, tmp);

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
    snprintf(tmp, sizeof(tmp), "✔ Pixel Readback: RGBA=(%d, %d, %d, %d)\n",
             (int)pix[0], (int)pix[1], (int)pix[2], (int)pix[3]);
    strcat(s_logBuf, tmp);

    if (pix[0] > 200 && pix[1] > 100) {
        strcat(s_logBuf, "🎉 SUCCESS: Guest .so Rendered Orange Pixel Through KuDroid Shims!\n");
    } else {
        strcat(s_logBuf, "⚠ WARNING: Color mismatch\n");
    }

    return s_logBuf;
}
