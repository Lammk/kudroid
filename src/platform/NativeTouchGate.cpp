#include "kudroid/platform/NativeTouchGate.h"

#include <chrono>
#include <mutex>

// A drag arrives at 60-120 Hz and every delivered MOVE costs one interpreted
// JNI hop (kuart_post_touch_event -> ActivityThread.postTouchEvent), each
// grabbing the VM lock against the render/main threads. Gate MOVEs at the
// source: at most two per 60 Hz window pass through, extra ones are dropped —
// the newest already replaces the pending one at the sinks (InputShim queue,
// ActivityThread message), so an intermediate position carries no information
// the app still needs. DOWN/UP/CANCEL never pass through here; their ordering
// is what the app reasons about.

namespace {

struct SourceGate {
    std::mutex mtx;
    int used = 0;               // MOVE budget consumed in the current window
    long long window_start = 0; // steady-clock ns
};

SourceGate g_gate;

constexpr int kMaxMovesPerWindow = 1;
constexpr long long kWindowNs = 33333333;  // one 30 Hz window

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
