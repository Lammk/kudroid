#pragma once

#include <cstdint>

// The host's default network path, as seen by android.net.ConnectivityManager.
//
// The host (iOS) watches the path and reports it here; the guest reads it back and is told
// on its main Looper when it changes. Until the host reports, the guest sees an unmetered
// Wi-Fi connection, which is what a host with no path monitoring can honestly assume.
extern "C" {

// Host to guest. `transport` is a ConnectivityManager.TYPE_* value: 0 cellular, 1 Wi-Fi,
// 9 Ethernet. `metered` marks an expensive path; `constrained` is Low Data Mode.
void kudroid_set_network_state(int connected, int transport, int metered, int constrained);

// True while a change waits for the guest.
int kudroid_network_has_pending(void);

// Guest side: bit 0 connected, bits 1-8 transport, bit 9 metered, bit 10 constrained.
int32_t bionic_kudroid_network_state(void);
// 1 when the state changed since the last call; clears the flag.
int32_t bionic_kudroid_network_take_change(void);

}  // extern "C"
