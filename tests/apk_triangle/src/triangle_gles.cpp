#include <jni.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <android/log.h>
#include <android/native_window.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "TriangleGLES", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "TriangleGLES", __VA_ARGS__)

static pthread_t render_thread;
static volatile bool render_running = false;

// Vertex Shader
static const char* vertexShaderCode =
    "attribute vec4 vPosition;"
    "attribute vec4 vColor;"
    "varying vec4 fColor;"
    "void main() {"
    "  gl_Position = vPosition;"
    "  fColor = vColor;"
    "}";

// Fragment Shader
static const char* fragmentShaderCode =
    "precision mediump float;"
    "varying vec4 fColor;"
    "void main() {"
    "  gl_FragColor = fColor;"
    "}";

static GLuint loadShader(GLenum type, const char* shaderCode) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &shaderCode, NULL);
    glCompileShader(shader);
    
    GLint compiled;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        GLint infoLen = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLen);
        if (infoLen > 1) {
            char* infoLog = (char*)malloc(sizeof(char) * infoLen);
            glGetShaderInfoLog(shader, infoLen, NULL, infoLog);
            LOGE("Error compiling shader:\n%s", infoLog);
            free(infoLog);
        }
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static void* render_loop(void* arg) {
    LOGI("Render thread started.");

    typedef ANativeWindow* (*ANativeWindow_fromSurface_t)(JNIEnv*, jobject);
    auto kudroid_ANativeWindow_fromSurface = (ANativeWindow_fromSurface_t) dlsym(RTLD_DEFAULT, "ANativeWindow_fromSurface");
    
    if (!kudroid_ANativeWindow_fromSurface) {
        LOGE("Failed to resolve ANativeWindow_fromSurface");
        return nullptr;
    }

    ANativeWindow* window = kudroid_ANativeWindow_fromSurface(nullptr, nullptr);
    if (!window) {
        LOGE("Failed to get ANativeWindow");
        return nullptr;
    }

    void* libEGL = dlopen("libEGL.so", RTLD_NOW);
    auto eglGetDisplay = (EGLDisplay (*)(EGLNativeDisplayType)) dlsym(libEGL, "eglGetDisplay");
    auto eglInitialize = (EGLBoolean (*)(EGLDisplay, EGLint*, EGLint*)) dlsym(libEGL, "eglInitialize");
    auto eglChooseConfig = (EGLBoolean (*)(EGLDisplay, const EGLint*, EGLConfig*, EGLint, EGLint*)) dlsym(libEGL, "eglChooseConfig");
    auto eglCreateWindowSurface = (EGLSurface (*)(EGLDisplay, EGLConfig, EGLNativeWindowType, const EGLint*)) dlsym(libEGL, "eglCreateWindowSurface");
    auto eglCreateContext = (EGLContext (*)(EGLDisplay, EGLConfig, EGLContext, const EGLint*)) dlsym(libEGL, "eglCreateContext");
    auto eglMakeCurrent = (EGLBoolean (*)(EGLDisplay, EGLSurface, EGLSurface, EGLContext)) dlsym(libEGL, "eglMakeCurrent");
    auto eglSwapBuffers = (EGLBoolean (*)(EGLDisplay, EGLSurface)) dlsym(libEGL, "eglSwapBuffers");

    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(display, 0, 0);

    const EGLint attribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_BLUE_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_RED_SIZE, 8,
        EGL_NONE
    };

    EGLConfig config;
    EGLint numConfigs;
    eglChooseConfig(display, attribs, &config, 1, &numConfigs);
    
    // ANGLE on iOS actually wants the UIView, but accepts CALayer. 
    // Sleep a tiny bit to allow iOS UI layout to finish.
    usleep(500000); 

    EGLSurface surface = eglCreateWindowSurface(display, config, window, nullptr);
    if (surface == EGL_NO_SURFACE) {
        LOGE("eglCreateWindowSurface failed!");
        return nullptr;
    }

    const EGLint contextAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext context = eglCreateContext(display, config, nullptr, contextAttribs);
    eglMakeCurrent(display, surface, surface, context);

    LOGI("EGL Setup Complete! Loading GL functions...");
    void* libGLES = dlopen("libGLESv2.so", RTLD_NOW);
    
    auto p_glCreateShader = (GLuint (*)(GLenum)) dlsym(libGLES, "glCreateShader");
    auto p_glShaderSource = (void (*)(GLuint, GLsizei, const GLchar**, const GLint*)) dlsym(libGLES, "glShaderSource");
    auto p_glCompileShader = (void (*)(GLuint)) dlsym(libGLES, "glCompileShader");
    auto p_glGetShaderiv = (void (*)(GLuint, GLenum, GLint*)) dlsym(libGLES, "glGetShaderiv");
    auto p_glGetShaderInfoLog = (void (*)(GLuint, GLsizei, GLsizei*, GLchar*)) dlsym(libGLES, "glGetShaderInfoLog");
    auto p_glCreateProgram = (GLuint (*)()) dlsym(libGLES, "glCreateProgram");
    auto p_glAttachShader = (void (*)(GLuint, GLuint)) dlsym(libGLES, "glAttachShader");
    auto p_glLinkProgram = (void (*)(GLuint)) dlsym(libGLES, "glLinkProgram");
    auto p_glGetProgramiv = (void (*)(GLuint, GLenum, GLint*)) dlsym(libGLES, "glGetProgramiv");
    auto p_glUseProgram = (void (*)(GLuint)) dlsym(libGLES, "glUseProgram");
    auto p_glGetAttribLocation = (GLint (*)(GLuint, const GLchar*)) dlsym(libGLES, "glGetAttribLocation");
    auto p_glEnableVertexAttribArray = (void (*)(GLuint)) dlsym(libGLES, "glEnableVertexAttribArray");
    auto p_glVertexAttribPointer = (void (*)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*)) dlsym(libGLES, "glVertexAttribPointer");
    auto p_glClearColor = (void (*)(GLclampf, GLclampf, GLclampf, GLclampf)) dlsym(libGLES, "glClearColor");
    auto p_glClear = (void (*)(GLbitfield)) dlsym(libGLES, "glClear");
    auto p_glDrawArrays = (void (*)(GLenum, GLint, GLsizei)) dlsym(libGLES, "glDrawArrays");

    if (!p_glCreateShader || !p_glClearColor) {
        LOGE("Failed to load GL functions");
        return nullptr;
    }

    auto loadShaderFn = [&](GLenum type, const char* code) -> GLuint {
        GLuint shader = p_glCreateShader(type);
        p_glShaderSource(shader, 1, &code, nullptr);
        p_glCompileShader(shader);
        GLint compiled;
        p_glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
        if (!compiled) {
            LOGE("Shader compile error");
        }
        return shader;
    };

    GLuint vertexShader = loadShaderFn(GL_VERTEX_SHADER, vertexShaderCode);
    GLuint fragmentShader = loadShaderFn(GL_FRAGMENT_SHADER, fragmentShaderCode);

    GLuint program = p_glCreateProgram();
    p_glAttachShader(program, vertexShader);
    p_glAttachShader(program, fragmentShader);
    p_glLinkProgram(program);
    p_glUseProgram(program);

    GLfloat vertices[] = { 0.0f, 0.5f, 0.0f, -0.5f, -0.5f, 0.0f, 0.5f, -0.5f, 0.0f };
    GLfloat colors[] = { 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f };

    GLuint positionHandle = p_glGetAttribLocation(program, "vPosition");
    p_glEnableVertexAttribArray(positionHandle);
    p_glVertexAttribPointer(positionHandle, 3, GL_FLOAT, GL_FALSE, 0, vertices);

    GLuint colorHandle = p_glGetAttribLocation(program, "vColor");
    p_glEnableVertexAttribArray(colorHandle);
    p_glVertexAttribPointer(colorHandle, 4, GL_FLOAT, GL_FALSE, 0, colors);

    LOGI("Render loop starting.");
    render_running = true;
    while (render_running) {
        p_glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
        p_glClear(GL_COLOR_BUFFER_BIT);
        p_glDrawArrays(GL_TRIANGLES, 0, 3);
        eglSwapBuffers(display, surface);
        usleep(16000);
    }
    
    LOGI("Render thread exiting.");
    return nullptr;
}

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    LOGI("JNI_OnLoad called for TriangleGLES! Spawning render thread...");
    pthread_create(&render_thread, nullptr, render_loop, nullptr);
    pthread_detach(render_thread);
    return JNI_VERSION_1_6;
}
