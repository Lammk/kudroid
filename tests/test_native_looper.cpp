#include "kudroid/BionicShim.h"
#include "kudroid/platform/FramePacer.h"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <future>
#include <thread>
#include <unistd.h>

extern "C" {
void* bionic_ALooper_prepare(int);
int bionic_ALooper_pollOnce(int, int*, int*, void**);
int bionic_ALooper_pollAll(int, int*, int*, void**);
int bionic_ALooper_addFd(void*, int, int, int, void*, void*);
int bionic_ALooper_removeFd(void*, int);
void bionic_ALooper_acquire(void*);
void bionic_ALooper_release(void*);
void bionic_ALooper_wake(void*);
}

static int failures;
static void check(bool value, const char* message) {
    std::printf("%s %s\n", value ? "PASS" : "FAIL", message);
    failures += !value;
}

static char logged[2048];
static int loggedPriority;
static void captureSyslog(int priority, const char* format, va_list args) {
    loggedPriority = priority;
    std::vsnprintf(logged, sizeof(logged), format, args);
    errno = ENOSPC;
}
extern "C" void syslog(int priority, const char* format, ...) {
    va_list args;
    va_start(args, format);
    captureSyslog(priority, format, args);
    va_end(args);
}

#if defined(__GLIBC__)
// Fortified libc headers redirect syslog calls to this entry point.
extern "C" void __syslog_chk(int priority, int, const char* format, ...) {
    va_list args;
    va_start(args, format);
    captureSyslog(priority, format, args);
    va_end(args);
}
#endif

struct FrameContext {
    std::atomic<int> calls{0};
    std::thread::id owner = std::this_thread::get_id();
    std::atomic<bool> correctThread{true};
};

static void frameCallback(int64_t, void* raw) {
    auto* context = static_cast<FrameContext*>(raw);
    if (context->owner != std::this_thread::get_id()) context->correctThread = false;
    ++context->calls;
}

static void testFrameOwnership() {
    auto* choreographer = bionic_AChoreographer_getInstance();
    FrameContext context;
    bionic_AChoreographer_postFrameCallback(choreographer,
        reinterpret_cast<void*>(&frameCallback), &context);
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    check(context.calls == 0, "native callback waits for its owner to poll");
    for (int i = 0; i < 10 && context.calls == 0; ++i) {
        bionic_ALooper_pollOnce(20, nullptr, nullptr, nullptr);
    }
    check(context.calls == 1 && context.correctThread,
          "slow owner receives its callback on its own thread");
}

static void testRetiredCallbacks() {
    FrameContext retired;
    std::atomic<bool> instancesValid{true};
    for (int i = 0; i < 100; ++i) {
        std::thread worker([&] {
            void* instance = bionic_AChoreographer_getInstance();
            if (instance == nullptr) instancesValid = false;
            bionic_AChoreographer_postFrameCallbackDelayed(instance,
                reinterpret_cast<void*>(&frameCallback), &retired, 80);
        });
        worker.join();
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(150);
    while (std::chrono::steady_clock::now() < deadline) {
        bionic_ALooper_pollOnce(10, nullptr, nullptr, nullptr);
    }
    check(instancesValid, "native instances remain available after repeated thread replacement");
    check(retired.calls == 0, "pending callbacks of exited owners never run on a replacement");
}

struct PipeContext {
    int calls = 0;
    bool correctThread = true;
    std::thread::id owner;
};

static int pipeCallback(int fd, int, void* raw) {
    char byte;
    ::read(fd, &byte, 1);
    auto* context = static_cast<PipeContext*>(raw);
    ++context->calls;
    context->correctThread &= context->owner == std::this_thread::get_id();
    return 0;
}

static void testLooperIsolation() {
    void* mainLooper = bionic_ALooper_prepare(1);
    while (bionic_ALooper_pollOnce(0, nullptr, nullptr, nullptr) != -3) {}
    int pipeFds[2];
    if (::pipe(pipeFds) != 0) { check(false, "create input pipe"); return; }
    std::promise<void*> ready;
    auto looperReady = ready.get_future();
    std::promise<void> proceed;
    auto mayPoll = proceed.get_future();
    PipeContext context;
    int result = 0;
    std::thread worker([&] {
        context.owner = std::this_thread::get_id();
        void* looper = bionic_ALooper_prepare(0);
        bionic_ALooper_addFd(looper, pipeFds[0], -1, 1,
            reinterpret_cast<void*>(&pipeCallback), &context);
        bionic_ALooper_acquire(looper);
        ready.set_value(looper);
        mayPoll.wait();
        result = bionic_ALooper_pollOnce(100, nullptr, nullptr, nullptr);
    });
    void* workerLooper = looperReady.get();
    check(workerLooper != mainLooper, "different threads own different loopers");
    const char byte = 1;
    ::write(pipeFds[1], &byte, 1);
    check(bionic_ALooper_pollOnce(0, nullptr, nullptr, nullptr) == -3,
          "another thread cannot consume the owner's fd callback");
    proceed.set_value();
    worker.join();
    check(result == -2 && context.calls == 1 && context.correctThread,
          "callback result and delivery thread match the native looper contract");
    bionic_ALooper_wake(workerLooper);
    bionic_ALooper_release(workerLooper);
    ::close(pipeFds[0]);
    ::close(pipeFds[1]);
}

static void testWakeBeforePoll() {
    void* looper = bionic_ALooper_prepare(0);
    bionic_ALooper_wake(looper);
    check(bionic_ALooper_pollOnce(0, nullptr, nullptr, nullptr) == -1,
          "wake before the first wait is retained");
    check(bionic_ALooper_pollAll(0, nullptr, nullptr, nullptr) == -3,
          "an idle poll reports the Android timeout constant");
}

static void testCallbackReplacementAndWake() {
    int pipeFds[2];
    if (::pipe(pipeFds) != 0) { check(false, "create replacement pipe"); return; }
    struct Context {
        void* looper;
        int first = 0, second = 0;
        static int replacement(int fd, int, void* raw) {
            char byte;
            ::read(fd, &byte, 1);
            ++static_cast<Context*>(raw)->second;
            return 0;
        }
        static int original(int fd, int, void* raw) {
            char byte;
            ::read(fd, &byte, 1);
            auto* context = static_cast<Context*>(raw);
            ++context->first;
            bionic_ALooper_addFd(context->looper, fd, -1, 1,
                reinterpret_cast<void*>(&replacement), raw);
            return 0;
        }
    } context{bionic_ALooper_prepare(0)};
    bionic_ALooper_addFd(context.looper, pipeFds[0], -1, 1,
        reinterpret_cast<void*>(&Context::original), &context);
    const char byte = 1;
    ::write(pipeFds[1], &byte, 1);
    check(bionic_ALooper_pollOnce(0, nullptr, nullptr, nullptr) == -2 && context.first == 1,
          "pollOnce reports a callback even when a wake is also pending");
    ::write(pipeFds[1], &byte, 1);
    check(bionic_ALooper_pollAll(100, nullptr, nullptr, nullptr) == -1 && context.second == 1,
          "pollAll retains a stop wake while dispatching a replacement callback");
    check(bionic_ALooper_removeFd(context.looper, pipeFds[0]) == 0,
          "returning zero removes only that callback's registration");
    ::close(pipeFds[0]);
    ::close(pipeFds[1]);
}

static void callVsyslog(int priority, const char* format, ...) {
    using Function = void (*)(int, const char*, va_list);
    auto call = reinterpret_cast<Function>(kudroid::resolve_bionic_symbol("vsyslog"));
    va_list args;
    va_start(args, format);
    call(priority, format, args);
    va_end(args);
}

static void testSyslog() {
    using Function = void (*)(int, const char*, ...);
    auto call = reinterpret_cast<Function>(kudroid::resolve_bionic_symbol("syslog"));
    errno = EBUSY;
    call(3, "%s %d", "frame", 42);
    check(std::strcmp(logged, "frame 42") == 0 && loggedPriority == 3 && errno == EBUSY,
          "syslog preserves the formatted message, priority and errno");
    errno = ENOENT;
    callVsyslog(4, "%s %d", "vsyslog", 7);
    check(std::strcmp(logged, "vsyslog 7") == 0 && loggedPriority == 4 && errno == ENOENT,
          "vsyslog preserves va_list formatting and errno");
}

int main() {
    testLooperIsolation();
    testWakeBeforePoll();
    testCallbackReplacementAndWake();
    testFrameOwnership();
    testRetiredCallbacks();
    testSyslog();
    kudroid::frame_pacer_reset_for_test();
    return failures ? 1 : 0;
}
