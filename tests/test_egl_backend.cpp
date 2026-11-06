#include "kudroid/platform/EglCapabilities.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglext_angle.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

namespace {
std::atomic<int> ordinaryCalls{0}, platformCalls{0}, invalidCalls{0};
bool available = true;
EGLDisplay reply = reinterpret_cast<EGLDisplay>(1);
thread_local EGLint error = EGL_BAD_ALLOC;
int checks = 0, failures = 0;

void check(bool value, const char* message) {
    ++checks;
    failures += !value;
    std::printf("%s %s\n", value ? "PASS" : "FAIL", message);
}

EGLDisplay ordinary(EGLNativeDisplayType native) {
    ++ordinaryCalls;
    if (native != EGL_DEFAULT_DISPLAY) ++invalidCalls;
    return reply;
}

EGLDisplay platform(EGLenum api, void* native, const EGLint* attributes) {
    ++platformCalls;
    if (api != EGL_PLATFORM_ANGLE_ANGLE || native != nullptr ||
        !attributes || attributes[0] != EGL_PLATFORM_ANGLE_TYPE_ANGLE ||
        attributes[1] != EGL_PLATFORM_ANGLE_TYPE_VULKAN_ANGLE || attributes[2] != EGL_NONE)
        ++invalidCalls;
    return reply;
}
}

namespace kudroid {
void* get_egl_func(const char* name) {
    if (!available) return nullptr;
    if (std::strcmp(name, "eglGetDisplay") == 0) return reinterpret_cast<void*>(&ordinary);
    if (std::strcmp(name, "eglGetPlatformDisplayEXT") == 0) return reinterpret_cast<void*>(&platform);
    if (std::strcmp(name, "eglGetError") == 0) ++invalidCalls;
    return nullptr;
}
}

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const bool vulkan = std::strcmp(argv[1], "vulkan") == 0;
    check(std::strcmp(kudroid::egl_backend_name(), argv[1]) == 0, "configured backend is reported");
    check(kudroid::egl_default_display() == reply, "backend display is returned unchanged");
    check((vulkan ? platformCalls : ordinaryCalls) == 1 &&
          (vulkan ? ordinaryCalls : platformCalls) == 0, "exactly one selected display route is called");
    check(invalidCalls == 0 && error == EGL_BAD_ALLOC, "native arguments and pending errors are preserved");
    reply = EGL_NO_DISPLAY;
    check(kudroid::egl_default_display() == EGL_NO_DISPLAY, "failed display result is preserved");
    check((vulkan ? ordinaryCalls : platformCalls) == 0, "failed backend does not select another renderer");
    const int before = ordinaryCalls + platformCalls;
    available = false;
    check(kudroid::egl_default_display() == EGL_NO_DISPLAY &&
          ordinaryCalls + platformCalls == before, "missing selected entry point returns no display");
    available = true;
    reply = reinterpret_cast<EGLDisplay>(1);
    std::atomic<int> wrongReplies{0};
    std::vector<std::thread> workers;
    for (int i = 0; i < 8; ++i) workers.emplace_back([&] {
        for (int j = 0; j < 32; ++j)
            if (kudroid::egl_default_display() != reply || error != EGL_BAD_ALLOC) ++wrongReplies;
    });
    for (auto& worker : workers) worker.join();
    check(wrongReplies == 0 && invalidCalls == 0 && ordinaryCalls + platformCalls == before + 256,
          "concurrent callers retain the selected display and their errors");
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
