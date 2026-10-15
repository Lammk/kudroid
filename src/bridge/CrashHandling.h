// CrashHandling.h — crash handler seam for the bridge unit family.
//
// The signal handler itself, its per-thread fault budgets and the JNI_OnLoad
// guard live in CrashHandling.cpp. Other bridge units need only the seam
// declared here: the buffer mirror, the main-thread predicate, the installer
// and the disposition logger.

#pragma once

#include <string>

// Main-thread predicate used by the crash handler to distinguish the iOS main
// thread (fatal, must report) from background worker threads (parks, skips).
int crashIsMainThread(void);
int crashIsBackgroundThread(void);
void crashNoteMainThread(void);  // records pthread_self as the main thread

// Install the process-wide signal handlers. Idempotent: the first call wins.
void installCrashHandlers(void);

// One-shot boot probe: does a signal delivery preserve x18? Guest binaries
// reserve x18 for a live address and nothing on the guest side can rebuild a
// destroyed value, so the answer decides whether guest code needs x18 lowered
// away or a delivery can be trusted to leave the register alone. Logs one line;
// no-op off Apple/arm64.
void kudroid_probe_platform_register(void);

// Reset the handler-owned worker budgets for a new app.
void crashResetWorkerBudgets(void);

// Diagnostic: whose handler owns the fatal signals right now? Call at X and at
// nativeDone entry to bracket a disposition change across teardown.
extern "C" void kudroid_log_signal_disposition(const char* tag);
