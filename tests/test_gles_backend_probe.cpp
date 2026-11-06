#include "kudroid/platform/GlesBackendProbe.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <dlfcn.h>
#include <cstdio>
#include <cstring>

namespace {
void* eglLibrary = nullptr;
void* glLibrary = nullptr;
PFNEGLGETPROCADDRESSPROC getProcedure = nullptr;
}

namespace kudroid {
void* get_egl_func(const char* name) {
    void* entry = dlsym(eglLibrary, name);
    return entry ? entry : reinterpret_cast<void*>(getProcedure(name));
}
void* get_gl_func(const char* name) {
    void* entry = dlsym(glLibrary, name);
    return entry ? entry : reinterpret_cast<void*>(getProcedure(name));
}
void* get_vk_func(const char*) { return nullptr; }
}

int main() {
    eglLibrary = dlopen("libEGL.so.1", RTLD_NOW | RTLD_LOCAL);
    glLibrary = dlopen("libGLESv2.so.2", RTLD_NOW | RTLD_LOCAL);
    if (!eglLibrary || !glLibrary) return 2;
    getProcedure = reinterpret_cast<PFNEGLGETPROCADDRESSPROC>(dlsym(eglLibrary, "eglGetProcAddress"));
    if (!getProcedure) return 2;
#define EGL_ENTRY(name) const auto name = reinterpret_cast<decltype(&::name)>(kudroid::get_egl_func(#name)); if (!name) return 2
    EGL_ENTRY(eglInitialize); EGL_ENTRY(eglChooseConfig); EGL_ENTRY(eglBindAPI);
    EGL_ENTRY(eglCreatePbufferSurface); EGL_ENTRY(eglCreateContext); EGL_ENTRY(eglMakeCurrent);
    EGL_ENTRY(eglDestroyContext); EGL_ENTRY(eglDestroySurface); EGL_ENTRY(eglTerminate);
#undef EGL_ENTRY
    const auto platformDisplay = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
        getProcedure("eglGetPlatformDisplayEXT"));
    if (!platformDisplay) return 2;
    const EGLDisplay display = platformDisplay(EGL_PLATFORM_SURFACELESS_MESA, nullptr, nullptr);
    if (!display || !eglInitialize(display, nullptr, nullptr) || !eglBindAPI(EGL_OPENGL_ES_API)) return 2;
    const EGLint configAttributes[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
    EGLConfig config = nullptr;
    EGLint count = 0;
    if (!eglChooseConfig(display, configAttributes, &config, 1, &count) || count != 1) return 2;
    const EGLint surfaceAttributes[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
    const EGLSurface surface = eglCreatePbufferSurface(display, config, surfaceAttributes);
    const EGLint contextAttributes[] = {EGL_CONTEXT_MAJOR_VERSION_KHR, 3,
        EGL_CONTEXT_MINOR_VERSION_KHR, 1, EGL_NONE};
    const EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttributes);
    if (!surface || !context || !eglMakeCurrent(display, surface, surface, context)) return 2;
    const std::string result = kudroid::probe_gles31();
    std::puts(result.c_str());
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(display, context);
    eglDestroySurface(display, surface);
    eglTerminate(display);
    dlclose(glLibrary);
    dlclose(eglLibrary);
    return result.rfind("PASS ", 0) == 0 ? 0 : 1;
}
