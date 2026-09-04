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

// ─────────────────────────────────────────────────────────────────────────────
// Per-thread call stacks.
//
// This was one global slot, overwritten by whichever call entered last and never
// restored on the way out. That single fact produced the most misleading log KuDroid
// has emitted: ULTRAKILL wedged inside UnityPlayer.nativeRender (call 13), which
// called back into Java, which called JNIBridge.invoke (call 14). Call 14 completed
// in 435 microseconds — and then, for the next 47 seconds, every watchdog line
// carried call 13's elapsed time beside call 14's class, method and stage.
//
// So the log said the process was hung in `JNIBridge.invoke at before-result-decode`,
// which had already returned, while `nativeRender at before-trampoline` — the call
// that never came back — was never named again after its second line. The thread
// sampler's reason= field inherited the same wrong name, so all five samples were
// filed under the wrong method too. Every individual field was plausible; only the
// combination was a lie, which is why it survived several readings.
//
// A stack per thread is the fix, and it has to be a stack rather than a save/restore
// pair: what the watchdog needs is not merely the outer call's name back, but the
// ability to say "this thread went in at nativeRender and is now N levels deep",
// which is the shape of the answer for every nested-call hang.
// ─────────────────────────────────────────────────────────────────────────────

// Deep enough for real nesting (native → Java → native → Java …) and small enough
// that the table costs little. Overflow stops recording deeper frames rather than
// failing: losing the innermost name is acceptable, corrupting the record is not.
constexpr unsigned kMaxNativeDepth = 24;
constexpr unsigned kMaxJavaDepth = 24;

// More threads than a guest engine runs. Beyond this a thread is not tracked, which
// is a lost diagnostic, never a wrong one.
constexpr int kMaxThreadSlots = 128;

struct NativeFrame {
    char class_name[256] = {};
    char method[256] = {};
    char signature[512] = {};
    char stage[64] = {};
    int vm_lock_depth = 0;
    uint64_t call_id = 0;
    uint64_t start_ns = 0;
};

struct JavaFrame {
    char class_name[256] = {};
    char method[256] = {};
    char signature[512] = {};
    size_t depth = 0;
    uint64_t start_ns = 0;
};

struct ThreadRecord {
    // Owned by the thread; only it claims and clears. Read by the watchdog.
    std::atomic<bool> claimed{false};
    std::atomic<uint64_t> thread_id{0};

    // Published AFTER the frame contents, read BEFORE them, so the watchdog can
    // never see a half-written frame.
    std::atomic<unsigned> native_depth{0};
    std::atomic<unsigned> java_depth{0};

    // Guards the frame arrays. Held only for the microseconds it takes to copy a few
    // strings, and never by a thread that is blocked in guest code — a wedged thread
    // is wedged inside its native call, having already released this.
    std::mutex mutex;
    NativeFrame native[kMaxNativeDepth];
    JavaFrame java[kMaxJavaDepth];
};

ThreadRecord g_threads[kMaxThreadSlots];

struct Globals {
    std::atomic<bool> started{false};
    std::atomic<uint64_t> next_call_id{1};

    // Set from a signal handler when a thread takes a fatal fault. Only scalars, and
    // only stores, because that is all a signal handler may do.
    //
    // Without this the watchdog cannot tell a hang from a crash, and it defaults to
    // calling everything a hang: UnityMain faulted, parked in the crash handler, and
    // the watchdog kept reporting a native call as running when the thread executing
    // it was dead.
    std::atomic<int> fatal_signal{0};
    std::atomic<uint64_t> fatal_thread_id{0};
};

Globals g;

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

void copy_text(char* dst, size_t capacity, const char* src) {
    if (capacity == 0) return;
    std::snprintf(dst, capacity, "%s", src != nullptr ? src : "?");
}

// This thread's record, claimed on first use and kept for the thread's lifetime.
thread_local ThreadRecord* t_record = nullptr;

ThreadRecord* acquire_record() {
    if (t_record != nullptr) return t_record;
    for (int i = 0; i < kMaxThreadSlots; ++i) {
        bool expected = false;
        if (g_threads[i].claimed.compare_exchange_strong(expected, true,
                                                         std::memory_order_acq_rel)) {
            g_threads[i].thread_id.store(thread_id(), std::memory_order_relaxed);
            t_record = &g_threads[i];
            return t_record;
        }
    }
    return nullptr;  // table full: this thread is not tracked
}

// Release the record when a thread exits, but only if it left no call behind.
//
// A thread that dies mid-call must keep its record: that is the crash case, and the
// half-finished stack is the evidence. A thread that finished cleanly should give the
// slot back, or a guest that churns worker threads exhausts the table and stops being
// tracked at all.
struct RecordReleaser {
    ~RecordReleaser() {
        ThreadRecord* r = t_record;
        if (r == nullptr) return;
        if (r->native_depth.load(std::memory_order_acquire) != 0) return;
        if (r->java_depth.load(std::memory_order_acquire) != 0) return;
        t_record = nullptr;
        r->thread_id.store(0, std::memory_order_relaxed);
        r->claimed.store(false, std::memory_order_release);
    }
};
thread_local RecordReleaser t_releaser;

void start_watchdog_once();

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Snapshot
// ─────────────────────────────────────────────────────────────────────────────

CallReport call_report_snapshot() {
    CallReport out;

    // The thread whose OUTERMOST native call started earliest. "Native code has been
    // running for N ms without returning" is a statement about the outermost call, so
    // the oldest of those is the one worth escalating on.
    const ThreadRecord* native_pick = nullptr;
    uint64_t native_oldest = 0;
    const ThreadRecord* java_pick = nullptr;
    uint64_t java_oldest = 0;

    for (int i = 0; i < kMaxThreadSlots; ++i) {
        const ThreadRecord& r = g_threads[i];
        if (!r.claimed.load(std::memory_order_acquire)) continue;

        if (r.native_depth.load(std::memory_order_acquire) != 0) {
            const uint64_t started = r.native[0].start_ns;
            if (started != 0 && (native_pick == nullptr || started < native_oldest)) {
                native_pick = &r;
                native_oldest = started;
            }
        }
        if (r.java_depth.load(std::memory_order_acquire) != 0) {
            const uint64_t started = r.java[0].start_ns;
            if (started != 0 && (java_pick == nullptr || started < java_oldest)) {
                java_pick = &r;
                java_oldest = started;
            }
        }
    }

    const uint64_t now = now_ns();

    if (native_pick != nullptr) {
        ThreadRecord& r = const_cast<ThreadRecord&>(*native_pick);
        std::lock_guard<std::mutex> lock(r.mutex);
        const unsigned depth = r.native_depth.load(std::memory_order_acquire);
        if (depth != 0) {
            const NativeFrame& outer = r.native[0];
            out.native_active = true;
            out.native_call_id = outer.call_id;
            out.native_thread_id = r.thread_id.load(std::memory_order_relaxed);
            out.native_elapsed_ms =
                now > outer.start_ns ? (now - outer.start_ns) / 1000000ull : 0;
            out.native_vm_lock_depth = outer.vm_lock_depth;
            out.native_stack_depth = depth;
            copy_text(out.native_class, sizeof(out.native_class), outer.class_name);
            copy_text(out.native_method, sizeof(out.native_method), outer.method);
            copy_text(out.native_signature, sizeof(out.native_signature), outer.signature);
            copy_text(out.native_stage, sizeof(out.native_stage), outer.stage);
            if (depth > 1) {
                const unsigned last = depth <= kMaxNativeDepth ? depth - 1 : kMaxNativeDepth - 1;
                const NativeFrame& inner = r.native[last];
                copy_text(out.native_inner_class, sizeof(out.native_inner_class),
                          inner.class_name);
                copy_text(out.native_inner_method, sizeof(out.native_inner_method),
                          inner.method);
                copy_text(out.native_inner_stage, sizeof(out.native_inner_stage), inner.stage);
            }
        }
    }

    if (java_pick != nullptr) {
        ThreadRecord& r = const_cast<ThreadRecord&>(*java_pick);
        std::lock_guard<std::mutex> lock(r.mutex);
        if (r.java_depth.load(std::memory_order_acquire) != 0) {
            const JavaFrame& outer = r.java[0];
            out.java_active = true;
            out.java_thread_id = r.thread_id.load(std::memory_order_relaxed);
            out.java_elapsed_ms =
                now > outer.start_ns ? (now - outer.start_ns) / 1000000ull : 0;
            out.java_depth = outer.depth;
            copy_text(out.java_class, sizeof(out.java_class), outer.class_name);
            copy_text(out.java_method, sizeof(out.java_method), outer.method);
            copy_text(out.java_signature, sizeof(out.java_signature), outer.signature);
        }
    }

    return out;
}

void call_telemetry_reset_for_test() {
    for (int i = 0; i < kMaxThreadSlots; ++i) {
        g_threads[i].native_depth.store(0, std::memory_order_relaxed);
        g_threads[i].java_depth.store(0, std::memory_order_relaxed);
        g_threads[i].thread_id.store(0, std::memory_order_relaxed);
        g_threads[i].claimed.store(false, std::memory_order_relaxed);
    }
    t_record = nullptr;
    g.fatal_signal.store(0, std::memory_order_relaxed);
    g.fatal_thread_id.store(0, std::memory_order_relaxed);
}

namespace {

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
    uint64_t sampled_call_id = 0;

    // Set once a fatal signal has been seen, so the announcement is made exactly once
    // while the loop itself keeps running.
    bool fatal_announced = false;

    while (g.started.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));

        // A thread has taken a fatal signal. Announce it once, then stop reporting
        // DURATIONS — but keep the loop alive.
        //
        // The durations become lies at that moment: the faulting thread never reaches
        // native_call_exit, so its stack stays put and the watchdog goes on describing
        // a call whose thread is gone. That is what reported
        // `native_call_id=14 native_elapsed_ms=9429` for nine seconds after UnityMain
        // had already aborted, and it is worse than silence because a reader chases it.
        //
        // Returning outright — which is what this did first — throws away the
        // diagnostics that are still TRUE. The blocking-wait registry is per-thread and
        // self-clearing, so it keeps working after another thread dies, and a process
        // that survives a fault its own handler fixed would otherwise run the rest of
        // the session with no stall detection at all.
        const int fatal = g.fatal_signal.load(std::memory_order_acquire);
        if (fatal != 0 && !fatal_announced) {
            fatal_announced = true;
            char line[256];
            std::snprintf(line, sizeof(line),
                          "watchdog call-timing stopped reason=fatal-signal signal=%d "
                          "thread_id=%llu (stall reporting continues)",
                          fatal,
                          static_cast<unsigned long long>(
                              g.fatal_thread_id.load(std::memory_order_relaxed)));
            kudroid_persistent_breadcrumb(line);
        }

        // Name any thread parked on a blocking wait for too long.
        //
        // This runs before the progress check below and independently of it: a stuck
        // thread is worth reporting whether or not a native or Java call happens to be
        // in flight on some other thread. Three seconds is long enough that no real
        // handshake, asset load or lock convoy trips it, and short enough to appear
        // well before a user gives up and force-quits.
        blocking_wait_report_stalled(/*threshold_ms=*/3000);

        // Threads that parked themselves with no deadline, at a much longer threshold
        // and a lower severity. An idle Looper thread is normal and permanent — every
        // app has one — so it must not share a line shape with a real stall. Thirty
        // seconds, because the only reason to mention it at all is when a deadlock
        // turns out to involve a notifier that died.
        blocking_wait_report_idle(/*threshold_ms=*/30000);

        const CallReport report = call_report_snapshot();

        // A native call that has outlasted the threshold: read every thread's pc
        // directly. Nothing else can, and the case this exists for is exactly the one
        // where the stuck thread reports nothing itself.
        //
        // The schedule is keyed on the call id, not on whether an idle tick was
        // observed. Native calls can start and finish between two 250ms ticks, so
        // "native_active went false" is not a reliable signal that a new call began —
        // whereas a changed outermost call id is exactly that, by construction.
        //
        // Skipped once a thread has faulted: the elapsed time is measured against a
        // call that will never return, so it grows without bound and would trigger a
        // sample every ten seconds forever, each one describing a thread that is gone.
        if (report.native_call_id != sampled_call_id) {
            sampled_call_id = report.native_call_id;
            next_sample_ms = kSampleAfterMs;
        }
        if (!fatal_announced && report.native_active &&
            report.native_elapsed_ms >= next_sample_ms) {
            // The reason names the OUTERMOST call — the one that has been running for
            // this long — and, when the thread went deeper, the innermost as well.
            // Naming only one of them is what filed all five ULTRAKILL samples under
            // a method that had already returned.
            char reason[1536];
            if (report.native_stack_depth > 1) {
                std::snprintf(reason, sizeof(reason), "native-%llums-%s.%s-at-%s-inner-%s.%s",
                              report.native_elapsed_ms, report.native_class,
                              report.native_method, report.native_stage,
                              report.native_inner_class, report.native_inner_method);
            } else {
                std::snprintf(reason, sizeof(reason), "native-%llums-%s.%s-at-%s",
                              report.native_elapsed_ms, report.native_class,
                              report.native_method, report.native_stage);
            }
            thread_sample_report(reason);
            next_sample_ms = report.native_elapsed_ms + kResampleEveryMs;
        }

        // Call timing is over once a thread has faulted, for the reason given at the
        // top of the loop. The blocking-wait check above still runs.
        if (fatal_announced) continue;

        // Short Java calls stay RAM-only; durable watchdog records begin once
        // execution has exceeded the diagnostic threshold.
        if (!report.native_active && (!report.java_active || report.java_elapsed_ms < 250)) {
            continue;
        }

        const SystemMemory memory = query_system_memory();
        char inner[768] = {};
        if (report.native_stack_depth > 1) {
            std::snprintf(inner, sizeof(inner),
                          " inner_class=%s inner_method=%s inner_stage=%s",
                          report.native_inner_class, report.native_inner_method,
                          report.native_inner_stage);
        }
        char line[4096];
        std::snprintf(line, sizeof(line),
                      "watchdog native_active=%d native_call_id=%llu native_thread_id=%llu "
                      "native_elapsed_ms=%llu stage=%s class=%s method=%s sig=%s vm_depth=%d "
                      "native_stack_depth=%u%s java_active=%d java_thread_id=%llu "
                      "java_depth=%zu java_elapsed_ms=%llu java_class=%s java_method=%s "
                      "java_sig=%s footprint=%llu process_headroom=%llu available=%llu "
                      "low_memory=%d",                      report.native_active ? 1 : 0, report.native_call_id,
                      report.native_thread_id, report.native_elapsed_ms, report.native_stage,
                      report.native_class, report.native_method, report.native_signature,
                      report.native_vm_lock_depth, report.native_stack_depth, inner,
                      report.java_active ? 1 : 0, report.java_thread_id, report.java_depth,
                      report.java_elapsed_ms, report.java_class, report.java_method,
                      report.java_signature,
                      static_cast<unsigned long long>(memory.process_resident_bytes),
                      static_cast<unsigned long long>(memory.process_available_bytes),
                      static_cast<unsigned long long>(memory.available_bytes),
                      memory.low_memory ? 1 : 0);
        kudroid_persistent_breadcrumb(line);
    }
}

void start_watchdog_once() {
    bool expected = false;
    if (g.started.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        std::thread(watchdog_main).detach();
    }
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
    g.fatal_thread_id.store(thread_id_value, std::memory_order_relaxed);
    g.fatal_signal.store(signal_number, std::memory_order_release);
}

void native_call_enter(const char* class_name, const char* method,
                       const char* signature, int vm_lock_depth) {
    (void)&t_releaser;  // instantiate the thread's cleanup
    ThreadRecord* r = acquire_record();
    if (r == nullptr) {
        start_watchdog_once();
        return;
    }

    const uint64_t call_id = g.next_call_id.fetch_add(1, std::memory_order_relaxed);
    const unsigned depth = r->native_depth.load(std::memory_order_relaxed);
    if (depth < kMaxNativeDepth) {
        std::lock_guard<std::mutex> lock(r->mutex);
        NativeFrame& f = r->native[depth];
        copy_text(f.class_name, sizeof(f.class_name), class_name);
        copy_text(f.method, sizeof(f.method), method);
        copy_text(f.signature, sizeof(f.signature), signature);
        copy_text(f.stage, sizeof(f.stage), "enter");
        f.vm_lock_depth = vm_lock_depth;
        f.call_id = call_id;
        f.start_ns = now_ns();
    }
    // Published last: the watchdog reads the depth with acquire before touching any
    // frame, so it can never see one that is still being filled in.
    r->native_depth.store(depth + 1, std::memory_order_release);

    start_watchdog_once();
}

void native_call_stage(const char* stage) {
    ThreadRecord* r = t_record;
    if (r == nullptr) return;
    const unsigned depth = r->native_depth.load(std::memory_order_relaxed);
    if (depth == 0) return;

    char line[2048];
    {
        std::lock_guard<std::mutex> lock(r->mutex);
        // The stage belongs to the frame that is running, which is the innermost one.
        const unsigned index = depth <= kMaxNativeDepth ? depth - 1 : kMaxNativeDepth - 1;
        NativeFrame& f = r->native[index];
        copy_text(f.stage, sizeof(f.stage), stage);
        std::snprintf(line, sizeof(line),
                      "native-stage call_id=%llu thread_id=%llu stage=%s class=%s method=%s "
                      "sig=%s vm_depth=%d depth=%u",
                      static_cast<unsigned long long>(f.call_id),
                      static_cast<unsigned long long>(thread_id()), stage, f.class_name,
                      f.method, f.signature, f.vm_lock_depth, depth);
    }
    kudroid_persistent_breadcrumb(line);
}

void native_call_exit() {
    ThreadRecord* r = t_record;
    if (r == nullptr) return;
    const unsigned depth = r->native_depth.load(std::memory_order_relaxed);
    if (depth == 0) return;
    // Popping restores the enclosing frame by construction. That is the whole fix:
    // the outer call's identity was never saved, so it could not be restored, and the
    // watchdog reported the inner call's name against the outer call's clock.
    r->native_depth.store(depth - 1, std::memory_order_release);
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
                     const char* signature, size_t depth_hint) {
    (void)&t_releaser;
    ThreadRecord* r = acquire_record();
    if (r == nullptr) {
        start_watchdog_once();
        return;
    }

    const unsigned depth = r->java_depth.load(std::memory_order_relaxed);
    if (depth < kMaxJavaDepth) {
        std::lock_guard<std::mutex> lock(r->mutex);
        JavaFrame& f = r->java[depth];
        copy_text(f.class_name, sizeof(f.class_name), class_name);
        copy_text(f.method, sizeof(f.method), method);
        copy_text(f.signature, sizeof(f.signature), signature);
        f.depth = depth_hint;
        f.start_ns = now_ns();
    }
    r->java_depth.store(depth + 1, std::memory_order_release);

    start_watchdog_once();
}

void java_call_exit() {
    ThreadRecord* r = t_record;
    if (r == nullptr) return;
    const unsigned depth = r->java_depth.load(std::memory_order_relaxed);
    if (depth == 0) return;

    char line[1200];
    bool worth_writing = false;
    {
        std::lock_guard<std::mutex> lock(r->mutex);
        const unsigned index = depth <= kMaxJavaDepth ? depth - 1 : kMaxJavaDepth - 1;
        const JavaFrame& f = r->java[index];
        const uint64_t elapsed = now_ns() - f.start_ns;
        worth_writing = elapsed >= 250000000ull;
        if (worth_writing) {
            std::snprintf(line, sizeof(line),
                          "java-exit thread_id=%llu depth=%zu duration_ms=%llu class=%s "
                          "method=%s sig=%s",
                          static_cast<unsigned long long>(
                              r->thread_id.load(std::memory_order_relaxed)),
                          f.depth, static_cast<unsigned long long>(elapsed / 1000000),
                          f.class_name, f.method, f.signature);
        }
    }
    r->java_depth.store(depth - 1, std::memory_order_release);
    if (worth_writing) kudroid_persistent_breadcrumb(line);
}

}  // namespace kudroid
