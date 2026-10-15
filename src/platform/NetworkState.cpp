#include "kudroid/platform/NetworkState.h"

#include <atomic>

extern "C" void kudroid_looper_wake_main(void);

namespace {

constexpr int32_t kConnected = 1;
constexpr int32_t kTransportShift = 1;
constexpr int32_t kMetered = 1 << 9;
constexpr int32_t kConstrained = 1 << 10;
constexpr int kTypeWifi = 1;

std::atomic<int32_t> g_state{kConnected | (kTypeWifi << kTransportShift)};
std::atomic<bool> g_changed{false};

}  // namespace

extern "C" void kudroid_set_network_state(int connected, int transport, int metered,
                                          int constrained) {
    int32_t state = 0;
    if (connected != 0) {
        state = kConnected | ((transport & 0xFF) << kTransportShift) |
                (metered != 0 ? kMetered : 0) | (constrained != 0 ? kConstrained : 0);
    }
    if (g_state.exchange(state) == state) return;
    g_changed.store(true);
    kudroid_looper_wake_main();
}

extern "C" int kudroid_network_has_pending(void) { return g_changed.load() ? 1 : 0; }

extern "C" int32_t bionic_kudroid_network_state(void) { return g_state.load(); }

extern "C" int32_t bionic_kudroid_network_take_change(void) {
    return g_changed.exchange(false) ? 1 : 0;
}
