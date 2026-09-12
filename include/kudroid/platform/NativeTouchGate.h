// Source-side pacing for host touch injection (NativeTouchGate.cpp).
// MOVE floods are budgeted per frame window; DOWN/UP/CANCEL always pass.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Reset the MOVE budget (called on runtime start).
void kudroid_touch_source_gate_init(void);

// 1 when this MOVE may be forwarded, 0 when it should be dropped.
int kudroid_touch_source_gate_allow_move(void);

#ifdef __cplusplus
}  // extern "C"
#endif
