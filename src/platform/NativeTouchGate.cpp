#include "kudroid/platform/NativeTouchGate.h"

#include <chrono>
#include <mutex>

// A drag arrives at 60-120 Hz. The dispatch used to cost one VM-locked
// interpreted hop per sample from a worker thread; it now runs on the Looper
// thread (kuart_touch_drain_pending) and the queue plus ActivityThread both fold
// a pending MOVE, so the rate is bounded where it matters. This gate is now only
// a floor against a flood, not a rate cap: at 30 Hz it was silently limiting the
// app's own input, which is felt as a drag that trails the finger.
//
// [KuDroidTouch] drain timing is the check that the dispatch is still cheap: a
// line there means this constant is the smaller problem.

namespace {

struct SourceGate {
    std::mutex mtx;
    int used = 0;               // MOVE budget consumed in the current window
    long long window_start = 0; // steady-clock ns
};

SourceGate g_gate;

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
    std::lock_guard<std::mutex> lock(g_gate.mtx);
    g_gate.used = 0;
    g_gate.window_start = steady_now_ns();
}

extern "C" int kudroid_touch_source_gate_allow_move(void) {
    std::lock_guard<std::mutex> lock(g_gate.mtx);
    const long long now = steady_now_ns();
    if (now - g_gate.window_start >= kWindowNs) {
        g_gate.window_start = now;
        g_gate.used = 0;
    }
    if (g_gate.used >= kMaxMovesPerWindow) return 0;
    ++g_gate.used;
    return 1;
}
