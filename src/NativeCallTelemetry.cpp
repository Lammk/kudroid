#include "kudroid/NativeCallTelemetry.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <functional>
#include <pthread.h>
#if !defined(__APPLE__)
#include <sys/syscall.h>
#include <unistd.h>
#endif

#include "kudroid/Log.h"
#include "kudroid/abi/BlockingWaitRegistry.h"
#include "kudroid/debug/ThreadSampler.h"
#include "kudroid/platform/MemoryInfo.h"

extern "C" void kudroid_persistent_breadcrumb(const char* line);

namespace kudroid {
namespace {

struct ActiveCall {
    std::mutex mutex;
    char class_name[256] = {};
    char method[256] = {};
    char signature[512] = {};
    char stage[64] = "enter";
    int vm_lock_depth = 0;
    std::atomic<unsigned> active_count{0};
    std::atomic<bool> started{false};
    std::atomic<uint64_t> next_call_id{1};
    uint64_t native_call_id = 0;
    uint64_t native_thread_id = 0;
    // When the outermost native call currently in flight began.
    //
    // Without this there was no native duration anywhere: the watchdog printed
    // native_active=1 every 250ms for as long as any native call ran and left the
    // reader to subtract timestamps by hand. Worse, it could not tell a call that had
    // been running for 40 seconds from one that started a tick ago, so it had no basis
    // for escalating — which is why 36 seconds of a wedged nativeRender produced 40
    // identical lines and no diagnosis.
    std::atomic<uint64_t> native_start_ns{0};
    std::atomic<unsigned> java_active_count{0};
    uint64_t java_thread_id = 0;
    size_t java_depth = 0;
    uint64_t java_start_ns = 0;
    char java_class[256] = {};
    char java_method[256] = {};
    char java_signature[512] = {};
    std::atomic<uint64_t> java_last_progress_ns{0};

    // Set from a signal handler when a thread takes a fatal fault. Only scalars, and
    // only stores, because that is all a signal handler may do.
    //
    // Without this the watchdog cannot tell a hang from a crash, and it defaults to
    // calling everything a hang: UnityMain faulted, parked in the crash handler, and the
    // watchdog kept reporting `native_elapsed_ms=16570 nativeRender` — a native call it
    // described as running when the thread executing it was dead.
    std::atomic<int> fatal_signal{0};
    std::atomic<uint64_t> fatal_thread_id{0};
};

ActiveCall g_call;
thread_local uint64_t t_call_id = 0;

// The operating system's thread id, the same number pthread_threadid_np gives and
// the same one the blocking-wait registry and the thread sampler print.
//
// This used to be std::hash<std::thread::id>, which produced values like
// 15304429423467498172 — stable within a run but shared with nothing else. So the
// watchdog said native_thread_id=15304429423467498172 while the stall report said
// tid=2844404, and there was no way to tell whether they were the same thread. They
// were not, and that fact was the whole answer; it just was not readable.
uint64_t thread_id() {
#if defined(__APPLE__)
    uint64_t tid = 0;
    ::pthread_threadid_np(nullptr, &tid);
    return tid;
#else
    return static_cast<uint64_t>(::syscall(SYS_gettid));
#endif
}

uint64_t now_ns() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}

void watchdog_main() {
    // Sampling thresholds. A sample costs one Mach call per thread and writes a line
    // each, so it is not something to do every tick — but it is the only diagnostic
    // that can see a thread stuck outside our code, so it must happen without anyone
    // enabling it.
    //
    // 5s: long enough that no legitimate frame, asset load or shader compile reaches
    // it, short enough to land well inside the window before a user force-quits.
    // Then every 10s, because the second and third samples are what turn a snapshot
    // into an answer: a pc that moved with cpu_ms climbing is a spin, a pc that did
    // not move with cpu_ms flat is a deadlock.
    constexpr uint64_t kSampleAfterMs = 5000;
    constexpr uint64_t kResampleEveryMs = 10000;
    uint64_t next_sample_ms = kSampleAfterMs;
    uint64_t sampled_start = 0;

    while (g_call.started.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));

        // A thread has crashed: say so once and stop reporting.
        //
        // Everything below measures how long calls have been running, and after a fatal
        // fault those measurements are lies — the thread is parked in the crash handler,
        // not working. Continuing to print them is worse than printing nothing, because
        // a reader chases the wrong thing: this watchdog announced
        // `native_elapsed_ms=16570 stage=before-trampoline nativeRender` for sixteen
        // seconds after UnityMain had already taken a SIGSEGV.
        if (const int fatal = g_call.fatal_signal.load(std::memory_order_acquire)) {
            char line[256];
            std::snprintf(line, sizeof(line),
                          "watchdog stopped reason=fatal-signal signal=%d thread_id=%llu",
                          fatal,
                          static_cast<unsigned long long>(
                              g_call.fatal_thread_id.load(std::memory_order_relaxed)));
            kudroid_persistent_breadcrumb(line);
            return;
        }

        // Name any thread parked on a blocking wait for too long.
        //
        // This runs before the progress check below and independently of it: a stuck
        // thread is worth reporting whether or not a native or Java call happens to be
        // in flight on some other thread. Three seconds is long enough that no real
        // handshake, asset load or lock convoy trips it, and short enough to appear
        // well before a user gives up and force-quits.
        //
        // This is the line that was missing when ULTRAKILL stopped inside
        // nativeRender: the log simply ended, because a wait is the one thing the
        // shims did not record.
        blocking_wait_report_stalled(/*threshold_ms=*/3000);

        const bool native_active = g_call.active_count.load(std::memory_order_acquire) != 0;
        const bool java_active = g_call.java_active_count.load(std::memory_order_acquire) != 0;

        char cls[sizeof(g_call.class_name)];
        char method[sizeof(g_call.method)];
        char sig[sizeof(g_call.signature)];
        char stage[sizeof(g_call.stage)];
        int depth;
        uint64_t native_id;
        uint64_t native_tid;
        uint64_t java_tid;
        uint64_t java_start;
        size_t java_depth;
        char java_cls[sizeof(g_call.java_class)];
        char java_method[sizeof(g_call.java_method)];
        char java_sig[sizeof(g_call.java_signature)];
        {
            std::lock_guard<std::mutex> lock(g_call.mutex);
            std::memcpy(cls, g_call.class_name, sizeof(cls));
            std::memcpy(method, g_call.method, sizeof(method));
            std::memcpy(sig, g_call.signature, sizeof(sig));
            std::memcpy(stage, g_call.stage, sizeof(stage));
            depth = g_call.vm_lock_depth;
            native_id = g_call.native_call_id;
            native_tid = g_call.native_thread_id;
            java_tid = g_call.java_thread_id;
            java_start = g_call.java_start_ns;
            java_depth = g_call.java_depth;
            std::memcpy(java_cls, g_call.java_class, sizeof(java_cls));
            std::memcpy(java_method, g_call.java_method, sizeof(java_method));
            std::memcpy(java_sig, g_call.java_signature, sizeof(java_sig));
        }
        const uint64_t now = now_ns();
        const uint64_t native_start = g_call.native_start_ns.load(std::memory_order_acquire);
        const uint64_t native_elapsed_ms =
            (native_active && native_start != 0 && now > native_start)
                ? (now - native_start) / 1000000ull
                : 0;
        const uint64_t java_elapsed = java_start != 0 ? now - java_start : 0;

        // A native call that has outlasted the threshold: read every thread's pc
        // directly. Nothing else can, and the case this exists for is exactly the one
        // where the stuck thread reports nothing itself.
        //
        // The schedule is keyed on the start timestamp, not on whether an idle tick was
        // observed. Native calls can start and finish between two 250ms ticks, so
        // "native_active went false" is not a reliable signal that a new call began —
        // whereas a changed start stamp is exactly that, by construction.
        if (native_start != sampled_start) {
            sampled_start = native_start;
            next_sample_ms = kSampleAfterMs;
        }
        if (native_active && native_elapsed_ms >= next_sample_ms) {
            char reason[704];
            std::snprintf(reason, sizeof(reason), "native-%llums-%s.%s-at-%s",
                          static_cast<unsigned long long>(native_elapsed_ms), cls, method,
                          stage);
            thread_sample_report(reason);
            next_sample_ms = native_elapsed_ms + kResampleEveryMs;
        }

        const SystemMemory memory = query_system_memory();
        char line[2048];
        // Short Java calls stay RAM-only; durable watchdog records begin once
        // execution has exceeded the diagnostic threshold.
        if (!native_active && (!java_active || java_elapsed < 250000000ull)) continue;
        std::snprintf(line, sizeof(line),
                      "watchdog native_active=%d native_call_id=%llu native_thread_id=%llu native_elapsed_ms=%llu stage=%s class=%s method=%s sig=%s vm_depth=%d java_active=%d java_thread_id=%llu java_depth=%zu java_elapsed_ms=%llu java_class=%s java_method=%s java_sig=%s footprint=%llu process_headroom=%llu available=%llu low_memory=%d",
                      native_active ? 1 : 0,
                      static_cast<unsigned long long>(native_id),
                      static_cast<unsigned long long>(native_tid),
                      static_cast<unsigned long long>(native_elapsed_ms),
                      stage, cls, method, sig, depth,
                      java_active ? 1 : 0, static_cast<unsigned long long>(java_tid), java_depth,
                      static_cast<unsigned long long>(java_elapsed / 1000000), java_cls,
                      java_method, java_sig,
                      static_cast<unsigned long long>(memory.process_resident_bytes),
                      static_cast<unsigned long long>(memory.process_available_bytes),
                      static_cast<unsigned long long>(memory.available_bytes),
                      memory.low_memory ? 1 : 0);
        kudroid_persistent_breadcrumb(line);
    }
}

void copy_text(char* dst, size_t capacity, const char* src) {
    if (capacity == 0) return;
    std::snprintf(dst, capacity, "%s", src != nullptr ? src : "?");
}

}  // namespace

void native_run_begin() {
    static std::atomic<uint64_t> next_run{1};
    const uint64_t run = next_run.fetch_add(1, std::memory_order_relaxed);
    char line[160];
    std::snprintf(line, sizeof(line), "run-start run_id=%llu thread_id=%llu",
                  static_cast<unsigned long long>(run),
                  static_cast<unsigned long long>(thread_id()));
    kudroid_persistent_breadcrumb(line);
}

void native_phase(const char* phase) {
    char line[256];
    std::snprintf(line, sizeof(line), "phase=%s thread_id=%llu",
                  phase != nullptr ? phase : "?",
                  static_cast<unsigned long long>(thread_id()));
    kudroid_persistent_breadcrumb(line);
}

void native_note_fatal_signal(int signal_number, unsigned long long thread_id_value) {
    // Two relaxed stores of scalars. Called from a signal handler, so nothing here may
    // allocate, lock or format — the watchdog thread does all of that on the next tick.
    g_call.fatal_thread_id.store(thread_id_value, std::memory_order_relaxed);
    g_call.fatal_signal.store(signal_number, std::memory_order_release);
}

void native_call_enter(const char* class_name, const char* method,
                       const char* signature, int vm_lock_depth) {
    {
        std::lock_guard<std::mutex> lock(g_call.mutex);
        copy_text(g_call.class_name, sizeof(g_call.class_name), class_name);
        copy_text(g_call.method, sizeof(g_call.method), method);
        copy_text(g_call.signature, sizeof(g_call.signature), signature);
        copy_text(g_call.stage, sizeof(g_call.stage), "enter");
        g_call.vm_lock_depth = vm_lock_depth;
        g_call.native_call_id = g_call.next_call_id.load(std::memory_order_relaxed);
        g_call.native_thread_id = thread_id();
    }
    // Stamp the start of the OUTERMOST native call in flight, not this one.
    //
    // Native calls nest: a native re-enters Java, which calls another native. Timing
    // the innermost would reset the clock on every nested call and a wedge deep inside
    // a long call would never look long. The outermost is what "native code has been
    // running for N ms without returning" means, and it is the number worth escalating
    // on. It goes back to zero when the last one leaves.
    const unsigned previous = g_call.active_count.fetch_add(1, std::memory_order_acq_rel);
    if (previous == 0) {
        g_call.native_start_ns.store(now_ns(), std::memory_order_release);
    }
    t_call_id = g_call.next_call_id.fetch_add(1, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(g_call.mutex);
        g_call.native_call_id = t_call_id;
    }
    bool expected = false;
    if (g_call.started.compare_exchange_strong(expected, true,
                                               std::memory_order_acq_rel)) {
        std::thread(watchdog_main).detach();
    }
}

void native_call_stage(const char* stage) {
    if (g_call.active_count.load(std::memory_order_acquire) == 0) return;
    char line[2048];
    {
        std::lock_guard<std::mutex> lock(g_call.mutex);
        copy_text(g_call.stage, sizeof(g_call.stage), stage);
        std::snprintf(line, sizeof(line),
                      "native-stage call_id=%llu thread_id=%llu stage=%s class=%s method=%s sig=%s vm_depth=%d",
                      static_cast<unsigned long long>(t_call_id),
                      static_cast<unsigned long long>(thread_id()), stage,
                      g_call.class_name, g_call.method, g_call.signature,
                      g_call.vm_lock_depth);
    }
    kudroid_persistent_breadcrumb(line);
}

void native_call_exit() {
    unsigned count = g_call.active_count.load(std::memory_order_acquire);
    while (count != 0 && !g_call.active_count.compare_exchange_weak(
                              count, count - 1, std::memory_order_acq_rel,
                              std::memory_order_acquire)) {
    }
    // The last native call left: clear the start stamp so the next one is timed from
    // its own entry rather than inheriting an age it did not earn.
    if (count == 1) g_call.native_start_ns.store(0, std::memory_order_release);
    t_call_id = 0;
}

bool java_call_should_trace(const char* method, size_t depth) {
    if (depth <= 2) return true;
    if (method == nullptr) return false;
    return std::strstr(method, "onCreate") != nullptr ||
           std::strstr(method, "onStart") != nullptr ||
           std::strstr(method, "onResume") != nullptr ||
           std::strcmp(method, "main") == 0 ||
           std::strstr(method, "launch") != nullptr;
}

void java_call_enter(const char* class_name, const char* method,
                     const char* signature, size_t depth) {
    {
        std::lock_guard<std::mutex> lock(g_call.mutex);
        copy_text(g_call.java_class, sizeof(g_call.java_class), class_name);
        copy_text(g_call.java_method, sizeof(g_call.java_method), method);
        copy_text(g_call.java_signature, sizeof(g_call.java_signature), signature);
        g_call.java_thread_id = thread_id();
        g_call.java_depth = depth;
    g_call.java_start_ns = now_ns();
        g_call.java_last_progress_ns.store(g_call.java_start_ns, std::memory_order_release);
    }
    g_call.java_active_count.fetch_add(1, std::memory_order_acq_rel);
    bool expected = false;
    if (g_call.started.compare_exchange_strong(expected, true,
                                                std::memory_order_acq_rel)) {
        std::thread(watchdog_main).detach();
    }
    char line[1200];
    std::snprintf(line, sizeof(line),
                  "java-enter thread_id=%llu depth=%zu class=%s method=%s sig=%s",
                  static_cast<unsigned long long>(thread_id()), depth,
                  class_name != nullptr ? class_name : "?",
                  method != nullptr ? method : "?",
                  signature != nullptr ? signature : "?");
    // Enter/exit are intentionally not persisted: doing so for every tiny
    // framework helper turns tracing itself into a launch-time workload.
    (void)line;
}

void java_call_exit() {
    if (g_call.java_active_count.load(std::memory_order_acquire) == 0) return;
    const uint64_t elapsed = now_ns() - g_call.java_start_ns;
    char line[1200];
    {
        std::lock_guard<std::mutex> lock(g_call.mutex);
        std::snprintf(line, sizeof(line),
                      "java-exit thread_id=%llu depth=%zu duration_ms=%llu class=%s method=%s sig=%s",
                      static_cast<unsigned long long>(g_call.java_thread_id), g_call.java_depth,
                      static_cast<unsigned long long>(elapsed / 1000000), g_call.java_class,
                      g_call.java_method, g_call.java_signature);
    }
    if (elapsed >= 250000000ull) kudroid_persistent_breadcrumb(line);
    g_call.java_active_count.fetch_sub(1, std::memory_order_acq_rel);
}

}  // namespace kudroid
