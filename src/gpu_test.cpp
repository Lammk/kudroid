#include <string>
#include <cstring>
#include <vector>
#include <cstdint>
#include "kudroid/platform/GraphicsShim.h"

extern "C" const char* kudroid_test_gpu(void) {
    std::string log = "=== KuDroid Deep GPU Diagnostic & Hardware Validation ===\n";

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
    typedef const char* (*PFN_eglQueryString)(EGLDisplay, int);

    auto get_display = (PFN_eglGetDisplay) kudroid::get_egl_func("eglGetDisplay");
    auto init = (PFN_eglInitialize) kudroid::get_egl_func("eglInitialize");
    auto choose_config = (PFN_eglChooseConfig) kudroid::get_egl_func("eglChooseConfig");
    auto create_pbuffer = (PFN_eglCreatePbufferSurface) kudroid::get_egl_func("eglCreatePbufferSurface");
    auto create_context = (PFN_eglCreateContext) kudroid::get_egl_func("eglCreateContext");
    auto make_current = (PFN_eglMakeCurrent) kudroid::get_egl_func("eglMakeCurrent");
    auto egl_query_string = (PFN_eglQueryString) kudroid::get_egl_func("eglQueryString");

    auto gl_get_string = (const char* (*)(unsigned int)) kudroid::get_gl_func("glGetString");
    auto gl_get_integerv = (void (*)(unsigned int, int*)) kudroid::get_gl_func("glGetIntegerv");
    auto gl_get_error = (unsigned int (*)(void)) kudroid::get_gl_func("glGetError");

    auto gl_create_shader = (unsigned int (*)(unsigned int)) kudroid::get_gl_func("glCreateShader");
    auto gl_shader_source = (void (*)(unsigned int, int, const char* const*, const int*)) kudroid::get_gl_func("glShaderSource");
    auto gl_compile_shader = (void (*)(unsigned int)) kudroid::get_gl_func("glCompileShader");
    auto gl_get_shaderiv = (void (*)(unsigned int, unsigned int, int*)) kudroid::get_gl_func("glGetShaderiv");
    auto gl_get_shader_info_log = (void (*)(unsigned int, int, int*, char*)) kudroid::get_gl_func("glGetShaderInfoLog");
    auto gl_delete_shader = (void (*)(unsigned int)) kudroid::get_gl_func("glDeleteShader");

    auto gl_create_program = (unsigned int (*)(void)) kudroid::get_gl_func("glCreateProgram");
    auto gl_attach_shader = (void (*)(unsigned int, unsigned int)) kudroid::get_gl_func("glAttachShader");
    auto gl_link_program = (void (*)(unsigned int)) kudroid::get_gl_func("glLinkProgram");
    auto gl_get_programiv = (void (*)(unsigned int, unsigned int, int*)) kudroid::get_gl_func("glGetProgramiv");
    auto gl_get_program_info_log = (void (*)(unsigned int, int, int*, char*)) kudroid::get_gl_func("glGetProgramInfoLog");
    auto gl_use_program = (void (*)(unsigned int)) kudroid::get_gl_func("glUseProgram");
    auto gl_delete_program = (void (*)(unsigned int)) kudroid::get_gl_func("glDeleteProgram");

    auto gl_viewport = (void (*)(int, int, int, int)) kudroid::get_gl_func("glViewport");
    auto gl_clear_color = (void (*)(float, float, float, float)) kudroid::get_gl_func("glClearColor");
    auto gl_clear = (void (*)(unsigned int)) kudroid::get_gl_func("glClear");
    auto gl_get_attrib_location = (int (*)(unsigned int, const char*)) kudroid::get_gl_func("glGetAttribLocation");
    auto gl_enable_vertex_attrib_array = (void (*)(unsigned int)) kudroid::get_gl_func("glEnableVertexAttribArray");
    auto gl_vertex_attrib_pointer = (void (*)(unsigned int, int, unsigned int, unsigned char, int, const void*)) kudroid::get_gl_func("glVertexAttribPointer");
    auto gl_draw_arrays = (void (*)(unsigned int, int, int)) kudroid::get_gl_func("glDrawArrays");
    auto gl_read_pixels = (void (*)(int, int, int, int, unsigned int, unsigned int, void*)) kudroid::get_gl_func("glReadPixels");

    if (!get_display || !init || !choose_config || !create_pbuffer || !create_context || !make_current) {
        log += "❌ ERROR: Required EGL entry points missing!\n";
        return strdup(log.c_str());
    }

    EGLDisplay dpy = get_display(nullptr);
    int major = 0, minor = 0;
    if (!init(dpy, &major, &minor)) {
        log += "❌ ERROR: eglInitialize failed\n";
        return strdup(log.c_str());
    }
    log += "✔ EGL Initialized: version " + std::to_string(major) + "." + std::to_string(minor) + "\n";
    if (egl_query_string) {
        const char* eglVendor = egl_query_string(dpy, 0x3053 /* EGL_VENDOR */);
        const char* eglVersion = egl_query_string(dpy, 0x3054 /* EGL_VERSION */);
        log += "  [EGL Vendor]: " + std::string(eglVendor ? eglVendor : "null") + "\n";
        log += "  [EGL Version]: " + std::string(eglVersion ? eglVersion : "null") + "\n";
    }

    const int configAttribs[] = {
        0x3033, 0x0001, // EGL_SURFACE_TYPE, EGL_PBUFFER_BIT
        0x3040, 0x0004, // EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT
        0x3024, 8, 0x3023, 8, 0x3022, 8, 0x3021, 8, // RGBA8
        0x3038
    };
    EGLConfig config = nullptr;
    int numConfigs = 0;
    choose_config(dpy, configAttribs, &config, 1, &numConfigs);

    const int pbufferAttribs[] = { 0x3057, 64, 0x3056, 64, 0x3038 };
    EGLSurface surface = create_pbuffer(dpy, config, pbufferAttribs);
    const int contextAttribs[] = { 0x3098, 2, 0x3038 };
    EGLContext context = create_context(dpy, config, nullptr, contextAttribs);
    make_current(dpy, surface, surface, context);

    // ── 1. Query Hardware & Driver Properties ──
    log += "\n--- 1. GPU & Driver Metadata ---\n";
    if (gl_get_string) {
        const char* glVendor = gl_get_string(0x1F00 /* GL_VENDOR */);
        const char* glRenderer = gl_get_string(0x1F01 /* GL_RENDERER */);
        const char* glVersion = gl_get_string(0x1F02 /* GL_VERSION */);
        const char* glSlVersion = gl_get_string(0x8B8C /* GL_SHADING_LANGUAGE_VERSION */);
        log += "  [GL_VENDOR]: " + std::string(glVendor ? glVendor : "null") + "\n";
        log += "  [GL_RENDERER]: " + std::string(glRenderer ? glRenderer : "null") + "\n";
        log += "  [GL_VERSION]: " + std::string(glVersion ? glVersion : "null") + "\n";
        log += "  [GL_SHADING_LANGUAGE_VERSION]: " + std::string(glSlVersion ? glSlVersion : "null") + "\n";
    }

    if (gl_get_integerv) {
        int maxTex = 0, maxAttribs = 0, maxVarying = 0;
        gl_get_integerv(0x0D33 /* GL_MAX_TEXTURE_SIZE */, &maxTex);
        gl_get_integerv(0x8869 /* GL_MAX_VERTEX_ATTRIBS */, &maxAttribs);
        gl_get_integerv(0x8B4B /* GL_MAX_VARYING_VECTORS */, &maxVarying);
        log += "  [Limits]: MaxTexture=" + std::to_string(maxTex) +
               " MaxVertexAttribs=" + std::to_string(maxAttribs) +
               " MaxVaryingVectors=" + std::to_string(maxVarying) + "\n";
    }

    // ── 2. Shader & Program Pipeline Test ──
    log += "\n--- 2. Shader Compilation & Program Link Test ---\n";
    const char* vShaderSource =
        "attribute vec4 aPosition;\n"
        "void main() {\n"
        "  gl_Position = aPosition;\n"
        "}\n";

    const char* fShaderSource =
        "precision mediump float;\n"
        "void main() {\n"
        "  gl_FragColor = vec4(0.0, 1.0, 0.0, 1.0);\n" // Solid Green
        "}\n";

    unsigned int vShader = gl_create_shader(0x8B31);
    const char* vPtrs[] = { vShaderSource };
    gl_shader_source(vShader, 1, vPtrs, nullptr);
    gl_compile_shader(vShader);
    int vStatus = 0;
    gl_get_shaderiv(vShader, 0x8B81, &vStatus);
    log += "  Vertex Shader (handle=" + std::to_string(vShader) + "): " + (vStatus ? "✔ COMPILED" : "❌ FAILED") + "\n";

    unsigned int fShader = gl_create_shader(0x8B30);
    const char* fPtrs[] = { fShaderSource };
    gl_shader_source(fShader, 1, fPtrs, nullptr);
    gl_compile_shader(fShader);
    int fStatus = 0;
    gl_get_shaderiv(fShader, 0x8B81, &fStatus);
    log += "  Fragment Shader (handle=" + std::to_string(fShader) + "): " + (fStatus ? "✔ COMPILED" : "❌ FAILED") + "\n";

    unsigned int program = gl_create_program();
    gl_attach_shader(program, vShader);
    gl_attach_shader(program, fShader);
    gl_link_program(program);
    int linkStatus = 0;
    gl_get_programiv(program, 0x8B82, &linkStatus);
    log += "  Program Link (handle=" + std::to_string(program) + "): " + (linkStatus ? "✔ LINKED" : "❌ FAILED") + "\n";

    // ── 3. Render Pipeline & Pixel Readback Verification ──
    log += "\n--- 3. Draw Call & Pixel Readback Verification ---\n";
    if (linkStatus && gl_viewport && gl_clear_color && gl_clear && gl_use_program &&
        gl_get_attrib_location && gl_enable_vertex_attrib_array && gl_vertex_attrib_pointer &&
        gl_draw_arrays && gl_read_pixels) {

        gl_viewport(0, 0, 64, 64);
        gl_clear_color(0.1f, 0.2f, 0.3f, 1.0f); // Dark Blue background
        gl_clear(0x00004000 /* GL_COLOR_BUFFER_BIT */);

        gl_use_program(program);
        int posLoc = gl_get_attrib_location(program, "aPosition");
        log += "  Attrib 'aPosition' Location: " + std::to_string(posLoc) + "\n";

        // Confirm there are no GL errors after pipeline setup
        const unsigned int glErr = gl_get_error();
        log += "  glGetError after pipeline setup: " +
               std::string(glErr == 0 ? "GL_NO_ERROR (0)" : "0x" + [&] {
                   char hex[16];
                   std::snprintf(hex, sizeof(hex), "%X", glErr);
                   return std::string(hex);
               }()) + "\n";

        // Fullscreen covering triangle
        const float vertices[] = {
            -1.0f, -1.0f, 0.0f,
             3.0f, -1.0f, 0.0f,
            -1.0f,  3.0f, 0.0f
        };

        gl_enable_vertex_attrib_array(posLoc);
        gl_vertex_attrib_pointer(posLoc, 3, 0x1406 /* GL_FLOAT */, 0, 0, vertices);
        gl_draw_arrays(0x0004 /* GL_TRIANGLES */, 0, 3);
        log += "  glDrawArrays(GL_TRIANGLES, count=3) executed\n";

        // Read back pixel at center (32, 32)
        uint8_t pixel[4] = {0, 0, 0, 0};
        gl_read_pixels(32, 32, 1, 1, 0x1908 /* GL_RGBA */, 0x1401 /* GL_UNSIGNED_BYTE */, pixel);
        log += "  Pixel Readback at (32, 32): RGBA=(" +
               std::to_string((int)pixel[0]) + ", " +
               std::to_string((int)pixel[1]) + ", " +
               std::to_string((int)pixel[2]) + ", " +
               std::to_string((int)pixel[3]) + ")\n";

        if (pixel[1] > 200 && pixel[0] < 50 && pixel[2] < 50) {
            log += "🎉 VERIFIED: Green Pixel Rendered Successfully! Metal GPU Rasterizer is 100% OPERATIONAL!\n";
        } else {
            log += "⚠ WARNING: Pixel color mismatch (expected Green (0, 255, 0, 255))\n";
        }
    }

    if (gl_delete_program) gl_delete_program(program);
    if (gl_delete_shader) {
        gl_delete_shader(vShader);
        gl_delete_shader(fShader);
    }
    make_current(dpy, nullptr, nullptr, nullptr);

    log += "\n=== End of GPU Diagnostics ===\n";
    return strdup(log.c_str());
}
