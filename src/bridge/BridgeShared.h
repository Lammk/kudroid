// BridgeShared.h — file-scope statics several bridge units used to own when
// kudroid_bridge.cpp was one file. They are extern here and defined in
// BridgeShared.cpp so CrashHandling, GuestLibrary and AppLifecycle can share
// them without exposing them anywhere else.
#pragma once

#include <atomic>
#include <cstdint>
#include <pthread.h>

// ── CrashHandling state, shared with AppLifecycle ────────────────────────────
// Set by the crash handler (any fatal thread, parked worker, null-call crash
// or guest-handled fatal); polled every 0.25s by the shell to show the crash
// modal and run teardown.
extern std::atomic<bool> g_hasCrashed;
// Last 30 lines of the crash buffer at the moment of the crash, surfaced to
// the shell for the modal's detail view.
extern char g_lastCrashTail[16384];
// The iOS main thread as recorded by kudroid_set_log_dir; stand-in for
// pthread_main_np on non-Apple hosts.
extern pthread_t g_mainThread;

// ── AppLifecycle state, shared with GuestLibrary ─────────────────────────────
// One in-flight run at a time: set by kudroid_run_apk, read by install/stop.
extern std::atomic<bool> s_isApkRunning;
// Cooperative stop flag: poll loops check it and bail out early.
extern std::atomic<bool> s_stopping;
// Bumped when the Java side pauses; lets a run notice it was backgrounded.
extern std::atomic<unsigned long long> s_pausedGeneration;
// Bumped by kudroid_install_apk; a resident-library generation below it means
// the loaded guest .so set is stale and the next run must refuse.
extern std::atomic<uint64_t> s_installGeneration;

// Residency of the guest .so set (see BridgeState: bridgeMarkLibsResident);
// generation compared against s_installGeneration.
extern std::atomic<bool> s_libsResident;
extern std::atomic<uint64_t> s_libsGeneration;

// Requested Activity orientation: -1 unspecified, 0 landscape, 1 portrait.
extern std::atomic<int> g_requestedOrientation;

// Keep-screen-on latch for the running app (1 while an app runs).
extern std::atomic<int> s_keepScreenOn;

// Soft-input (IME) visibility flag driven by the Java callbacks.
extern std::atomic<int> s_softInputVisible;

// ── CrashHandling predicates shared with the other units ─────────────────────
// True when a fault on `tid` must stop the app (host main, guest UI, render
// threads, engine-critical names). Definition stays in CrashHandling.cpp next
// to the thread registry it reads.
bool kudroid_fault_is_fatal(unsigned long long tid, bool isHostMain);
