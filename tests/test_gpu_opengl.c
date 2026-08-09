/**
 * test_gpu_opengl.c — ARM64 ELF .so that tests OpenGL ES via dlopen/dlsym.
 * When loaded by KuDroid's ELF loader, the dlopen("libGLESv2.so") and
 * dlopen("libEGL.so") calls get intercepted by BionicShim, which maps
 * dlsym calls directly to ANGLE's Metal backend on iOS.
 */
#include <dlfcn.h>
#include <stdint.h>
#include <string.h>

/* Android log extern — resolved by BionicShim */
#ifndef ANDROID_LOG_INFO
#define ANDROID_LOG_INFO 4
#endif
extern int __android_log_print(int priority, const char* tag,
                               const char* format, ...);

/* Minimal GL/EGL type stubs */
typedef unsigned int GLenum;
typedef unsigned char GLubyte;
#define GL_VENDOR     0x1F00
#define GL_RENDERER   0x1F01
#define GL_VERSION    0x1F02
#define GL_EXTENSIONS 0x1F03

typedef const GLubyte* (*PFN_glGetString)(GLenum name);
typedef void (*PFN_glGetIntegerv)(GLenum pname, int* params);

/* EGL types */
typedef void* EGLDisplay;
typedef int EGLint;
typedef unsigned int EGLBoolean;
#define EGL_DEFAULT_DISPLAY ((void*)0)

typedef EGLDisplay (*PFN_eglGetDisplay)(void* display_id);
typedef EGLBoolean (*PFN_eglInitialize)(EGLDisplay dpy, EGLint* major, EGLint* minor);
typedef const char* (*PFN_eglQueryString)(EGLDisplay dpy, EGLint name);
typedef EGLBoolean (*PFN_eglTerminate)(EGLDisplay dpy);

#define EGL_VENDOR     0x3053
#define EGL_VERSION    0x3054
#define EGL_EXTENSIONS 0x3055

/* Result codes */
#define GPU_GL_OK               0
#define GPU_GL_DLOPEN_FAIL     -1
#define GPU_GL_NO_GETSTRING    -2
#define GPU_EGL_DLOPEN_FAIL    -3
#define GPU_EGL_NO_GETDISPLAY  -4

/**
 * kudroid_gpu_opengl_test() — main entry point called by KuDroid.
 * Returns 0 on success, negative on failure.
 */
int kudroid_gpu_opengl_test(void) {
    int result = GPU_GL_OK;

    /* ── Test GLESv2 ── */
    void* gles = dlopen("libGLESv2.so", RTLD_NOW);
    if (!gles) {
        __android_log_print(ANDROID_LOG_INFO, "KuDroidGPU",
                            "GL TEST: dlopen(libGLESv2.so) FAILED");
        return GPU_GL_DLOPEN_FAIL;
    }
    __android_log_print(ANDROID_LOG_INFO, "KuDroidGPU",
                        "GL TEST: dlopen(libGLESv2.so) OK handle=%p", gles);

    /* Check key GLES symbols */
    const char* gl_symbols[] = {
        "glGetString",
        "glGetIntegerv",
        "glClear",
        "glClearColor",
        "glEnable",
        "glDisable",
        "glViewport",
        "glCreateShader",
        "glCreateProgram",
        "glDrawArrays",
        "glDrawElements",
        "glGenTextures",
        "glBindTexture",
        "glTexImage2D",
        "glGenFramebuffers",
        "glBindFramebuffer",
        "glGenBuffers",
        "glBindBuffer",
        "glBufferData",
        NULL
    };

    int gl_found = 0, gl_missing = 0;
    for (int i = 0; gl_symbols[i]; i++) {
        void* sym = dlsym(gles, gl_symbols[i]);
        if (sym) {
            gl_found++;
            __android_log_print(ANDROID_LOG_INFO, "KuDroidGPU",
                                "GL TEST: %s => %p", gl_symbols[i], sym);
        } else {
            gl_missing++;
            __android_log_print(ANDROID_LOG_INFO, "KuDroidGPU",
                                "GL TEST: %s => NOT FOUND", gl_symbols[i]);
        }
    }
    __android_log_print(ANDROID_LOG_INFO, "KuDroidGPU",
                        "GL TEST: %d symbols found, %d missing", gl_found, gl_missing);

    dlclose(gles);

    /* ── Test EGL ── */
    void* egl = dlopen("libEGL.so", RTLD_NOW);
    if (!egl) {
        __android_log_print(ANDROID_LOG_INFO, "KuDroidGPU",
                            "EGL TEST: dlopen(libEGL.so) FAILED");
        return GPU_EGL_DLOPEN_FAIL;
    }
    __android_log_print(ANDROID_LOG_INFO, "KuDroidGPU",
                        "EGL TEST: dlopen(libEGL.so) OK handle=%p", egl);

    const char* egl_symbols[] = {
        "eglGetDisplay",
        "eglInitialize",
        "eglTerminate",
        "eglQueryString",
        "eglChooseConfig",
        "eglCreateContext",
        "eglMakeCurrent",
        "eglSwapBuffers",
        "eglCreateWindowSurface",
        "eglGetProcAddress",
        NULL
    };

    int egl_found = 0, egl_missing = 0;
    for (int i = 0; egl_symbols[i]; i++) {
        void* sym = dlsym(egl, egl_symbols[i]);
        if (sym) {
            egl_found++;
            __android_log_print(ANDROID_LOG_INFO, "KuDroidGPU",
                                "EGL TEST: %s => %p", egl_symbols[i], sym);
        } else {
            egl_missing++;
            __android_log_print(ANDROID_LOG_INFO, "KuDroidGPU",
                                "EGL TEST: %s => NOT FOUND", egl_symbols[i]);
        }
    }
    __android_log_print(ANDROID_LOG_INFO, "KuDroidGPU",
                        "EGL TEST: %d symbols found, %d missing", egl_found, egl_missing);

    dlclose(egl);

    __android_log_print(ANDROID_LOG_INFO, "KuDroidGPU",
                        "GPU OPENGL+EGL TEST: ALL PASSED (GL=%d/%d EGL=%d/%d)",
                        gl_found, gl_found + gl_missing,
                        egl_found, egl_found + egl_missing);
    return result;
}
