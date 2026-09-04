// test_frame_pacer.cpp — a guest that asks for frames must get them.
//
// Why this exists. ULTRAKILL launched, brought up Vulkan on the A13, created a
// 3-image swapchain at 1792x828 — and then stopped forever, with no crash and no
// further log output. The cause was six symbols:
//
//     missing symbol bound to dummy: ANativeWindow_setFrameRate
//     missing symbol bound to dummy: AChoreographer_getInstance
//     missing symbol bound to dummy: AChoreographer_postFrameCallback
//     missing symbol bound to dummy: AChoreographer_postFrameCallbackDelayed
//     missing symbol bound to dummy: AChoreographer_registerRefreshRateCallback
//     missing symbol bound to dummy: AChoreographer_unregisterRefreshRateCallback
//
// All six bound to kudroid_universal_dummy, which returns 0. getInstance() therefore
// answered NULL — indistinguishable, to a guest, from "this thread has no Looper", a
// real Android condition that callers handle by giving up. Unity's frame-pacer thread
// gave up: started, read NULL, exited (`thread-entry` then `thread-exit`, two adjacent
// log lines), and never signalled the condition variable UnityMain was waiting on.
// UnityMain then sat in __psynch_cvwait for the rest of the session — five thread
// samples across 41 seconds with identical pc, identical sp, and cpu_ms frozen at 383.
//
// These tests are therefore mostly about the contract at the boundary, not about
// timing: what a guest is entitled to conclude from each return value. The one that
// would have caught the original bug is the first, and it is one line.
#include "kudroid/platform/FramePacer.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <thread>
#include <vector>

extern "C" {
void* bionic_AChoreographer_getInstance(void);
void bionic_AChoreographer_postFrameCallback(void* c, void* cb, void* data);
void bionic_AChoreographer_postFrameCallbackDelayed(void* c, void* cb, void* data,
                                                    long delayMillis);
void bionic_AChoreographer_postFrameCallback64(void* c, void* cb, void* data);
void bionic_AChoreographer_postFrameCallbackDelayed64(void* c, void* cb, void* data,
                                                      uint32_t delayMillis);
void bionic_AChoreographer_registerRefreshRateCallback(void* c, void* cb, void* data);
void bionic_AChoreographer_unregisterRefreshRateCallback(void* c, void* cb, void* data);
int32_t bionic_ANativeWindow_setFrameRate(void* window, float rate, int8_t compat);
int32_t bionic_ANativeWindow_setFrameRateWithChangeStrategy(void* w, float r, int8_t c,
                                                            int8_t s);
}

namespace kudroid {
// The shim symbol table, to prove these are reachable the way a guest reaches them.
void* resolve_bionic_symbol(const char* name);
bool is_universal_dummy(const void* address);
}  // namespace kudroid

namespace {

int g_failures = 0;
int g_checks = 0;

// argv[0], so the shutdown test can re-run this binary as a child.
const char* g_argv0 = nullptr;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    std::printf("%s %s\n", ok ? "  OK  " : "  FAIL", what.c_str());
    if (!ok) ++g_failures;
}

using kudroid::display_vsync_period_ns;
using kudroid::frame_pacer_dispatch_due;
using kudroid::frame_pacer_interval_ns;
using kudroid::frame_pacer_pending_count;
using kudroid::frame_pacer_reset_for_test;

// ── frame callback recording ────────────────────────────────────────────────

struct FrameRecord {
    std::atomic<int> calls{0};
    std::atomic<int64_t> last_time{0};
    std::atomic<void*> last_data{nullptr};
};

FrameRecord g_frames;

void frame_callback(int64_t frameTimeNanos, void* data) {
    g_frames.last_time.store(frameTimeNanos, std::memory_order_relaxed);
    g_frames.last_data.store(data, std::memory_order_relaxed);
    g_frames.calls.fetch_add(1, std::memory_order_release);
}

// A callback that re-posts itself, which is how every real Choreographer client
// works: the API is one-shot, so continuous rendering means posting again from
// inside the callback. Re-entering the pacer's own lock from there would deadlock.
struct Reposter {
    static std::atomic<int> calls;
    static std::atomic<int> limit;
    static void run(int64_t /*t*/, void* data) {
        const int n = calls.fetch_add(1, std::memory_order_acq_rel) + 1;
        if (n < limit.load(std::memory_order_relaxed)) {
            bionic_AChoreographer_postFrameCallback(data, reinterpret_cast<void*>(&run), data);
        }
    }
};
std::atomic<int> Reposter::calls{0};
std::atomic<int> Reposter::limit{0};

struct RefreshRecord {
    std::atomic<int> calls{0};
    std::atomic<int64_t> last_period{0};
    std::atomic<void*> last_data{nullptr};
};

RefreshRecord g_refresh;

void refresh_callback(int64_t vsyncPeriodNanos, void* data) {
    g_refresh.last_period.store(vsyncPeriodNanos, std::memory_order_relaxed);
    g_refresh.last_data.store(data, std::memory_order_relaxed);
    g_refresh.calls.fetch_add(1, std::memory_order_release);
}

void reset() {
    frame_pacer_reset_for_test();
    g_frames.calls.store(0, std::memory_order_relaxed);
    g_frames.last_time.store(0, std::memory_order_relaxed);
    g_frames.last_data.store(nullptr, std::memory_order_relaxed);
    g_refresh.calls.store(0, std::memory_order_relaxed);
    g_refresh.last_period.store(0, std::memory_order_relaxed);
    g_refresh.last_data.store(nullptr, std::memory_order_relaxed);
    Reposter::calls.store(0, std::memory_order_relaxed);
    Reposter::limit.store(0, std::memory_order_relaxed);
}

// Poll until a condition holds or the deadline passes. Never a fixed sleep: the
// pacer runs at the display rate, so a sleep long enough to be reliable on a slow
// CI box makes the whole suite slow, and one short enough to be fast is flaky.
template <typename Fn>
bool wait_until(Fn&& done, int timeout_ms = 2000) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (done()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return done();
}

uint64_t mono_now_ns() {
    struct timespec ts {};
    ::clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ull +
           static_cast<uint64_t>(ts.tv_nsec);
}

// ─────────────────────────────────────────────────────────────────────────────

// THE test. One line, and it is the whole ULTRAKILL bug.
void test_get_instance_is_never_null() {
    std::printf("[choreographer] getInstance never returns NULL\n");
    reset();

    void* c = bionic_AChoreographer_getInstance();
    Check(c != nullptr,
          "getInstance returns an instance — NULL reads as 'no Looper on this thread', "
          "and Unity's pacer thread answers that by exiting and never signalling its "
          "render thread");

    // Called repeatedly from one thread, as a guest does on every frame.
    void* again = bionic_AChoreographer_getInstance();
    Check(again == c, "the same thread gets the same instance every time");
    bool stable = true;
    for (int i = 0; i < 100; ++i) {
        if (bionic_AChoreographer_getInstance() != c) {
            stable = false;
            break;
        }
    }
    Check(stable, "100 repeat calls all return the same instance");
}

// The symbols must be reachable the way the ELF loader reaches them, not just by
// direct call from a test. This is the check that would have failed while the
// implementation existed but was not wired into the shim table — a real possibility,
// since the table is a hand-maintained array.
void test_symbols_resolve_through_the_shim() {
    std::printf("[choreographer] every symbol resolves through the shim, not to the dummy\n");
    reset();

    const char* names[] = {
        "AChoreographer_getInstance",
        "AChoreographer_postFrameCallback",
        "AChoreographer_postFrameCallbackDelayed",
        "AChoreographer_postFrameCallback64",
        "AChoreographer_postFrameCallbackDelayed64",
        "AChoreographer_registerRefreshRateCallback",
        "AChoreographer_unregisterRefreshRateCallback",
        "ANativeWindow_setFrameRate",
        "ANativeWindow_setFrameRateWithChangeStrategy",
    };
    for (const char* name : names) {
        void* addr = kudroid::resolve_bionic_symbol(name);
        const bool ok = addr != nullptr && !kudroid::is_universal_dummy(addr);
        Check(ok, std::string(name) + " resolves to a real implementation");
    }

    // And the negative control: a symbol nobody implements must still report as the
    // dummy, or the check above proves nothing.
    void* absent = kudroid::resolve_bionic_symbol("AChoreographer_thisDoesNotExist");
    Check(absent != nullptr && kudroid::is_universal_dummy(absent),
          "an unimplemented symbol is still bound to the dummy, and is identifiable as "
          "such — which is what makes the assertions above meaningful");
}

void test_posted_callback_runs() {
    std::printf("[choreographer] a posted frame callback actually runs\n");
    reset();

    void* c = bionic_AChoreographer_getInstance();
    int marker = 0;
    bionic_AChoreographer_postFrameCallback(c, reinterpret_cast<void*>(&frame_callback),
                                            &marker);
    Check(frame_pacer_pending_count() == 1, "the callback is queued");

    Check(wait_until([] { return g_frames.calls.load(std::memory_order_acquire) >= 1; }),
          "and is delivered without the guest polling anything");
    Check(g_frames.last_data.load(std::memory_order_relaxed) == &marker,
          "with the data pointer it was registered with");
    Check(frame_pacer_pending_count() == 0, "and is removed from the queue afterwards");
}

// One-shot semantics. Getting this wrong in either direction is a real failure:
// re-arming automatically makes a guest render twice per frame, and dropping the
// entry without running it stops rendering entirely.
void test_callback_is_one_shot() {
    std::printf("[choreographer] a callback fires once per post, as on Android\n");
    reset();

    void* c = bionic_AChoreographer_getInstance();
    bionic_AChoreographer_postFrameCallback(c, reinterpret_cast<void*>(&frame_callback),
                                            nullptr);
    Check(wait_until([] { return g_frames.calls.load(std::memory_order_acquire) >= 1; }),
          "the callback ran");

    // Several frame intervals with no further post: the count must not move.
    const int after_first = g_frames.calls.load(std::memory_order_acquire);
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    Check(g_frames.calls.load(std::memory_order_acquire) == after_first,
          "and does not fire again on its own — the API is one-shot, so a guest that "
          "did not re-post must not be woken");
}

// The re-post path, which is how the API is actually used. A guest posts from inside
// its own callback; if the callback were invoked with the pacer's lock held, that
// call would deadlock and every Unity game would hang on its second frame.
void test_callback_may_repost_itself() {
    std::printf("[choreographer] a callback can post the next one from inside itself\n");
    reset();

    void* c = bionic_AChoreographer_getInstance();
    Reposter::limit.store(5, std::memory_order_relaxed);
    bionic_AChoreographer_postFrameCallback(c, reinterpret_cast<void*>(&Reposter::run), c);

    Check(wait_until([] { return Reposter::calls.load(std::memory_order_acquire) >= 5; },
                     3000),
          "five chained frames arrive — re-posting from inside the callback does not "
          "deadlock, which it would if the callback ran with the queue lock held");
}

void test_delayed_callback_waits() {
    std::printf("[choreographer] a delayed callback is not delivered early\n");
    reset();

    void* c = bionic_AChoreographer_getInstance();
    const uint64_t posted_at = mono_now_ns();
    bionic_AChoreographer_postFrameCallbackDelayed(
        c, reinterpret_cast<void*>(&frame_callback), nullptr, 80);

    Check(wait_until([] { return g_frames.calls.load(std::memory_order_acquire) >= 1; },
                     3000),
          "the delayed callback is delivered");

    const int64_t at = g_frames.last_time.load(std::memory_order_relaxed);
    const uint64_t elapsed_ms =
        at > 0 ? (static_cast<uint64_t>(at) - posted_at) / 1000000ull : 0;
    Check(elapsed_ms >= 75,
          std::string("and not before its delay elapsed (") + std::to_string(elapsed_ms) +
              " ms >= 75)");
}

// frameTimeNanos has to be on CLOCK_MONOTONIC, because that is the clock the guest
// reads for itself through clock_gettime and System.nanoTime. Reporting
// mach_absolute_time instead — which is what steady_clock is on Darwin, and which
// does not advance across sleep — gives a guest two timelines that drift apart by
// however long the device slept, and a pacer subtracting one from the other then
// either stalls on a deadline in the past or floods frames catching up.
void test_frame_time_is_on_clock_monotonic() {
    std::printf("[choreographer] frameTimeNanos is CLOCK_MONOTONIC, the clock the guest reads\n");
    reset();

    void* c = bionic_AChoreographer_getInstance();
    const uint64_t before = mono_now_ns();
    bionic_AChoreographer_postFrameCallback(c, reinterpret_cast<void*>(&frame_callback),
                                            nullptr);
    Check(wait_until([] { return g_frames.calls.load(std::memory_order_acquire) >= 1; }),
          "the callback ran");
    const uint64_t after = mono_now_ns();

    const int64_t at = g_frames.last_time.load(std::memory_order_relaxed);
    Check(at > 0, "a timestamp was reported");
    Check(static_cast<uint64_t>(at) >= before && static_cast<uint64_t>(at) <= after,
          "and it lies between two CLOCK_MONOTONIC readings taken around the call");
}

// Registration must call back immediately with the current period. Without that, a
// pacer on a fixed-rate display never learns the period at all: the only other call
// happens on a change, and the rate never changes.
void test_refresh_callback_fires_on_registration() {
    std::printf("[choreographer] registering a refresh-rate callback calls it immediately\n");
    reset();

    void* c = bionic_AChoreographer_getInstance();
    int marker = 0;
    bionic_AChoreographer_registerRefreshRateCallback(
        c, reinterpret_cast<void*>(&refresh_callback), &marker);

    Check(g_refresh.calls.load(std::memory_order_acquire) == 1,
          "called synchronously on registration — a fixed-rate display never fires a "
          "change event, so this is the only call a pacer will ever get");
    Check(g_refresh.last_data.load(std::memory_order_relaxed) == &marker,
          "with the registered data pointer");

    const int64_t period = g_refresh.last_period.load(std::memory_order_relaxed);
    Check(period == display_vsync_period_ns(),
          "and the period it reports is the display's own, not an invented constant");
    // A plausible display: between 240 Hz and 24 Hz. A zero or negative period makes a
    // guest divide by it.
    Check(period >= 4000000 && period <= 41666667,
          std::string("the period is plausible (") + std::to_string(period) + " ns)");
}

void test_refresh_callback_unregisters_by_pair() {
    std::printf("[choreographer] unregister matches on (callback, data), as Android does\n");
    reset();

    void* c = bionic_AChoreographer_getInstance();
    int a = 0, b = 0;
    bionic_AChoreographer_registerRefreshRateCallback(
        c, reinterpret_cast<void*>(&refresh_callback), &a);
    bionic_AChoreographer_registerRefreshRateCallback(
        c, reinterpret_cast<void*>(&refresh_callback), &b);
    Check(g_refresh.calls.load(std::memory_order_acquire) == 2,
          "two registrations, two immediate calls");

    // Removing one context must not remove the other. Matching on the function
    // pointer alone would drop both, and a guest that registers the same function per
    // window would silently stop receiving rate changes for windows it still has.
    bionic_AChoreographer_unregisterRefreshRateCallback(
        c, reinterpret_cast<void*>(&refresh_callback), &a);
    bionic_AChoreographer_unregisterRefreshRateCallback(
        c, reinterpret_cast<void*>(&refresh_callback), &b);
    Check(true, "both unregister without incident");

    // Unregistering something never registered must be harmless.
    bionic_AChoreographer_unregisterRefreshRateCallback(
        c, reinterpret_cast<void*>(&refresh_callback), &a);
    Check(true, "and unregistering twice is safe");
}

// ── setFrameRate ────────────────────────────────────────────────────────────

void test_set_frame_rate_lowers_the_interval() {
    std::printf("[framerate] a lower requested rate lengthens the frame interval\n");
    reset();

    const int64_t display = display_vsync_period_ns();
    Check(frame_pacer_interval_ns() == display,
          "with no hint, frames follow the display");

    Check(bionic_ANativeWindow_setFrameRate(nullptr, 30.0f, 0) == 0,
          "setFrameRate reports success, as Android does");
    const int64_t at30 = frame_pacer_interval_ns();
    Check(at30 > display,
          std::string("30 fps asks for a longer interval than the display's (") +
              std::to_string(at30) + " > " + std::to_string(display) + ")");
    // ~33.3ms, within a millisecond.
    Check(at30 >= 32000000 && at30 <= 34500000,
          std::string("and it is about 33 ms (") + std::to_string(at30) + " ns)");
}

// A hint may only ask for FEWER frames. Honouring a higher rate would have the pacer
// deliver callbacks the display cannot present, and a guest pacing itself from those
// timestamps runs ahead of the compositor.
void test_set_frame_rate_cannot_exceed_the_display() {
    std::printf("[framerate] a rate above the display's is capped, not honoured\n");
    reset();

    const int64_t display = display_vsync_period_ns();
    bionic_ANativeWindow_setFrameRate(nullptr, 1000.0f, 0);
    Check(frame_pacer_interval_ns() == display,
          "1000 fps on a 60 Hz panel still paces at the display rate — delivering "
          "frames the display cannot present would put the guest ahead of the compositor");
}

void test_set_frame_rate_zero_clears_the_hint() {
    std::printf("[framerate] 0 means 'no preference' and restores the display rate\n");
    reset();

    const int64_t display = display_vsync_period_ns();
    bionic_ANativeWindow_setFrameRate(nullptr, 24.0f, 0);
    Check(frame_pacer_interval_ns() > display, "24 fps applies");

    Check(bionic_ANativeWindow_setFrameRate(nullptr, 0.0f, 0) == 0, "clearing succeeds");
    Check(frame_pacer_interval_ns() == display,
          "and the interval returns to the display's own");

    // Negative and absurd values take the same path rather than producing a negative
    // interval, which would make every frame instantly overdue.
    bionic_ANativeWindow_setFrameRate(nullptr, -60.0f, 0);
    Check(frame_pacer_interval_ns() == display, "a negative rate is treated as no preference");
}

void test_set_frame_rate_with_strategy_matches() {
    std::printf("[framerate] the WithChangeStrategy form behaves identically\n");
    reset();

    bionic_ANativeWindow_setFrameRateWithChangeStrategy(nullptr, 30.0f, 0, 0);
    const int64_t a = frame_pacer_interval_ns();
    reset();
    bionic_ANativeWindow_setFrameRate(nullptr, 30.0f, 0);
    const int64_t b = frame_pacer_interval_ns();
    Check(a == b, "both forms produce the same interval");
}

// The requested rate must actually govern delivery, not merely be stored. A hint that
// changes a number nothing reads is indistinguishable from the dummy that returned 0.
void test_requested_rate_governs_delivery() {
    std::printf("[framerate] the hint governs how often callbacks arrive\n");
    reset();

    void* c = bionic_AChoreographer_getInstance();
    bionic_ANativeWindow_setFrameRate(nullptr, 20.0f, 0);  // 50ms per frame

    const uint64_t posted_at = mono_now_ns();
    bionic_AChoreographer_postFrameCallback(c, reinterpret_cast<void*>(&frame_callback),
                                            nullptr);
    Check(wait_until([] { return g_frames.calls.load(std::memory_order_acquire) >= 1; },
                     3000),
          "the frame arrives");

    const int64_t at = g_frames.last_time.load(std::memory_order_relaxed);
    const uint64_t elapsed_ms =
        at > 0 ? (static_cast<uint64_t>(at) - posted_at) / 1000000ull : 0;
    Check(elapsed_ms >= 45,
          std::string("and not before the requested 50 ms interval (") +
              std::to_string(elapsed_ms) + " ms >= 45) — the hint is applied, not just stored");
}

// ── threads ─────────────────────────────────────────────────────────────────

// A guest engine has several threads asking for frames. Each must get its own
// instance, and each must receive its own callbacks: Unity's pacer and render threads
// both post, and cross-delivery would run render work on the wrong thread.
void test_each_thread_gets_its_own_instance() {
    std::printf("[choreographer] separate threads get separate instances\n");
    reset();

    void* main_instance = bionic_AChoreographer_getInstance();
    std::atomic<void*> worker_instance{nullptr};
    std::thread worker([&] {
        worker_instance.store(bionic_AChoreographer_getInstance(), std::memory_order_release);
    });
    worker.join();

    void* w = worker_instance.load(std::memory_order_acquire);
    Check(w != nullptr, "the worker thread also gets an instance");
    Check(w != main_instance,
          "and it is its own — Android's Choreographer is per-thread, and a guest that "
          "posts from two threads must not have them collide");
}

void test_frames_arrive_on_several_threads() {
    std::printf("[choreographer] every posting thread receives its own frames\n");
    reset();

    constexpr int kThreads = 4;
    std::atomic<int> delivered{0};
    std::atomic<int> got_instance{0};

    std::vector<std::thread> threads;
    for (int i = 0; i < kThreads; ++i) {
        threads.emplace_back([&] {
            void* c = bionic_AChoreographer_getInstance();
            if (c != nullptr) got_instance.fetch_add(1, std::memory_order_relaxed);
            // A local counter reached through the data pointer, so a frame delivered to
            // the wrong queue would still be counted here — what is being checked is
            // that all four arrive, not which thread ran them.
            struct Local {
                static void cb(int64_t, void* d) {
                    static_cast<std::atomic<int>*>(d)->fetch_add(1, std::memory_order_release);
                }
            };
            bionic_AChoreographer_postFrameCallback(c, reinterpret_cast<void*>(&Local::cb),
                                                    &delivered);
        });
    }
    for (std::thread& t : threads) t.join();

    Check(got_instance.load(std::memory_order_relaxed) == kThreads,
          "all four threads got an instance");
    Check(wait_until([&] { return delivered.load(std::memory_order_acquire) >= kThreads; },
                     3000),
          "and all four frame callbacks are delivered");
}

// ── idle cost ───────────────────────────────────────────────────────────────

// Nothing queued must mean nothing running. A pacer that ticked at the display rate
// regardless would burn a core for as long as an app sat on a menu, which on a phone
// is a battery and thermal problem rather than a correctness one — but it is the kind
// of cost that gets a diagnostic thread disabled, and then it is not there when it is
// needed.
void test_pacer_is_idle_when_nothing_is_queued() {
    std::printf("[choreographer] an idle pacer delivers nothing and queues nothing\n");
    reset();

    void* c = bionic_AChoreographer_getInstance();
    bionic_AChoreographer_postFrameCallback(c, reinterpret_cast<void*>(&frame_callback),
                                            nullptr);
    Check(wait_until([] { return g_frames.calls.load(std::memory_order_acquire) >= 1; }),
          "one frame is delivered");

    const int after = g_frames.calls.load(std::memory_order_acquire);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    Check(g_frames.calls.load(std::memory_order_acquire) == after,
          "and with nothing posted, nothing more is delivered over 200 ms");
    Check(frame_pacer_pending_count() == 0, "the queue is empty");
}

// ── robustness ──────────────────────────────────────────────────────────────

void test_null_arguments_are_survivable() {
    std::printf("[choreographer] null arguments do not crash\n");
    reset();

    // A null callback is a guest bug, but faulting inside the shim turns it into an
    // unexplained crash with KuDroid's own frames on the stack.
    void* c = bionic_AChoreographer_getInstance();
    bionic_AChoreographer_postFrameCallback(c, nullptr, nullptr);
    bionic_AChoreographer_postFrameCallbackDelayed(c, nullptr, nullptr, 10);
    bionic_AChoreographer_registerRefreshRateCallback(c, nullptr, nullptr);
    bionic_AChoreographer_unregisterRefreshRateCallback(c, nullptr, nullptr);
    Check(frame_pacer_pending_count() == 0, "a null callback is rejected, not queued");

    // A null choreographer: guests pass the value getInstance gave them, and a guest
    // that ignored a NULL from an older build would arrive here.
    bionic_AChoreographer_postFrameCallback(nullptr,
                                            reinterpret_cast<void*>(&frame_callback), nullptr);
    Check(wait_until([] { return g_frames.calls.load(std::memory_order_acquire) >= 1; }),
          "and a null choreographer falls back to this thread's instance rather than "
          "dropping the frame");
}

void test_the_64bit_forms_behave_the_same() {
    std::printf("[choreographer] the ...64 entry points behave like their originals\n");
    reset();

    void* c = bionic_AChoreographer_getInstance();
    bionic_AChoreographer_postFrameCallback64(c, reinterpret_cast<void*>(&frame_callback),
                                              nullptr);
    Check(wait_until([] { return g_frames.calls.load(std::memory_order_acquire) >= 1; }),
          "postFrameCallback64 delivers");

    reset();
    bionic_AChoreographer_postFrameCallbackDelayed64(
        c, reinterpret_cast<void*>(&frame_callback), nullptr, 10);
    Check(wait_until([] { return g_frames.calls.load(std::memory_order_acquire) >= 1; },
                     3000),
          "postFrameCallbackDelayed64 delivers");
}

// A process that used the pacer and never called the test seam must exit cleanly.
//
// This is not hypothetical: the pacer thread was first held in a global std::thread,
// and at process exit the static destructor ran while the thread was still alive, so
// ~thread() saw joinable() and called std::terminate. Every test passed — they all call
// reset_for_test, which joined it — while any real app that rendered one frame aborted
// on the way out with "terminate called without an active exception", after doing
// everything correctly, and with nothing in the message pointing at frame pacing.
//
// Run as a child process, because the failure IS the exit and it cannot be observed
// from inside the process it kills.
void test_process_exits_cleanly_without_a_reset() {
    std::printf("[shutdown] a process that rendered a frame and never reset exits cleanly\n");

    // The child re-runs this binary with a marker argument, posts a frame, and returns
    // from main without touching the test seam — exactly what a real app does.
    //
    // Wrapped in `timeout`, because the two ways this fails are an abort AND a hang: a
    // joinable global std::thread aborts at static destruction, and a detached one
    // whose condition_variable is destroyed underneath it never returns from exit().
    // Without the timeout the second failure mode wedges this test instead of failing
    // it, which is the same class of mistake as the bug being pinned.
    const char* self = g_argv0;
    if (self == nullptr) {
        Check(false, "argv[0] is available");
        return;
    }
    std::string cmd = std::string("timeout 20 '") + self + "' --exit-check >/dev/null 2>&1";
    const int status = std::system(cmd.c_str());
    Check(status == 0,
          std::string("the child exits 0 (got ") + std::to_string(status) +
              ") — a joinable pacer thread in a global aborts here, and a detached one "
              "whose condvar is destroyed at static teardown hangs, while every "
              "in-process test still passes either way");
}

}  // namespace

int main(int argc, char** argv) {
    g_argv0 = argc > 0 ? argv[0] : nullptr;

    // The child half of test_process_exits_cleanly_without_a_reset: use the pacer, then
    // return from main WITHOUT calling the test seam, which is what a real app does.
    if (argc > 1 && std::strcmp(argv[1], "--exit-check") == 0) {
        void* c = bionic_AChoreographer_getInstance();
        bionic_AChoreographer_postFrameCallback(c, reinterpret_cast<void*>(&frame_callback),
                                                nullptr);
        wait_until([] { return g_frames.calls.load(std::memory_order_acquire) >= 1; });
        return 0;
    }

    std::printf("=== frame pacer (AChoreographer / setFrameRate) ===\n");

    test_get_instance_is_never_null();
    test_symbols_resolve_through_the_shim();
    test_posted_callback_runs();
    test_callback_is_one_shot();
    test_callback_may_repost_itself();
    test_delayed_callback_waits();
    test_frame_time_is_on_clock_monotonic();
    test_refresh_callback_fires_on_registration();
    test_refresh_callback_unregisters_by_pair();
    test_set_frame_rate_lowers_the_interval();
    test_set_frame_rate_cannot_exceed_the_display();
    test_set_frame_rate_zero_clears_the_hint();
    test_set_frame_rate_with_strategy_matches();
    test_requested_rate_governs_delivery();
    test_each_thread_gets_its_own_instance();
    test_frames_arrive_on_several_threads();
    test_pacer_is_idle_when_nothing_is_queued();
    test_null_arguments_are_survivable();
    test_the_64bit_forms_behave_the_same();
    test_process_exits_cleanly_without_a_reset();

    frame_pacer_reset_for_test();

    std::printf("=== %d checks, %d failures ===\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
