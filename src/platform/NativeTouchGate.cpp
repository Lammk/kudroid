#include "kudroid/platform/NativeTouchGate.h"

#include <atomic>
#include <chrono>

// A drag arrives at 60-120 Hz. The dispatch used to cost one VM-locked
// interpreted hop per sample from a worker thread; it now runs on the Looper
// thread (kuart_touch_drain_pending) and the queue plus ActivityThread both fold
// a pending MOVE, so the rate is bounded where it matters. This gate is now only
// a floor against a flood, not a rate cap: at 30 Hz it was silently limiting the
// app's own input, which is felt as a drag that trails the finger.
//
// [KuDroidTouch] drain timing is the check that the dispatch is still cheap: a
// line there means this constant is the smaller problem.
//
// The budget is a pair of relaxed atomics, not a mutex: this runs on every MOVE
// of every finger on the UI thread, and contending the mutex there steals frame
// budget from the same loop that is supposed to drain the queue.

namespace {

std::atomic<int> g_used{0};
std::atomic<long long> g_windowStart{0};

// 2 moves per 8 ms is ~250/s: every sample of a 120 Hz drag passes through, and
// a flood still cannot buy more than that many dispatches.
constexpr int kMaxMovesPerWindow = 2;
constexpr long long kWindowNs = 8000000;  // 8 ms

long long steady_now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

}  // namespace

extern "C" void kudroid_touch_source_gate_init(void) {
    g_used.store(0, std::memory_order_relaxed);
    g_windowStart.store(steady_now_ns(), std::memory_order_relaxed);
}

extern "C" int kudroid_touch_source_gate_allow_move(void) {
    const long long now = steady_now_ns();
    long long win = g_windowStart.load(std::memory_order_relaxed);
    if (now - win >= kWindowNs) {
        // CAS winner resets the window; losers keep their budget decision in
        // the old window, which is fine — the floor exists for floods.
        if (g_windowStart.compare_exchange_strong(win, now, std::memory_order_relaxed)) {
            g_used.store(0, std::memory_order_relaxed);
        }
    }
    int used = g_used.load(std::memory_order_relaxed);
    if (used >= kMaxMovesPerWindow) return 0;
    return g_used.compare_exchange_strong(used, used + 1, std::memory_order_relaxed) ? 1 : 0;
}
