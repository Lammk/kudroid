#include "kudroid/platform/SurfaceTextureHost.h"
#include "kudroid/Log.h"
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <dlfcn.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace kudroid {
void* get_gl_func(const char* name) {
    static void* library = dlopen("libGLESv2.so.2", RTLD_NOW | RTLD_GLOBAL);
    return library ? dlsym(library, name) : nullptr;
}
void* get_egl_func(const char* name) {
    static void* library = dlopen("libEGL.so.1", RTLD_NOW | RTLD_GLOBAL);
    return library ? dlsym(library, name) : nullptr;
}
namespace log { void write(Level, const char*, const char*, ...) {} }
}
extern "C" void kudroid_media_frame_post(int, long long) {}

#define GL(name) reinterpret_cast<decltype(&name)>(kudroid::get_gl_func(#name))
#define EGL(name) reinterpret_cast<decltype(&name)>(kudroid::get_egl_func(#name))

static GLuint compile(GLenum stage, const std::string& source) {
    const auto translated = kudroid::surface_texture::shader(source);
    const char* text = translated.c_str();
    GLuint shader = GL(glCreateShader)(stage);
    GL(glShaderSource)(shader, 1, &text, nullptr);
    GL(glCompileShader)(shader);
    GLint compiled = 0;
    GL(glGetShaderiv)(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        char message[2048]{};
        GL(glGetShaderInfoLog)(shader, sizeof(message), nullptr, message);
        std::fprintf(stderr, "Shader failed: %s\n%s\n", message, translated.c_str());
        std::exit(1);
    }
    return shader;
}
static GLuint make_program(const std::string& fragment) {
    const GLuint vertex = compile(GL_VERTEX_SHADER,
        "#version 300 es\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.0-1.0,0,1);}");
    const GLuint pixel = compile(GL_FRAGMENT_SHADER, fragment);
    GLuint program = GL(glCreateProgram)();
    GL(glProgramParameteri)(program, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);
    GL(glAttachShader)(program, vertex); GL(glAttachShader)(program, pixel); GL(glLinkProgram)(program);
    GLint linked = 0;
    GL(glGetProgramiv)(program, GL_LINK_STATUS, &linked);
    if (!linked) std::exit(2);
    GL(glDeleteShader)(vertex); GL(glDeleteShader)(pixel);
    return program;
}

int main() {
    if (!kudroid::get_egl_func("eglGetDisplay") || !kudroid::get_gl_func("glCreateShader")) return 77;
    EGLDisplay display = EGL(eglGetDisplay)(EGL_DEFAULT_DISPLAY);
    if (!EGL(eglInitialize)(display, nullptr, nullptr)) return 77;
    const EGLint attributes[] = {EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,0x40,
        EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_NONE};
    EGLConfig config;
    EGLint count = 0;
    if (!EGL(eglChooseConfig)(display, attributes, &config, 1, &count) || !count) return 77;
    const EGLint size[] = {EGL_WIDTH,2,EGL_HEIGHT,2,EGL_NONE};
    EGLSurface surface = EGL(eglCreatePbufferSurface)(display, config, size);
    const EGLint version[] = {EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE};
    EGLContext context = EGL(eglCreateContext)(display, config, EGL_NO_CONTEXT, version);
    if (!EGL(eglMakeCurrent)(display, surface, surface, context)) return 77;
    using namespace kudroid::surface_texture;
    const GLuint videoProgram = make_program(
        "#version 300 es\n#extension GL_OES_EGL_image_external_essl3 : require\n"
        "precision mediump float;uniform samplerExternalOES frame;out vec4 color;"
        "void main(){color=texture(frame,vec2(0.5));}");
    const GLuint ordinaryProgram = make_program(
        "#version 300 es\nprecision mediump float;uniform sampler2D frame;out vec4 color;"
        "void main(){color=texture(frame,vec2(0.5));}");
    GLuint images[2]; GL(glGenTextures)(2, images);
    GL(glBindTexture)(GL_TEXTURE_2D, images[0]);
    GL(glTexParameteri)(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    const uint8_t green[] = {0,255,0,255};
    GL(glTexImage2D)(GL_TEXTURE_2D,0,GL_RGBA,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,green);
    kudroid_surface_texture_create(1, images[1]);
    if (!post(1, {81,81,81,81,90,240}, 2,2,333) || kudroid_surface_texture_update(1) != 333) return 3;
    GL(glViewport)(0,0,2,2);
    GL(glUseProgram)(videoProgram); use_program(videoProgram);
    GL(glUniform1i)(GL(glGetUniformLocation)(videoProgram,"kdExternalSampler_frame"),0);
    {
        DrawScope scope;
        GL(glDrawArrays)(GL_TRIANGLES,0,3);
    }
    uint8_t pixel[4]{};
    GL(glReadPixels)(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
    if (pixel[0] < 250 || pixel[1] > 3 || pixel[2] > 3 || GL(glGetError)() != GL_NO_ERROR) return 4;
    std::puts("PASS real EGL draw samples decoded red video pixels");
    GL(glUseProgram)(ordinaryProgram); use_program(ordinaryProgram);
    GL(glUniform1i)(GL(glGetUniformLocation)(ordinaryProgram,"frame"),0);
    {
        DrawScope scope;
        GL(glDrawArrays)(GL_TRIANGLES,0,3);
    }
    GL(glReadPixels)(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
    if (pixel[0] > 3 || pixel[1] < 250 || pixel[2] > 3 || GL(glGetError)() != GL_NO_ERROR) return 5;
    std::puts("PASS ordinary texture still draws green after video sampling");
    kudroid_surface_texture_release(1);
    GL(glDeleteTextures)(2, images); GL(glDeleteProgram)(videoProgram); GL(glDeleteProgram)(ordinaryProgram);
    EGL(eglMakeCurrent)(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    EGL(eglDestroyContext)(display, context); EGL(eglDestroySurface)(display, surface); EGL(eglTerminate)(display);
    return 0;
}
