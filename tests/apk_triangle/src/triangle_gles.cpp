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
    "attribute vec4 vPosition;\n"
    "attribute vec4 vColor;\n"
    "varying vec4 fColor;\n"
    "void main() {\n"
    "  gl_Position = vPosition;\n"
    "  fColor = vColor;\n"
    "}\n";

// Fragment Shader
static const char* fragmentShaderCode =
    "precision mediump float;\n"
    "varying vec4 fColor;\n"
    "void main() {\n"
    "  gl_FragColor = fColor;\n"
    "}\n";

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
    if (!libEGL) {
        LOGE("Failed to dlopen libEGL.so");
        return nullptr;
    }

    auto eglGetDisplay = (EGLDisplay (*)(EGLNativeDisplayType)) dlsym(libEGL, "eglGetDisplay");
    auto eglInitialize = (EGLBoolean (*)(EGLDisplay, EGLint*, EGLint*)) dlsym(libEGL, "eglInitialize");
    auto eglChooseConfig = (EGLBoolean (*)(EGLDisplay, const EGLint*, EGLConfig*, EGLint, EGLint*)) dlsym(libEGL, "eglChooseConfig");
    auto eglCreateWindowSurface = (EGLSurface (*)(EGLDisplay, EGLConfig, EGLNativeWindowType, const EGLint*)) dlsym(libEGL, "eglCreateWindowSurface");
    auto eglCreateContext = (EGLContext (*)(EGLDisplay, EGLConfig, EGLContext, const EGLint*)) dlsym(libEGL, "eglCreateContext");
    auto eglMakeCurrent = (EGLBoolean (*)(EGLDisplay, EGLSurface, EGLSurface, EGLContext)) dlsym(libEGL, "eglMakeCurrent");
    auto eglSwapBuffers = (EGLBoolean (*)(EGLDisplay, EGLSurface)) dlsym(libEGL, "eglSwapBuffers");
    auto eglSwapInterval = (EGLBoolean (*)(EGLDisplay, EGLint)) dlsym(libEGL, "eglSwapInterval");

    EGLDisplay display = eglGetDisplay ? eglGetDisplay(EGL_DEFAULT_DISPLAY) : EGL_NO_DISPLAY;
    if (display == EGL_NO_DISPLAY) {
        LOGE("eglGetDisplay failed!");
        return nullptr;
    }

    EGLint major = 0, minor = 0;
    eglInitialize(display, &major, &minor);

    const EGLint attribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_BLUE_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_RED_SIZE, 8,
        EGL_NONE
    };

    EGLConfig config;
    EGLint numConfigs = 0;
    eglChooseConfig(display, attribs, &config, 1, &numConfigs);
    
    // Đợi UI layout hoàn tất
    usleep(200000);

    EGLSurface surface = eglCreateWindowSurface(display, config, window, nullptr);
    if (surface == EGL_NO_SURFACE) {
        LOGE("eglCreateWindowSurface failed!");
        return nullptr;
    }

    const EGLint contextAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext context = eglCreateContext(display, config, nullptr, contextAttribs);
    if (!context) {
        LOGE("eglCreateContext failed!");
        return nullptr;
    }

    eglMakeCurrent(display, surface, surface, context);
    if (eglSwapInterval) {
        eglSwapInterval(display, 1);
    }

    LOGI("EGL Setup Complete! Loading GL functions...");
    void* libGLES = dlopen("libGLESv2.so", RTLD_NOW);
    if (!libGLES) {
        LOGE("Failed to dlopen libGLESv2.so");
        return nullptr;
    }
    
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
    auto p_glViewport = (void (*)(GLint, GLint, GLsizei, GLsizei)) dlsym(libGLES, "glViewport");
    auto p_glGenBuffers = (void (*)(GLsizei, GLuint*)) dlsym(libGLES, "glGenBuffers");
    auto p_glBindBuffer = (void (*)(GLenum, GLuint)) dlsym(libGLES, "glBindBuffer");
    auto p_glBufferData = (void (*)(GLenum, GLsizeiptr, const void*, GLenum)) dlsym(libGLES, "glBufferData");

    if (!p_glCreateShader || !p_glClearColor || !p_glViewport || !p_glDrawArrays) {
        LOGE("Failed to load essential GL functions");
        return nullptr;
    }

    auto loadShaderFn = [&](GLenum type, const char* code) -> GLuint {
        GLuint shader = p_glCreateShader(type);
        const char* src = code;
        p_glShaderSource(shader, 1, &src, nullptr);
        p_glCompileShader(shader);
        GLint compiled = 0;
        p_glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
        if (!compiled) {
            GLint infoLen = 0;
            p_glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLen);
            if (infoLen > 1) {
                char* infoLog = (char*)malloc(infoLen);
                p_glGetShaderInfoLog(shader, infoLen, nullptr, infoLog);
                LOGE("Shader compile error:\n%s", infoLog);
                free(infoLog);
            }
            return 0;
        }
        return shader;
    };

    GLuint vertexShader = loadShaderFn(GL_VERTEX_SHADER, vertexShaderCode);
    GLuint fragmentShader = loadShaderFn(GL_FRAGMENT_SHADER, fragmentShaderCode);

    if (!vertexShader || !fragmentShader) {
        LOGE("Failed to compile shaders!");
        return nullptr;
    }

    GLuint program = p_glCreateProgram();
    p_glAttachShader(program, vertexShader);
    p_glAttachShader(program, fragmentShader);
    p_glLinkProgram(program);

    GLint linked = 0;
    p_glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        LOGE("Program linking failed!");
        return nullptr;
    }

    static const GLfloat vertices[] = {
        0.0f,  0.6f, 0.0f,
       -0.6f, -0.6f, 0.0f,
        0.6f, -0.6f, 0.0f
    };
    static const GLfloat colors[] = {
        1.0f, 0.2f, 0.2f, 1.0f, // Red
        0.2f, 1.0f, 0.2f, 1.0f, // Green
        0.2f, 0.4f, 1.0f, 1.0f  // Blue
    };

    GLuint vbo[2] = {0, 0};
    if (p_glGenBuffers && p_glBindBuffer && p_glBufferData) {
        p_glGenBuffers(2, vbo);
        p_glBindBuffer(GL_ARRAY_BUFFER, vbo[0]);
        p_glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
        p_glBindBuffer(GL_ARRAY_BUFFER, vbo[1]);
        p_glBufferData(GL_ARRAY_BUFFER, sizeof(colors), colors, GL_STATIC_DRAW);
        p_glBindBuffer(GL_ARRAY_BUFFER, 0);
        LOGI("VBO created successfully (%u, %u)", vbo[0], vbo[1]);
    }

    GLint positionHandle = p_glGetAttribLocation(program, "vPosition");
    GLint colorHandle = p_glGetAttribLocation(program, "vColor");

    typedef int (*ANativeWindow_getDim_t)(ANativeWindow*);
    auto kudroid_ANativeWindow_getWidth = (ANativeWindow_getDim_t) dlsym(RTLD_DEFAULT, "ANativeWindow_getWidth");
    auto kudroid_ANativeWindow_getHeight = (ANativeWindow_getDim_t) dlsym(RTLD_DEFAULT, "ANativeWindow_getHeight");
    int w = kudroid_ANativeWindow_getWidth ? kudroid_ANativeWindow_getWidth(window) : 1080;
    int h = kudroid_ANativeWindow_getHeight ? kudroid_ANativeWindow_getHeight(window) : 1920;
    if (w <= 0) w = 1080;
    if (h <= 0) h = 1920;

    LOGI("Render loop starting with viewport %dx%d.", w, h);
    render_running = true;
    while (render_running) {
        p_glViewport(0, 0, w, h);
        
        // Nền xanh Navy đẹp mắt
        p_glClearColor(0.08f, 0.12f, 0.24f, 1.0f);
        p_glClear(GL_COLOR_BUFFER_BIT);

        p_glUseProgram(program);

        if (positionHandle >= 0) {
            p_glEnableVertexAttribArray((GLuint)positionHandle);
            if (vbo[0] != 0) {
                p_glBindBuffer(GL_ARRAY_BUFFER, vbo[0]);
                p_glVertexAttribPointer((GLuint)positionHandle, 3, GL_FLOAT, GL_FALSE, 0, (const void*)0);
            } else {
                p_glVertexAttribPointer((GLuint)positionHandle, 3, GL_FLOAT, GL_FALSE, 0, vertices);
            }
        }

        if (colorHandle >= 0) {
            p_glEnableVertexAttribArray((GLuint)colorHandle);
            if (vbo[1] != 0) {
                p_glBindBuffer(GL_ARRAY_BUFFER, vbo[1]);
                p_glVertexAttribPointer((GLuint)colorHandle, 4, GL_FLOAT, GL_FALSE, 0, (const void*)0);
            } else {
                p_glVertexAttribPointer((GLuint)colorHandle, 4, GL_FLOAT, GL_FALSE, 0, colors);
            }
        }

        p_glDrawArrays(GL_TRIANGLES, 0, 3);
        
        if (!eglSwapBuffers(display, surface)) {
            LOGE("eglSwapBuffers failed!");
            break;
        }
        usleep(16666);
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
