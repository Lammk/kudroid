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
typedef float GLclampf;
typedef unsigned int GLbitfield;

#define GL_VENDOR     0x1F00
#define GL_RENDERER   0x1F01
#define GL_VERSION    0x1F02
#define GL_EXTENSIONS 0x1F03
#define GL_COLOR_BUFFER_BIT 0x00004000

typedef const GLubyte* (*PFN_glGetString)(GLenum name);
typedef void (*PFN_glClearColor)(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha);
typedef void (*PFN_glClear)(GLbitfield mask);

/* EGL types */
typedef void* EGLDisplay;
typedef void* EGLConfig;
typedef void* EGLSurface;
typedef void* EGLContext;
typedef int EGLint;
typedef unsigned int EGLBoolean;

#define EGL_DEFAULT_DISPLAY ((void*)0)
#define EGL_NO_DISPLAY ((EGLDisplay)0)
#define EGL_NO_SURFACE ((EGLSurface)0)
#define EGL_NO_CONTEXT ((EGLContext)0)
#define EGL_TRUE 1
#define EGL_FALSE 0

#define EGL_SURFACE_TYPE 0x3033
#define EGL_PBUFFER_BIT 0x0001
#define EGL_RENDERABLE_TYPE 0x3040
#define EGL_OPENGL_ES2_BIT 0x0004
#define EGL_NONE 0x3038
#define EGL_WIDTH 0x3057
#define EGL_HEIGHT 0x3056
#define EGL_CONTEXT_CLIENT_VERSION 0x3098

typedef EGLDisplay (*PFN_eglGetDisplay)(void* display_id);
typedef EGLBoolean (*PFN_eglInitialize)(EGLDisplay dpy, EGLint* major, EGLint* minor);
typedef EGLBoolean (*PFN_eglChooseConfig)(EGLDisplay dpy, const EGLint* attrib_list, EGLConfig* configs, EGLint config_size, EGLint* num_config);
typedef EGLSurface (*PFN_eglCreatePbufferSurface)(EGLDisplay dpy, EGLConfig config, const EGLint* attrib_list);
typedef EGLContext (*PFN_eglCreateContext)(EGLDisplay dpy, EGLConfig config, EGLContext share_context, const EGLint* attrib_list);
typedef EGLBoolean (*PFN_eglMakeCurrent)(EGLDisplay dpy, EGLSurface draw, EGLSurface read, EGLContext ctx);
typedef EGLBoolean (*PFN_eglDestroySurface)(EGLDisplay dpy, EGLSurface surface);
typedef EGLBoolean (*PFN_eglDestroyContext)(EGLDisplay dpy, EGLContext ctx);
typedef EGLBoolean (*PFN_eglTerminate)(EGLDisplay dpy);
typedef const char* (*PFN_eglQueryString)(EGLDisplay dpy, EGLint name);

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

    /* ── Test EGL ── */
    void* egl = dlopen("libEGL.so", RTLD_NOW);
    if (!egl) {
        __android_log_print(ANDROID_LOG_INFO, "KuDroidGPU", "GL TEST: dlopen(libEGL.so) FAILED");
        return GPU_EGL_DLOPEN_FAIL;
    }
    
    void* gles = dlopen("libGLESv2.so", RTLD_NOW);
    if (!gles) {
        __android_log_print(ANDROID_LOG_INFO, "KuDroidGPU", "GL TEST: dlopen(libGLESv2.so) FAILED");
        return GPU_GL_DLOPEN_FAIL;
    }

    PFN_eglGetDisplay getDisplay = (PFN_eglGetDisplay)dlsym(egl, "eglGetDisplay");
    PFN_eglInitialize initialize = (PFN_eglInitialize)dlsym(egl, "eglInitialize");
    PFN_eglChooseConfig chooseConfig = (PFN_eglChooseConfig)dlsym(egl, "eglChooseConfig");
    PFN_eglCreatePbufferSurface createPbufferSurface = (PFN_eglCreatePbufferSurface)dlsym(egl, "eglCreatePbufferSurface");
    PFN_eglCreateContext createContext = (PFN_eglCreateContext)dlsym(egl, "eglCreateContext");
    PFN_eglMakeCurrent makeCurrent = (PFN_eglMakeCurrent)dlsym(egl, "eglMakeCurrent");
    PFN_glGetString glGetString = (PFN_glGetString)dlsym(gles, "glGetString");
    PFN_glClearColor glClearColor = (PFN_glClearColor)dlsym(gles, "glClearColor");
    PFN_glClear glClear = (PFN_glClear)dlsym(gles, "glClear");
    PFN_eglDestroySurface destroySurface = (PFN_eglDestroySurface)dlsym(egl, "eglDestroySurface");
    PFN_eglDestroyContext destroyContext = (PFN_eglDestroyContext)dlsym(egl, "eglDestroyContext");
    PFN_eglTerminate terminate = (PFN_eglTerminate)dlsym(egl, "eglTerminate");

    if (!getDisplay || !initialize || !chooseConfig || !createContext || !makeCurrent) {
        __android_log_print(ANDROID_LOG_INFO, "KuDroidGPU", "GL TEST: Missing core EGL functions!");
        return GPU_EGL_NO_GETDISPLAY;
    }

    EGLDisplay display = getDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY) {
        __android_log_print(ANDROID_LOG_INFO, "KuDroidGPU", "GL TEST: eglGetDisplay failed.");
        return -5;
    }

    EGLint major, minor;
    if (initialize(display, &major, &minor) != EGL_TRUE) {
        __android_log_print(ANDROID_LOG_INFO, "KuDroidGPU", "GL TEST: eglInitialize failed.");
        return -6;
    }
    __android_log_print(ANDROID_LOG_INFO, "KuDroidGPU", "GL TEST: EGL Initialized (v%d.%d)", major, minor);

    const EGLint configAttribs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE
    };
    
    EGLConfig config;
    EGLint numConfigs;
    if (chooseConfig(display, configAttribs, &config, 1, &numConfigs) != EGL_TRUE || numConfigs < 1) {
        __android_log_print(ANDROID_LOG_INFO, "KuDroidGPU", "GL TEST: eglChooseConfig failed.");
        return -7;
    }
    
    const EGLint pbufferAttribs[] = {
        EGL_WIDTH, 128,
        EGL_HEIGHT, 128,
        EGL_NONE
    };
    EGLSurface surface = createPbufferSurface(display, config, pbufferAttribs);
    if (surface == EGL_NO_SURFACE) {
        __android_log_print(ANDROID_LOG_INFO, "KuDroidGPU", "GL TEST: eglCreatePbufferSurface failed.");
        return -8;
    }
    
    const EGLint contextAttribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };
    EGLContext context = createContext(display, config, EGL_NO_CONTEXT, contextAttribs);
    if (context == EGL_NO_CONTEXT) {
        __android_log_print(ANDROID_LOG_INFO, "KuDroidGPU", "GL TEST: eglCreateContext failed.");
        return -9;
    }

    if (makeCurrent(display, surface, surface, context) != EGL_TRUE) {
        __android_log_print(ANDROID_LOG_INFO, "KuDroidGPU", "GL TEST: eglMakeCurrent failed.");
        return -10;
    }

    __android_log_print(ANDROID_LOG_INFO, "KuDroidGPU", "GL TEST: GL_VERSION = %s", glGetString ? (const char*)glGetString(GL_VERSION) : "NULL");
    __android_log_print(ANDROID_LOG_INFO, "KuDroidGPU", "GL TEST: GL_VENDOR = %s", glGetString ? (const char*)glGetString(GL_VENDOR) : "NULL");
    __android_log_print(ANDROID_LOG_INFO, "KuDroidGPU", "GL TEST: GL_RENDERER = %s", glGetString ? (const char*)glGetString(GL_RENDERER) : "NULL");

    if (glClearColor && glClear) {
        glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        __android_log_print(ANDROID_LOG_INFO, "KuDroidGPU", "GL TEST: glClear executed successfully.");
    }

    destroySurface(display, surface);
    destroyContext(display, context);
    terminate(display);

    __android_log_print(ANDROID_LOG_INFO, "KuDroidGPU", "GL TEST: EGL resources cleaned up successfully.");

    return result;
}
