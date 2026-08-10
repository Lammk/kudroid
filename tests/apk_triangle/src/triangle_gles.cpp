#include <jni.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <android/log.h>
#include <android/native_window.h>
#include <dlfcn.h>
#include <stdio.h>
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

    // Retrieve ANativeWindow_fromSurface from Kudroid (which acts as libandroid.so)
    // Kudroid exports this symbol globally.
    typedef ANativeWindow* (*ANativeWindow_fromSurface_t)(JNIEnv*, jobject);
    auto kudroid_ANativeWindow_fromSurface = (ANativeWindow_fromSurface_t) dlsym(RTLD_DEFAULT, "ANativeWindow_fromSurface");
    
    if (!kudroid_ANativeWindow_fromSurface) {
        LOGE("Failed to resolve ANativeWindow_fromSurface");
        return nullptr;
    }

    // Pass NULL because Kudroid's implementation ignores env and surface, directly returning g_metalLayer.
    ANativeWindow* window = kudroid_ANativeWindow_fromSurface(nullptr, nullptr);
    if (!window) {
        LOGE("Failed to get ANativeWindow (g_metalLayer is probably null)");
        return nullptr;
    }

    void* libEGL = dlopen("libEGL.so", RTLD_NOW);
    if (!libEGL) {
        LOGE("Failed to load libEGL.so");
        return nullptr;
    }

    auto eglGetDisplay = (EGLDisplay (*)(EGLNativeDisplayType)) dlsym(libEGL, "eglGetDisplay");
    auto eglInitialize = (EGLBoolean (*)(EGLDisplay, EGLint*, EGLint*)) dlsym(libEGL, "eglInitialize");
    auto eglChooseConfig = (EGLBoolean (*)(EGLDisplay, const EGLint*, EGLConfig*, EGLint, EGLint*)) dlsym(libEGL, "eglChooseConfig");
    auto eglCreateWindowSurface = (EGLSurface (*)(EGLDisplay, EGLConfig, EGLNativeWindowType, const EGLint*)) dlsym(libEGL, "eglCreateWindowSurface");
    auto eglCreateContext = (EGLContext (*)(EGLDisplay, EGLConfig, EGLContext, const EGLint*)) dlsym(libEGL, "eglCreateContext");
    auto eglMakeCurrent = (EGLBoolean (*)(EGLDisplay, EGLSurface, EGLSurface, EGLContext)) dlsym(libEGL, "eglMakeCurrent");
    auto eglSwapBuffers = (EGLBoolean (*)(EGLDisplay, EGLSurface)) dlsym(libEGL, "eglSwapBuffers");

    if (!eglGetDisplay || !eglInitialize) {
        LOGE("EGL functions not found in libEGL.so");
        return nullptr;
    }

    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY) {
        LOGE("eglGetDisplay failed");
        return nullptr;
    }

    if (!eglInitialize(display, 0, 0)) {
        LOGE("eglInitialize failed");
        return nullptr;
    }

    const EGLint attribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_BLUE_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_RED_SIZE, 8,
        EGL_NONE
    };

    EGLConfig config;
    EGLint numConfigs;
    if (!eglChooseConfig(display, attribs, &config, 1, &numConfigs) || numConfigs <= 0) {
        LOGE("eglChooseConfig failed");
        return nullptr;
    }

    EGLSurface surface = eglCreateWindowSurface(display, config, window, nullptr);
    if (surface == EGL_NO_SURFACE) {
        LOGE("eglCreateWindowSurface failed");
        return nullptr;
    }

    const EGLint contextAttribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };
    
    EGLContext context = eglCreateContext(display, config, nullptr, contextAttribs);
    if (context == EGL_NO_CONTEXT) {
        LOGE("eglCreateContext failed");
        return nullptr;
    }

    if (!eglMakeCurrent(display, surface, surface, context)) {
        LOGE("eglMakeCurrent failed");
        return nullptr;
    }

    LOGI("EGL Setup Complete! Compiling Shaders...");

    void* libGLES = dlopen("libGLESv2.so", RTLD_NOW);
    if (!libGLES) {
        LOGE("Failed to load libGLESv2.so");
        return nullptr;
    }

    GLuint vertexShader = loadShader(GL_VERTEX_SHADER, vertexShaderCode);
    GLuint fragmentShader = loadShader(GL_FRAGMENT_SHADER, fragmentShaderCode);

    GLuint program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);

    GLint linkStatus;
    glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);
    if (!linkStatus) {
        LOGE("Error linking program");
        return nullptr;
    }

    glUseProgram(program);

    GLfloat vertices[] = {
         0.0f,  0.5f, 0.0f,
        -0.5f, -0.5f, 0.0f,
         0.5f, -0.5f, 0.0f
    };
    
    GLfloat colors[] = {
        1.0f, 0.0f, 0.0f, 1.0f,
        0.0f, 1.0f, 0.0f, 1.0f,
        0.0f, 0.0f, 1.0f, 1.0f
    };

    GLuint positionHandle = glGetAttribLocation(program, "vPosition");
    glEnableVertexAttribArray(positionHandle);
    glVertexAttribPointer(positionHandle, 3, GL_FLOAT, GL_FALSE, 0, vertices);

    GLuint colorHandle = glGetAttribLocation(program, "vColor");
    glEnableVertexAttribArray(colorHandle);
    glVertexAttribPointer(colorHandle, 4, GL_FLOAT, GL_FALSE, 0, colors);

    LOGI("Render loop starting.");
    render_running = true;
    while (render_running) {
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glDrawArrays(GL_TRIANGLES, 0, 3);

        eglSwapBuffers(display, surface);
        
        // Target ~60 FPS
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
