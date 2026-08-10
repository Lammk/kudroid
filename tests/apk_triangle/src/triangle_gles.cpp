#include <jni.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <android/log.h>
#include <dlfcn.h>
#include <stdio.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "TriangleGLES", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "TriangleGLES", __VA_ARGS__)

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    LOGI("JNI_OnLoad called for TriangleGLES!");
    
    // Attempt to load EGL and initialize
    void* libEGL = dlopen("libEGL.so", RTLD_NOW);
    if (!libEGL) {
        LOGE("Failed to load libEGL.so");
        return JNI_VERSION_1_6;
    }
    
    auto eglGetDisplay = (EGLDisplay (*)(EGLNativeDisplayType)) dlsym(libEGL, "eglGetDisplay");
    auto eglInitialize = (EGLBoolean (*)(EGLDisplay, EGLint*, EGLint*)) dlsym(libEGL, "eglInitialize");
    
    if (eglGetDisplay && eglInitialize) {
        EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (display != EGL_NO_DISPLAY) {
            EGLint major, minor;
            if (eglInitialize(display, &major, &minor)) {
                LOGI("EGL Initialized successfully! Version: %d.%d", major, minor);
            } else {
                LOGE("eglInitialize failed");
            }
        } else {
            LOGE("eglGetDisplay failed");
        }
    } else {
        LOGE("Could not find EGL functions");
    }
    
    LOGI("TriangleGLES initialization complete. Returning JNI_VERSION_1_6.");
    return JNI_VERSION_1_6;
}
