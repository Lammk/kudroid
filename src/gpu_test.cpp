#include <string>
#include <cstring>
#include <vector>
#include "kudroid/platform/GraphicsShim.h"

extern "C" const char* kudroid_test_gpu(void) {
    std::string log = "=== KuDroid Host-Native GPU & Shader Compiler Test ===\n";

    typedef void* EGLDisplay;
    typedef void* EGLConfig;
    typedef void* EGLSurface;
    typedef void* EGLContext;
    typedef unsigned int (*PFN_eglInitialize)(EGLDisplay, int*, int*);
    typedef EGLDisplay (*PFN_eglGetDisplay)(void*);
    typedef int (*PFN_eglChooseConfig)(EGLDisplay, const int*, EGLConfig*, int, int*);
    typedef EGLSurface (*PFN_eglCreatePbufferSurface)(EGLDisplay, EGLConfig, const int*);
    typedef EGLContext (*PFN_eglCreateContext)(EGLDisplay, EGLConfig, EGLContext, const int*);
    typedef unsigned int (*PFN_eglMakeCurrent)(EGLDisplay, EGLSurface, EGLSurface, EGLContext);

    auto get_display = (PFN_eglGetDisplay) kudroid::get_egl_func("eglGetDisplay");
    auto init = (PFN_eglInitialize) kudroid::get_egl_func("eglInitialize");
    auto choose_config = (PFN_eglChooseConfig) kudroid::get_egl_func("eglChooseConfig");
    auto create_pbuffer = (PFN_eglCreatePbufferSurface) kudroid::get_egl_func("eglCreatePbufferSurface");
    auto create_context = (PFN_eglCreateContext) kudroid::get_egl_func("eglCreateContext");
    auto make_current = (PFN_eglMakeCurrent) kudroid::get_egl_func("eglMakeCurrent");

    auto gl_create_shader = (unsigned int (*)(unsigned int)) kudroid::get_gl_func("glCreateShader");
    auto gl_shader_source = (void (*)(unsigned int, int, const char* const*, const int*)) kudroid::get_gl_func("glShaderSource");
    auto gl_compile_shader = (void (*)(unsigned int)) kudroid::get_gl_func("glCompileShader");
    auto gl_get_shaderiv = (void (*)(unsigned int, unsigned int, int*)) kudroid::get_gl_func("glGetShaderiv");
    auto gl_get_shader_info_log = (void (*)(unsigned int, int, int*, char*)) kudroid::get_gl_func("glGetShaderInfoLog");
    auto gl_delete_shader = (void (*)(unsigned int)) kudroid::get_gl_func("glDeleteShader");

    if (!get_display || !init || !choose_config || !create_pbuffer || !create_context || !make_current ||
        !gl_create_shader || !gl_shader_source || !gl_compile_shader || !gl_get_shaderiv) {
        log += "❌ ERROR: Required EGL/GLES entry points missing from host frameworks!\n";
        return strdup(log.c_str());
    }

    EGLDisplay dpy = get_display(nullptr);
    if (!dpy) {
        log += "❌ ERROR: eglGetDisplay returned NULL\n";
        return strdup(log.c_str());
    }

    int major = 0, minor = 0;
    if (!init(dpy, &major, &minor)) {
        log += "❌ ERROR: eglInitialize failed\n";
        return strdup(log.c_str());
    }
    log += "✔ EGL Initialized: version " + std::to_string(major) + "." + std::to_string(minor) + "\n";

    const int configAttribs[] = {
        0x3033, 0x0001, // EGL_SURFACE_TYPE, EGL_PBUFFER_BIT
        0x3040, 0x0004, // EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT
        0x3038          // EGL_NONE
    };
    EGLConfig config = nullptr;
    int numConfigs = 0;
    if (!choose_config(dpy, configAttribs, &config, 1, &numConfigs) || numConfigs < 1) {
        log += "❌ ERROR: eglChooseConfig failed\n";
        return strdup(log.c_str());
    }

    const int pbufferAttribs[] = { 0x3057, 64, 0x3056, 64, 0x3038 };
    EGLSurface surface = create_pbuffer(dpy, config, pbufferAttribs);
    const int contextAttribs[] = { 0x3098, 2, 0x3038 };
    EGLContext context = create_context(dpy, config, nullptr, contextAttribs);

    if (!surface || !context || !make_current(dpy, surface, surface, context)) {
        log += "❌ ERROR: Failed to create/bind Pbuffer surface and EGL context\n";
        return strdup(log.c_str());
    }
    log += "✔ EGL Pbuffer Surface & Context active on Host thread\n";

    // Host-Native Shader Code (Pure C++ Literal)
    const char* vShaderSource =
        "#version 300 es\n"
        "layout(location = 0) in vec4 vPosition;\n"
        "void main() {\n"
        "  gl_Position = vPosition;\n"
        "}\n";

    log += "⏳ Creating Shader (GL_VERTEX_SHADER = 0x8B31)...\n";
    unsigned int shader = gl_create_shader(0x8B31);
    if (shader == 0) {
        log += "❌ ERROR: glCreateShader returned 0\n";
        return strdup(log.c_str());
    }
    log += "✔ glCreateShader returned shader ID: " + std::to_string(shader) + "\n";

    log += "⏳ Passing shader source to ANGLE Metal...\n";
    const char* srcPtrs[] = { vShaderSource };
    int srcLens[] = { static_cast<int>(strlen(vShaderSource)) };
    gl_shader_source(shader, 1, srcPtrs, srcLens);
    log += "✔ glShaderSource completed without abort!\n";

    log += "⏳ Compiling shader with ANGLE Metal compiler...\n";
    gl_compile_shader(shader);
    log += "✔ glCompileShader completed without abort!\n";

    int compileStatus = 0;
    gl_get_shaderiv(shader, 0x8B81 /* GL_COMPILE_STATUS */, &compileStatus);

    char infoLog[1024] = {0};
    int infoLen = 0;
    if (gl_get_shader_info_log) {
        gl_get_shader_info_log(shader, sizeof(infoLog), &infoLen, infoLog);
    }

    if (compileStatus) {
        log += "🎉 SUCCESS: ANGLE Metal Shader Compiler works 100% on this iOS device!\n";
    } else {
        log += "⚠ Shader Compilation Failed! InfoLog: " + std::string(infoLog) + "\n";
    }

    if (gl_delete_shader) gl_delete_shader(shader);
    make_current(dpy, nullptr, nullptr, nullptr);

    return strdup(log.c_str());
}
