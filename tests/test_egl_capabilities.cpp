#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <cstdio>
#include <cstdint>
#include <atomic>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

extern "C" const char* bionic_eglQueryString(EGLDisplay, EGLint);
extern "C" EGLBoolean bionic_eglChooseConfig(EGLDisplay, const EGLint*, EGLConfig*, EGLint, EGLint*);
extern "C" EGLint bionic_eglGetError();

static int checks, failures;
static std::atomic<int> probes{0};
static const char* extensions = "EGL_KHR_image EGL_ANDROID_recordable EGL_KHR_fence_sync";
static thread_local EGLint pendingError = EGL_SUCCESS;
static const EGLint* forwarded;
static EGLDisplay display(int n) { return reinterpret_cast<EGLDisplay>(uintptr_t(n)); }
static void check(bool ok, const char* message) {
    ++checks;
    failures += !ok;
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", message);
}

extern "C" const char* eglQueryString(EGLDisplay dpy, EGLint name) {
    if (dpy == display(9)) { pendingError = EGL_BAD_DISPLAY; return nullptr; }
    return name == EGL_EXTENSIONS ? extensions : "vendor";
}
extern "C" EGLBoolean eglGetConfigs(EGLDisplay dpy, EGLConfig* configs, EGLint size, EGLint* count) {
    ++probes;
    *count = configs ? (size < 2 ? size : 2) : 2;
    for (int i = 0; configs && i < *count; ++i)
        configs[i] = reinterpret_cast<EGLConfig>(uintptr_t(dpy) * 10 + i);
    return EGL_TRUE;
}
extern "C" EGLBoolean eglGetConfigAttrib(EGLDisplay, EGLConfig config, EGLint key, EGLint* value) {
    if (key != EGL_RECORDABLE_ANDROID) { pendingError = EGL_BAD_ATTRIBUTE; return EGL_FALSE; }
    *value = uintptr_t(config) == 21 ? EGL_TRUE : EGL_FALSE;
    return EGL_TRUE;
}
extern "C" EGLBoolean eglChooseConfig(EGLDisplay dpy, const EGLint* attrs,
                                       EGLConfig* configs, EGLint size, EGLint* count) {
    forwarded = attrs;
    bool recordable = false;
    for (int i = 0; attrs && attrs[i] != EGL_NONE; i += 2)
        if (attrs[i] == EGL_RECORDABLE_ANDROID) recordable = attrs[i + 1] == EGL_TRUE;
    *count = recordable && dpy != display(2) ? 0 : 1;
    if (configs && size && *count) configs[0] = reinterpret_cast<EGLConfig>(uintptr_t(dpy) * 10);
    return EGL_TRUE;
}
extern "C" EGLint eglGetError() {
    const EGLint result = pendingError;
    pendingError = EGL_SUCCESS;
    return result;
}

int main() {
    const EGLint requested[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR,
        EGL_DEPTH_SIZE, 16, EGL_STENCIL_SIZE, 8, EGL_RECORDABLE_ANDROID, EGL_TRUE, EGL_NONE};
    int count = -1;
    bionic_eglChooseConfig(display(1), requested, nullptr, 0, &count);
    check(count == 0, "recordable request has no matching backend config");
    const char* filtered = bionic_eglQueryString(display(1), EGL_EXTENSIONS);
    check(std::strcmp(filtered, "EGL_KHR_image EGL_KHR_fence_sync") == 0,
          "display without recordable configs does not advertise recording");
    const EGLint ordinary[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR,
        EGL_DEPTH_SIZE, 16, EGL_STENCIL_SIZE, 8, EGL_NONE};
    bionic_eglChooseConfig(display(1), ordinary, nullptr, 0, &count);
    check(count == 1 && forwarded == ordinary, "ordinary context config request passes unchanged");
    bionic_eglChooseConfig(display(1), requested, nullptr, 0, &count);
    check(count == 0 && forwarded == requested, "explicit recording requests retain backend semantics");
    check(bionic_eglQueryString(display(2), EGL_EXTENSIONS) == extensions,
          "recordable backend retains its complete extension string");
    check(bionic_eglQueryString(display(1), EGL_EXTENSIONS) == filtered,
          "filtered response has a stable address");
    const int before = probes;
    check(bionic_eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS) == extensions && probes == before,
          "client extension query never probes display configs");
    check(std::strcmp(bionic_eglQueryString(display(1), EGL_VENDOR), "vendor") == 0,
          "non-extension queries remain unchanged");
    pendingError = EGL_BAD_ALLOC;
    bionic_eglQueryString(display(1), EGL_EXTENSIONS);
    check(bionic_eglGetError() == EGL_BAD_ALLOC, "valid capability queries preserve the pending EGL error");
    check(bionic_eglGetError() == EGL_SUCCESS, "EGL errors remain consumed once");
    check(bionic_eglQueryString(display(9), EGL_EXTENSIONS) == nullptr &&
          bionic_eglGetError() == EGL_BAD_DISPLAY, "backend query errors are preserved");
    extensions = "EGL_ANDROID_recordable_extra X_EGL_ANDROID_recordable";
    check(bionic_eglQueryString(display(1), EGL_EXTENSIONS) == extensions,
          "extension filtering matches complete tokens only");
    extensions = "EGL_ANDROID_recordable";
    check(std::strcmp(bionic_eglQueryString(display(1), EGL_EXTENSIONS), "") == 0,
          "single unsupported extension produces an empty list");
    check(std::strcmp(filtered, "EGL_KHR_image EGL_KHR_fence_sync") == 0,
          "earlier returned strings survive later queries");
    extensions = "EGL_KHR_image EGL_ANDROID_recordable EGL_KHR_fence_sync";
    std::vector<const char*> replies(8);
    std::vector<std::thread> workers;
    for (size_t i = 0; i < replies.size(); ++i)
        workers.emplace_back([&, i] { replies[i] = bionic_eglQueryString(display(1), EGL_EXTENSIONS); });
    for (auto& worker : workers) worker.join();
    bool stable = true;
    for (const char* reply : replies) stable &= reply == filtered;
    check(stable, "parallel queries share immutable stable strings");
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
