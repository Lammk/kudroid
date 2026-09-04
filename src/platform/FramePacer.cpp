// Frame pacing: AChoreographer and ANativeWindow_setFrameRate.
//
// Why this exists. ULTRAKILL launched, brought up Vulkan on the A13, created its
// swapchain — and then stopped, permanently, with no crash and nothing in the log
// after the last symbol lookup. The lookups were these:
//
//     missing symbol bound to dummy: ANativeWindow_setFrameRate
//     missing symbol bound to dummy: AChoreographer_getInstance
//     missing symbol bound to dummy: AChoreographer_postFrameCallback
//     ...
//     thread-create                       tid=UnityMain
//     cond-wait-enter cond=0x300f15e10    tid=UnityMain      <-- forever
//     thread-entry tid=3711786
//     thread-exit  tid=3711786            <-- started and left immediately
//
// All five bound to kudroid_universal_dummy, which returns 0. So getInstance()
// returned NULL, Unity's frame-pacer thread had nothing to post a callback to and
// exited without ever signalling the condition variable UnityMain was waiting on.
// UnityMain sat in __psynch_cvwait for the rest of the session: five thread samples
// forty seconds apart, identical pc, identical sp, cpu_ms unchanged at 383.
// UnityGfxDeviceWorker never accumulated a millisecond, so not one frame was ever
// submitted.
//
// A dummy that returns 0 is the worst possible answer for a factory function. It is
// indistinguishable from "no Choreographer on this thread", which on Android is a
// real and rare condition that callers handle by giving up — so the guest did
// exactly what it was written to do, and the failure surfaced as a hang in a
// condition variable several layers away from the cause.
#include "kudroid/platform/FramePacer.h"

#include "kudroid/BionicShim.h"
#include "kudroid/Log.h"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <dispatch/dispatch.h>
#include <objc/message.h>
#include <objc/runtime.h>
#endif

// The process-wide looper, from SyscallShim. A Choreographer's wake pipe is
// registered with it so a frame callback can run on a guest thread — Android
// dispatches Choreographer callbacks through the Looper of the thread that
// registered them, and a guest callback may touch state that only exists there.
extern "C" void* bionic_ALooper_forThread(void);
extern "C" int bionic_ALooper_addFd(void* looper, int fd, int ident, int events,
                                    void* callback, void* data);
extern "C" int bionic_ALooper_removeFd(void* looper, int fd);

namespace kudroid {
namespace {

// ALOOPER_EVENT_INPUT. Defined locally rather than included: the looper's constants
// live inside SyscallShim.cpp's translation unit.
constexpr int kLooperEventInput = 0x0001;

// A distinctive ident so a looper wake from a frame pipe is identifiable in a log
// and cannot be confused with the input queue's.
constexpr int kFrameLooperIdent = 0x4B465250;  // 'KFRP'

void paceLog(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void paceLog(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    ::kudroid::log::write(::kudroid::log::kDebug, "KuDroidPace", "%s", buf);
}

// CLOCK_MONOTONIC, deliberately, and not steady_clock.
//
// frameTimeNanos must sit on the same timeline the guest reads for itself. Android's
// Choreographer reports System.nanoTime(), which is CLOCK_MONOTONIC, and the guest
// gets that value through bionic_clock_gettime(CLOCK_MONOTONIC) — mapped to Darwin
// clock 6. steady_clock on Darwin is mach_absolute_time, which does not advance
// across sleep: a frame pacer subtracting one from the other computes a delta that
// jumps by however long the device slept, then either stalls on a deadline in the
// past or floods frames trying to catch up.
uint64_t mono_ns() {
    struct timespec ts {};
    if (::clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ull +
           static_cast<uint64_t>(ts.tv_nsec);
}

uint64_t self_thread_id() {
#if defined(__APPLE__)
    uint64_t tid = 0;
    ::pthread_threadid_np(nullptr, &tid);
    return tid;
#else
    return static_cast<uint64_t>(::pthread_self());
#endif
}

struct FrameCallbackEntry {
    void* fn = nullptr;
    void* data = nullptr;
    uint64_t due_ns = 0;
    // Set once the pacer has written to the wake pipe for this entry, so it is not
    // nudged again every tick while the guest gets round to polling.
    bool nudged = false;
};

struct RefreshCallbackEntry {
    void* fn = nullptr;
    void* data = nullptr;
};

// One per guest thread that asked for a Choreographer. A fixed table, because the
// pacer scans it while guest threads are blocked and the scan must not allocate.
// Overflow falls back to a shared instance, which still delivers frames: dispatching
// on a neighbouring thread is a lesser fault than returning NULL, which is the bug
// this file exists to remove.
constexpr int kMaxInstances = 64;

struct Choreographer {
    std::atomic<bool> claimed{false};
    std::atomic<uint64_t> owner_thread{0};

    std::mutex mtx;
    std::vector<FrameCallbackEntry> frame_callbacks;
    std::vector<RefreshCallbackEntry> refresh_callbacks;

    int wake_pipe[2] = {-1, -1};
    bool pipe_registered = false;

    // Published without the lock so the pacer can skip an idle instance without
    // taking it. Kept in step with frame_callbacks.size().
    std::atomic<unsigned> pending{0};
};

// ── pacer state ─────────────────────────────────────────────────────────────
//
// Everything with a non-trivial destructor lives in one heap object that is never
// freed. That is deliberate, and it is the second shutdown bug this seam produced.
//
// The pacer thread outlives main(). It has to: a guest can post a frame and return,
// and there is no point in the lifecycle where KuDroid can prove no further frame
// will be asked for. So at process exit the thread is typically parked on the
// condition variable — and if these objects have static storage duration, static
// destruction runs `~condition_variable` and `~mutex` underneath it, plus `~vector`
// on every queue the thread scans.
//
// Both failure modes were observed. Holding the thread in a global `std::thread`
// aborted at exit ("terminate called without an active exception", because ~thread()
// saw joinable()). Detaching it fixed that and produced a HANG instead: exit()
// destroying a condition_variable that still has a waiter never returns on glibc.
// Neither showed up in the test suite, because every test calls the reset seam first
// — so the suite was green while any real app that rendered one frame failed to exit.
//
// Leaking is the correct answer rather than a workaround: the memory is reclaimed by
// the OS at process teardown, and there is no window in which a live thread can
// observe a destroyed object.
struct PacerState {
    Choreographer instances[kMaxInstances];
    // For a thread that could not claim a slot.
    Choreographer shared;

    std::mutex mtx;
    std::condition_variable cv;
    bool started = false;
    bool stop = false;

    // Cleared by the pacer thread on its way out. The test seam waits on this rather
    // than joining, because the thread is detached.
    std::atomic<bool> running{false};
};

PacerState& state() {
    // Never deleted; see the comment above.
    static PacerState* s = new PacerState();
    return *s;
}

thread_local Choreographer* t_instance = nullptr;

// Frames delivered through the guest's looper, and frames the pacer had to deliver
// itself because nobody collected them.
//
// The distinction drives the grace period below. A guest that polls its looper gets
// callbacks on its own thread with no added latency. A guest that never polls would
// otherwise have every frame delivered one interval late — halving its frame rate —
// so after a couple of unclaimed frames the pacer stops waiting.
std::atomic<uint64_t> g_looper_deliveries{0};
std::atomic<uint64_t> g_direct_deliveries{0};
std::atomic<bool> g_direct_mode{false};

// ── display refresh rate ────────────────────────────────────────────────────

constexpr int64_t kDefaultVsyncPeriodNs = 16666667;  // 60 Hz

std::atomic<int64_t> g_vsync_period_ns{kDefaultVsyncPeriodNs};
std::atomic<bool> g_vsync_query_started{false};

// A guest hint from ANativeWindow_setFrameRate, as nanoseconds per frame. Zero means
// no preference.
std::atomic<int64_t> g_requested_period_ns{0};

#if defined(__APPLE__)
// UIScreen.maximumFramesPerSecond, read on the main thread.
//
// Asynchronously, and that is not laziness: UIKit objects may only be touched on the
// main thread, and a dispatch_sync from a guest thread deadlocks whenever the main
// thread is itself waiting on the guest — which is the normal state during startup.
// Until the answer arrives the period is 60 Hz, which is right for most devices and
// wrong only in being conservative.
void start_vsync_query() {
    bool expected = false;
    if (!g_vsync_query_started.compare_exchange_strong(expected, true,
                                                       std::memory_order_acq_rel)) {
        return;
    }
    dispatch_async(dispatch_get_main_queue(), ^{
        Class screenClass = objc_getClass("UIScreen");
        if (screenClass == nullptr) return;
        using IdFn = id (*)(id, SEL);
        using IntFn = long (*)(id, SEL);
        auto msgId = reinterpret_cast<IdFn>(objc_msgSend);
        auto msgInt = reinterpret_cast<IntFn>(objc_msgSend);
        id screen = msgId(reinterpret_cast<id>(screenClass), sel_registerName("mainScreen"));
        if (screen == nullptr) return;
        const long fps = msgInt(screen, sel_registerName("maximumFramesPerSecond"));
        if (fps <= 0) return;
        const int64_t period = 1000000000ll / fps;
        g_vsync_period_ns.store(period, std::memory_order_relaxed);
        paceLog("display refresh rate = %ld Hz (vsync period %lld ns)", fps,
                static_cast<long long>(period));
    });
}
#else
void start_vsync_query() { g_vsync_query_started.store(true, std::memory_order_relaxed); }
#endif

int64_t vsync_period_ns() {
    start_vsync_query();
    const int64_t period = g_vsync_period_ns.load(std::memory_order_relaxed);
    return period > 0 ? period : kDefaultVsyncPeriodNs;
}

int64_t interval_ns() {
    const int64_t display = vsync_period_ns();
    const int64_t requested = g_requested_period_ns.load(std::memory_order_relaxed);
    // A hint can only ask for FEWER frames. Asking for more than the display can
    // present would have the pacer deliver callbacks the display cannot honour, and a
    // guest pacing itself from those timestamps would run ahead of the compositor.
    return requested > display ? requested : display;
}

// ── instance management ─────────────────────────────────────────────────────

int dispatch_instance(Choreographer* c);

void ensure_pipe_locked(Choreographer* c) {
    if (c->wake_pipe[0] >= 0) return;
    int fds[2] = {-1, -1};
    if (::pipe(fds) != 0) {
        paceLog("wake pipe creation failed (errno %d); frames fall back to the pacer thread",
                errno);
        return;
    }
    // Non-blocking on both ends. The read end is drained until EAGAIN, and the write
    // end must never block the pacer: a guest that stops polling would otherwise fill
    // the pipe and wedge the one thread that can still deliver frames.
    ::fcntl(fds[0], F_SETFL, ::fcntl(fds[0], F_GETFL, 0) | O_NONBLOCK);
    ::fcntl(fds[1], F_SETFL, ::fcntl(fds[1], F_GETFL, 0) | O_NONBLOCK);
    c->wake_pipe[0] = fds[0];
    c->wake_pipe[1] = fds[1];
}

// Invoked by bionic_ALooper_pollAll on whichever guest thread is polling. `data` is
// the instance, so the right queue is drained no matter which thread woke up —
// KuDroid has one process-wide looper, so the poller is not necessarily the poster.
int frame_pipe_looper_callback(int fd, int events, void* data) {
    (void)fd;
    (void)events;
    auto* c = static_cast<Choreographer*>(data);
    if (c != nullptr) dispatch_instance(c);
    return 1;  // keep the fd registered
}

void register_pipe_locked(Choreographer* c) {
    if (c->pipe_registered || c->wake_pipe[0] < 0) return;
    void* looper = bionic_ALooper_forThread();
    if (looper == nullptr) return;
    bionic_ALooper_addFd(looper, c->wake_pipe[0], kFrameLooperIdent, kLooperEventInput,
                         reinterpret_cast<void*>(&frame_pipe_looper_callback), c);
    c->pipe_registered = true;
}

Choreographer* acquire_instance() {
    if (t_instance != nullptr) return t_instance;
    for (int i = 0; i < kMaxInstances; ++i) {
        bool expected = false;
        if (state().instances[i].claimed.compare_exchange_strong(expected, true,
                                                           std::memory_order_acq_rel)) {
            state().instances[i].owner_thread.store(self_thread_id(), std::memory_order_relaxed);
            t_instance = &state().instances[i];
            return t_instance;
        }
    }
    t_instance = &state().shared;
    return t_instance;
}

void drain_pipe(Choreographer* c) {
    const int fd = c->wake_pipe[0];
    if (fd < 0) return;
    // Until EAGAIN. A pipe left readable makes poll() return immediately every time,
    // which turns the guest's event loop into a spin — the same failure the input
    // queue's drain exists to prevent.
    char scratch[64];
    for (int i = 0; i < (1 << 16); ++i) {
        const ssize_t n = ::read(fd, scratch, sizeof(scratch));
        if (n <= 0) break;
    }
}

// Run every frame callback on `c` whose deadline has passed. Entries are moved out
// under the lock and invoked outside it: a guest callback re-posts itself, which
// would deadlock against a lock held across the call.
int dispatch_instance(Choreographer* c) {
    if (c == nullptr) return 0;
    drain_pipe(c);

    const uint64_t now = mono_ns();
    FrameCallbackEntry due[16];
    int count = 0;
    {
        std::lock_guard<std::mutex> lock(c->mtx);
        for (size_t i = 0; i < c->frame_callbacks.size() && count < 16;) {
            if (c->frame_callbacks[i].due_ns <= now) {
                due[count++] = c->frame_callbacks[i];
                c->frame_callbacks.erase(c->frame_callbacks.begin() + static_cast<long>(i));
                continue;
            }
            ++i;
        }
        c->pending.store(static_cast<unsigned>(c->frame_callbacks.size()),
                         std::memory_order_release);
    }

    for (int i = 0; i < count; ++i) {
        if (due[i].fn == nullptr) continue;
        // void(long frameTimeNanos, void* data) — the 32-bit and 64-bit NDK forms
        // share this ABI on arm64, where long is 64-bit.
        reinterpret_cast<void (*)(int64_t, void*)>(due[i].fn)(static_cast<int64_t>(now),
                                                             due[i].data);
    }
    return count;
}

// ── pacer thread ────────────────────────────────────────────────────────────

void pacer_main() {
    // Guest TLS before anything else. A frame callback is guest code, and guest code
    // built for Android reads tpidr_el0 directly; the loader rewrites those reads into
    // BRK traps a handler services lazily, but paying a trap per access on the one
    // thread that guarantees frame delivery is not a reasonable cost.
    bionic_init_main_thread_tls();

    // Cleared on every exit path, so the test seam can wait for this thread to be gone
    // before closing the pipes it reads.
    struct RunningFlag {
        ~RunningFlag() { state().running.store(false, std::memory_order_release); }
    } running_flag;

    while (true) {
        {
            std::unique_lock<std::mutex> lock(state().mtx);
            if (state().stop) return;
        }

        const uint64_t now = mono_ns();
        const int64_t interval = interval_ns();
        // How long the guest's own looper is given to collect a due frame before the
        // pacer delivers it. Zero once the guest has shown it does not poll, so a
        // non-looper guest runs at full rate instead of half.
        const uint64_t grace = g_direct_mode.load(std::memory_order_relaxed)
                                   ? 0
                                   : static_cast<uint64_t>(interval);

        uint64_t next_wake = 0;  // 0 = nothing pending anywhere

        for (int i = 0; i <= kMaxInstances; ++i) {
            Choreographer* c = (i == kMaxInstances) ? &state().shared : &state().instances[i];
            if (c->pending.load(std::memory_order_acquire) == 0) continue;

            bool wake_needed = false;
            bool deliver_now = false;
            uint64_t earliest = 0;
            {
                std::lock_guard<std::mutex> lock(c->mtx);
                for (FrameCallbackEntry& e : c->frame_callbacks) {
                    if (e.due_ns <= now) {
                        if (!e.nudged) {
                            e.nudged = true;
                            wake_needed = true;
                        }
                        if (now >= e.due_ns + grace) deliver_now = true;
                        const uint64_t at = e.due_ns + grace;
                        if (earliest == 0 || at < earliest) earliest = at;
                    } else if (earliest == 0 || e.due_ns < earliest) {
                        earliest = e.due_ns;
                    }
                }
                // No pipe means the looper path does not exist for this instance, so
                // there is nothing to wait for.
                if (c->wake_pipe[0] < 0 && wake_needed) deliver_now = true;
            }

            if (wake_needed && c->wake_pipe[1] >= 0) {
                const uint8_t byte = 1;
                const ssize_t written = ::write(c->wake_pipe[1], &byte, 1);
                (void)written;
            }

            if (deliver_now) {
                const int ran = dispatch_instance(c);
                if (ran > 0) {
                    const uint64_t total =
                        g_direct_deliveries.fetch_add(static_cast<uint64_t>(ran),
                                                      std::memory_order_relaxed) +
                        static_cast<uint64_t>(ran);
                    // Two frames the guest never collected and none it ever did: it is
                    // not polling. Stop granting grace.
                    if (total >= 2 &&
                        g_looper_deliveries.load(std::memory_order_relaxed) == 0 &&
                        !g_direct_mode.exchange(true, std::memory_order_relaxed)) {
                        paceLog("guest does not poll its looper for frames; delivering "
                                "directly from the pacer thread");
                    }
                }
            }

            if (earliest != 0 && (next_wake == 0 || earliest < next_wake)) next_wake = earliest;
        }

        std::unique_lock<std::mutex> lock(state().mtx);
        if (state().stop) return;
        if (next_wake == 0) {
            // Nothing queued anywhere: park until a post wakes us. A pacer that spun
            // at the frame rate with no frames requested would burn a core for as long
            // as an app sat on a menu.
            state().cv.wait(lock);
            continue;
        }
        const uint64_t after = mono_ns();
        if (next_wake > after) {
            uint64_t delay = next_wake - after;
            // Cap the sleep so a changed interval or a stop request is noticed promptly
            // even when the next frame is far off.
            constexpr uint64_t kMaxSleepNs = 100000000ull;  // 100ms
            if (delay > kMaxSleepNs) delay = kMaxSleepNs;
            state().cv.wait_for(lock, std::chrono::nanoseconds(delay));
        }
    }
}

void start_pacer_once() {
    std::lock_guard<std::mutex> lock(state().mtx);
    if (state().started) return;
    state().started = true;
    state().stop = false;
    state().running.store(true, std::memory_order_release);
    std::thread(pacer_main).detach();
}

void wake_pacer() { state().cv.notify_all(); }

void post_frame_callback(Choreographer* c, void* callback, void* data, uint64_t delay_ns) {
    if (callback == nullptr) return;
    start_vsync_query();
    {
        std::lock_guard<std::mutex> lock(c->mtx);
        ensure_pipe_locked(c);
        register_pipe_locked(c);
        FrameCallbackEntry e;
        e.fn = callback;
        e.data = data;
        // Aligned to the next frame boundary rather than fired the instant it is asked
        // for. A callback that runs immediately turns a guest's "render on the next
        // frame" into a busy loop, because the guest re-posts from inside it.
        const uint64_t now = mono_ns();
        e.due_ns = now + delay_ns + static_cast<uint64_t>(interval_ns());
        e.nudged = false;
        c->frame_callbacks.push_back(e);
        c->pending.store(static_cast<unsigned>(c->frame_callbacks.size()),
                         std::memory_order_release);
    }
    start_pacer_once();
    wake_pacer();
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Bridge between the NDK entry points (global scope, unmangled) and the state
// above. Non-static so the entry points can reach it; nothing else calls it.
// ─────────────────────────────────────────────────────────────────────────────

void* frame_pacer_instance_for_this_thread() {
    start_vsync_query();
    return acquire_instance();
}

void frame_pacer_post(void* instance, void* callback, void* data, uint64_t delay_ns) {
    auto* c = instance != nullptr ? static_cast<Choreographer*>(instance) : acquire_instance();
    post_frame_callback(c, callback, data, delay_ns);
}

void frame_pacer_add_refresh_callback(void* instance, void* callback, void* data) {
    if (callback == nullptr) return;
    auto* c = instance != nullptr ? static_cast<Choreographer*>(instance) : acquire_instance();
    {
        std::lock_guard<std::mutex> lock(c->mtx);
        RefreshCallbackEntry e;
        e.fn = callback;
        e.data = data;
        c->refresh_callbacks.push_back(e);
    }
    // Android calls back immediately with the current period, then again on change.
    // The immediate call is the one that matters here: a frame pacer sizes its budget
    // from it, and on a display whose rate never changes there would otherwise be no
    // call at all — leaving the pacer with no period and nothing to pace against.
    const int64_t period = vsync_period_ns();
    reinterpret_cast<void (*)(int64_t, void*)>(callback)(period, data);
}

void frame_pacer_remove_refresh_callback(void* instance, void* callback, void* data) {
    auto* c = instance != nullptr ? static_cast<Choreographer*>(instance) : acquire_instance();
    std::lock_guard<std::mutex> lock(c->mtx);
    for (size_t i = 0; i < c->refresh_callbacks.size(); ++i) {
        // Android matches on the (callback, data) pair, so the same function registered
        // with two different contexts unregisters independently.
        if (c->refresh_callbacks[i].fn == callback && c->refresh_callbacks[i].data == data) {
            c->refresh_callbacks.erase(c->refresh_callbacks.begin() + static_cast<long>(i));
            return;
        }
    }
}

void frame_pacer_request_rate(float frame_rate) {
    if (!(frame_rate > 0.0f)) {
        // 0 means "no preference" on Android, and so does anything non-finite or
        // negative. Clearing the hint restores the display's own rate.
        g_requested_period_ns.store(0, std::memory_order_relaxed);
        paceLog("setFrameRate: no preference; frames follow the display");
        wake_pacer();
        return;
    }
    const int64_t period = static_cast<int64_t>(1000000000.0f / frame_rate);
    g_requested_period_ns.store(period, std::memory_order_relaxed);
    paceLog("setFrameRate: %.2f fps requested (period %lld ns); effective %lld ns",
            static_cast<double>(frame_rate), static_cast<long long>(period),
            static_cast<long long>(interval_ns()));
    wake_pacer();
}

// ─────────────────────────────────────────────────────────────────────────────
// Public queries
// ─────────────────────────────────────────────────────────────────────────────

int64_t display_vsync_period_ns() { return vsync_period_ns(); }

int64_t frame_pacer_interval_ns() { return interval_ns(); }

int frame_pacer_pending_count() {
    int total = 0;
    for (int i = 0; i < kMaxInstances; ++i) {
        total += static_cast<int>(state().instances[i].pending.load(std::memory_order_acquire));
    }
    total += static_cast<int>(state().shared.pending.load(std::memory_order_acquire));
    return total;
}

int frame_pacer_dispatch_due() {
    Choreographer* c = t_instance;
    if (c == nullptr) return 0;
    const int ran = dispatch_instance(c);
    if (ran > 0) {
        g_looper_deliveries.fetch_add(static_cast<uint64_t>(ran), std::memory_order_relaxed);
    }
    return ran;
}

void frame_pacer_reset_for_test() {
    {
        std::lock_guard<std::mutex> lock(state().mtx);
        state().stop = true;
    }
    state().cv.notify_all();
    // Wait for the thread to actually be gone before touching the pipes below: it is
    // detached, so there is no join, and closing a descriptor it is still reading would
    // be a use-after-close inside the pacer.
    for (int i = 0; i < 2000 && state().running.load(std::memory_order_acquire); ++i) {
        state().cv.notify_all();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    {
        std::lock_guard<std::mutex> lock(state().mtx);
        state().started = false;
        state().stop = false;
    }

    for (int i = 0; i <= kMaxInstances; ++i) {
        Choreographer* c = (i == kMaxInstances) ? &state().shared : &state().instances[i];
        std::lock_guard<std::mutex> lock(c->mtx);
        c->frame_callbacks.clear();
        c->refresh_callbacks.clear();
        c->pending.store(0, std::memory_order_release);
        if (c->wake_pipe[0] >= 0) {
            if (c->pipe_registered) {
                if (void* looper = bionic_ALooper_forThread()) {
                    bionic_ALooper_removeFd(looper, c->wake_pipe[0]);
                }
            }
            ::close(c->wake_pipe[0]);
            ::close(c->wake_pipe[1]);
            c->wake_pipe[0] = -1;
            c->wake_pipe[1] = -1;
        }
        c->pipe_registered = false;
        c->owner_thread.store(0, std::memory_order_relaxed);
        c->claimed.store(false, std::memory_order_release);
    }
    t_instance = nullptr;
    g_requested_period_ns.store(0, std::memory_order_relaxed);
    g_looper_deliveries.store(0, std::memory_order_relaxed);
    g_direct_deliveries.store(0, std::memory_order_relaxed);
    g_direct_mode.store(false, std::memory_order_relaxed);
}

}  // namespace kudroid

// ─────────────────────────────────────────────────────────────────────────────
// NDK entry points
// ─────────────────────────────────────────────────────────────────────────────

extern "C" void* bionic_AChoreographer_getInstance(void) {
    // Never NULL. On Android this returns NULL only when the calling thread has no
    // Looper; KuDroid's ALooper_forThread creates one on demand, so the precondition
    // always holds. Returning NULL — which the universal dummy did, by returning 0 —
    // reads to a guest as "this thread cannot receive frames", and Unity's pacer thread
    // answered that by exiting immediately, leaving its render thread parked on a
    // condition variable for the rest of the session.
    return kudroid::frame_pacer_instance_for_this_thread();
}

extern "C" void bionic_AChoreographer_postFrameCallback(void* choreographer, void* callback,
                                                        void* data) {
    kudroid::frame_pacer_post(choreographer, callback, data, 0);
}

extern "C" void bionic_AChoreographer_postFrameCallbackDelayed(void* choreographer,
                                                               void* callback, void* data,
                                                               long delayMillis) {
    const uint64_t delay =
        delayMillis > 0 ? static_cast<uint64_t>(delayMillis) * 1000000ull : 0;
    kudroid::frame_pacer_post(choreographer, callback, data, delay);
}

extern "C" void bionic_AChoreographer_postFrameCallback64(void* choreographer, void* callback,
                                                          void* data) {
    kudroid::frame_pacer_post(choreographer, callback, data, 0);
}

extern "C" void bionic_AChoreographer_postFrameCallbackDelayed64(void* choreographer,
                                                                 void* callback, void* data,
                                                                 uint32_t delayMillis) {
    kudroid::frame_pacer_post(choreographer, callback, data,
                              static_cast<uint64_t>(delayMillis) * 1000000ull);
}

extern "C" void bionic_AChoreographer_registerRefreshRateCallback(void* choreographer,
                                                                  void* callback, void* data) {
    kudroid::frame_pacer_add_refresh_callback(choreographer, callback, data);
}

extern "C" void bionic_AChoreographer_unregisterRefreshRateCallback(void* choreographer,
                                                                    void* callback,
                                                                    void* data) {
    kudroid::frame_pacer_remove_refresh_callback(choreographer, callback, data);
}

extern "C" int32_t bionic_ANativeWindow_setFrameRate(void* window, float frameRate,
                                                     int8_t compatibility) {
    (void)window;
    (void)compatibility;
    kudroid::frame_pacer_request_rate(frameRate);
    return 0;  // Android returns 0 on success
}

extern "C" int32_t bionic_ANativeWindow_setFrameRateWithChangeStrategy(
    void* window, float frameRate, int8_t compatibility, int8_t changeFrameRateStrategy) {
    (void)changeFrameRateStrategy;
    return bionic_ANativeWindow_setFrameRate(window, frameRate, compatibility);
}
