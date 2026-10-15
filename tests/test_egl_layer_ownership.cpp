#include "kudroid/platform/GraphicsShim.h"
#include "kudroid/kudroid_bridge.h"

#include <cstdio>
#include <cstdint>

extern "C" void* bionic_eglCreateWindowSurface(void*, void*, void*, const int*);
extern "C" unsigned bionic_eglDestroySurface(void*, void*);
extern "C" void* bionic_ANativeWindow_fromSurface(void*, void*);

static int failures;
static uintptr_t nextSurface = 1;
static bool destroyAllowed = true, current = false, intervalAfterCurrent = false;
static int intervalCalls;

static void check(bool ok, const char* message) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", message);
    if (!ok) ++failures;
}

extern "C" void* eglGetDisplay(void*) { return reinterpret_cast<void*>(1); }
extern "C" unsigned eglInitialize(void*, int* major, int* minor) {
    if (major) *major = 1;
    if (minor) *minor = 5;
    return 1;
}
extern "C" unsigned eglBindAPI(unsigned) { return 1; }
extern "C" unsigned eglChooseConfig(void*, const int*, void** configs, int, int* count) {
    *configs = reinterpret_cast<void*>(1); *count = 1; return 1;
}
extern "C" void* eglCreateContext(void*, void*, void*, const int*) {
    return reinterpret_cast<void*>(1);
}
extern "C" void* eglCreateWindowSurface(void*, void*, void*, const int*) {
    return reinterpret_cast<void*>(nextSurface++);
}
extern "C" unsigned eglDestroySurface(void*, void*) { return destroyAllowed; }
extern "C" unsigned eglDestroyContext(void*, void*) { return 1; }
extern "C" unsigned eglMakeCurrent(void*, void*, void*, void* context) {
    current = context != nullptr; return 1;
}
extern "C" unsigned eglSwapInterval(void*, int) {
    ++intervalCalls; intervalAfterCurrent = current; return current;
}

int main() {
    int layer, other;
    kudroid_set_metal_layer(&layer, 640, 480, 1);
    check(kudroid_gpu_canvas_allowed(&layer), "canvas owns layer before EGL surface creation");
    void* window = bionic_ANativeWindow_fromSurface(nullptr, nullptr);
    void* surface1 = bionic_eglCreateWindowSurface(reinterpret_cast<void*>(1), nullptr, window, nullptr);
    void* surface2 = bionic_eglCreateWindowSurface(reinterpret_cast<void*>(1), nullptr, window, nullptr);
    check(!kudroid_gpu_canvas_allowed(&layer), "EGL prevents software image from covering drawables");
    check(kudroid_gpu_canvas_allowed(&other), "ownership applies only to the surface's layer");
    destroyAllowed = false;
    bionic_eglDestroySurface(reinterpret_cast<void*>(1), surface1);
    check(!kudroid_gpu_canvas_allowed(&layer), "failed destruction retains hardware ownership");
    destroyAllowed = true;
    bionic_eglDestroySurface(reinterpret_cast<void*>(1), surface1);
    check(!kudroid_gpu_canvas_allowed(&layer), "remaining EGL surface retains ownership");
    bionic_eglDestroySurface(reinterpret_cast<void*>(1), surface2);
    check(kudroid_gpu_canvas_allowed(&layer), "canvas regains layer after last EGL surface is destroyed");
    const auto state = bionic_kudroid_gl_surface_create(2, 16, 8);
    check(state != 0 && intervalCalls == 0, "creation does not set swap interval without current context");
    check(bionic_kudroid_gl_surface_make_current(state) && intervalAfterCurrent,
          "swap interval is set after context becomes current");
    bionic_kudroid_gl_surface_destroy(state);
    return failures ? 1 : 0;
}
