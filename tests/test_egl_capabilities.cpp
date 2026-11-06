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
extern "C" const char* bionic_glGetString(unsigned int);
extern "C" const char* bionic_glGetStringi(unsigned int, unsigned int);
extern "C" void bionic_glGetIntegerv(unsigned int, int*);
extern "C" void* bionic_eglGetProcAddress(const char*);
extern "C" void bionic_glBufferData(unsigned, long, const void*, unsigned);
extern "C" void bionic_glBufferSubData(unsigned, long, long, const void*);
extern "C" void* bionic_glMapBufferRange(unsigned, long, long, unsigned);
extern "C" void bionic_glPushDebugGroup(unsigned, unsigned, int, const char*);
extern "C" void bionic_glObjectLabel(unsigned, unsigned, int, const char*);
extern "C" unsigned bionic_glGetError();

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

static const char* glList[] = {"GL_OES_a", "GL_EXT_disjoint_timer_query", "GL_OES_b"};
static const char* glJoined = "GL_OES_a GL_EXT_disjoint_timer_query GL_OES_b";
static unsigned lastIndex;
extern "C" const char* glGetString(unsigned int name) {
    return name == 0x1F03 ? glJoined : "renderer";
}
extern "C" const char* glGetStringi(unsigned int, unsigned int index) {
    lastIndex = index;
    return index < 3 ? glList[index] : nullptr;
}
extern "C" void glGetIntegerv(unsigned int pname, int* value) {
    *value = pname == 0x821D ? 3 : 7;
}

static void gl_checks() {
    check(std::strcmp(bionic_glGetString(0x1F03), "GL_OES_a GL_OES_b") == 0,
          "never-completing timer query extension is hidden from GL_EXTENSIONS");
    check(std::strcmp(bionic_glGetString(0x1F01), "renderer") == 0, "other GL strings unchanged");
    int count = 0;
    bionic_glGetIntegerv(0x821D, &count);
    check(count == 2, "GL_NUM_EXTENSIONS excludes hidden extensions");
    int other = 0;
    bionic_glGetIntegerv(0x0D33, &other);
    check(other == 7, "unrelated integer queries unchanged");
    check(std::strcmp(bionic_glGetStringi(0x1F03, 0), "GL_OES_a") == 0 &&
          std::strcmp(bionic_glGetStringi(0x1F03, 1), "GL_OES_b") == 0,
          "indexed extensions skip hidden entries");
    bionic_glGetStringi(0x1F03, 2);
    check(lastIndex >= 3, "out-of-range index still reaches the backend as out of range");
    glJoined = "GL_OES_a GL_OES_b";
    check(bionic_glGetString(0x1F03) == glJoined, "lists without hidden tokens pass through unchanged");
}

static unsigned bufferTarget, bufferFlags, labelKind, labelId, driverError;
static long bufferOffset, bufferSize;
static const void* bufferInput;
static const char* labelInput;
static int labelLength, bufferCalls, labelCalls, errorQueries;
static bool provideLabels;
static void* mappedResult;

extern "C" void glBufferData(unsigned target, long size, const void* data, unsigned usage) {
    ++bufferCalls;
    bufferTarget = target; bufferSize = size; bufferInput = data; bufferFlags = usage;
}
extern "C" void glBufferSubData(unsigned target, long offset, long size, const void* data) {
    ++bufferCalls;
    bufferTarget = target; bufferOffset = offset; bufferSize = size; bufferInput = data;
}
extern "C" void* glMapBufferRange(unsigned target, long offset, long length, unsigned access) {
    ++bufferCalls;
    bufferTarget = target; bufferOffset = offset; bufferSize = length; bufferFlags = access;
    return mappedResult;
}
extern "C" void glPushDebugGroup(unsigned source, unsigned id, int length, const char* message) {
    ++labelCalls;
    labelKind = source; labelId = id; labelLength = length; labelInput = message;
}
extern "C" void glObjectLabel(unsigned identifier, unsigned name, int length, const char* label) {
    glPushDebugGroup(identifier, name, length, label);
}
extern "C" __eglMustCastToProperFunctionPointerType eglGetProcAddress(const char* name) {
    if (std::strcmp(name, "glPushDebugGroup") == 0)
        return reinterpret_cast<__eglMustCastToProperFunctionPointerType>(&glPushDebugGroup);
    if (provideLabels && std::strcmp(name, "glObjectLabel") == 0)
        return reinterpret_cast<__eglMustCastToProperFunctionPointerType>(&glObjectLabel);
    return nullptr;
}
extern "C" unsigned glGetError() {
    ++errorQueries;
    const unsigned error = driverError;
    driverError = 0;
    return error;
}

static void submission_checks() {
    char data[] = {'a', 'b', 'c'};
    driverError = 0x0502;
    bionic_glBufferData(0x8892, 0x100000005L, data, 0x88E8);
    check(bufferCalls == 1 && bufferTarget == 0x8892 && bufferSize == 0x100000005L &&
          bufferInput == data && bufferFlags == 0x88E8, "buffer allocation retains 64-bit size and original payload");
    bionic_glBufferData(0x8892, -1, nullptr, 0x88E4);
    check(bufferCalls == 2 && bufferSize == -1 && bufferInput == nullptr,
          "invalid allocation arguments still reach the backend once");
    bionic_glBufferSubData(0x8893, 0x100000004L, 3, data);
    check(bufferCalls == 3 && bufferOffset == 0x100000004L && bufferSize == 3 && bufferInput == data,
          "buffer update retains 64-bit offset and original payload");
    mappedResult = data;
    check(bionic_glMapBufferRange(0x8892, 7, 3, 2) == data && bufferCalls == 4 &&
          bufferOffset == 7 && bufferFlags == 2, "writable mapping returns the exact backend pointer");
    mappedResult = nullptr;
    check(bionic_glMapBufferRange(0x8892, -1, -2, 0) == nullptr && bufferCalls == 5 && bufferSize == -2,
          "failed mapping keeps null result and invalid arguments");
    check(bionic_eglGetProcAddress("glPushDebugGroup") == reinterpret_cast<void*>(&bionic_glPushDebugGroup),
          "available debug group procedure routes through tracing");
    check(bionic_eglGetProcAddress("glObjectLabel") == nullptr,
          "tracing does not expose an unavailable optional procedure");
    provideLabels = true;
    check(bionic_eglGetProcAddress("glObjectLabel") == reinterpret_cast<void*>(&bionic_glObjectLabel),
          "available object label procedure routes through tracing");
    bionic_glPushDebugGroup(0x824A, 17, 3, data);
    check(labelCalls == 1 && labelKind == 0x824A && labelId == 17 && labelLength == 3 && labelInput == data,
          "explicit label length does not require a terminator or copy the driver input");
    bionic_glObjectLabel(0x82E2, 29, -1, "program");
    check(labelCalls == 2 && labelKind == 0x82E2 && labelId == 29 && labelLength == -1 &&
          std::strcmp(labelInput, "program") == 0, "terminated object label preserves backend arguments");
    bionic_glObjectLabel(0x82E0, 30, 0, nullptr);
    check(labelCalls == 3 && labelLength == 0 && labelInput == nullptr, "null label removal reaches the backend");
    const char* invalid = reinterpret_cast<const char*>(uintptr_t(1));
    bionic_glPushDebugGroup(0x824A, 18, -2, invalid);
    check(labelCalls == 4 && labelLength == -2 && labelInput == invalid,
          "invalid negative label length is forwarded without inspecting its pointer");
    check(errorQueries == 0 && driverError == 0x0502, "submission diagnostics never query or consume pending GL errors");
    check(bionic_glGetError() == 0x0502 && bionic_glGetError() == 0 && errorQueries == 2,
          "guest still receives the original GL error exactly once");
}

int main() {
    gl_checks();
    submission_checks();
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
