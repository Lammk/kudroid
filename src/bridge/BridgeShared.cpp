// BridgeShared.cpp — definitions for the cross-unit state declared in
// BridgeShared.h. Moved out of the old monolith unchanged; see the header for
// what each variable is for.
#include "BridgeShared.h"

std::atomic<bool> g_hasCrashed{false};
char g_lastCrashTail[16384] = {0};
pthread_t g_mainThread = 0;

std::atomic<bool> s_isApkRunning{false};
std::atomic<bool> s_stopping{false};
std::mutex s_runStateMutex;
std::condition_variable s_runStateCv;
std::atomic<unsigned long long> s_pausedGeneration{0};
std::atomic<uint64_t> s_installGeneration{0};
std::atomic<bool> s_libsResident{false};
std::atomic<uint64_t> s_libsGeneration{0};
std::atomic<int> g_requestedOrientation{-1};  // -1=Unspecified, 0=Landscape, 1=Portrait
std::atomic<int> s_keepScreenOn{1};  // on while an app runs (No Sleep)
std::atomic<int> s_softInputVisible{0};

ApkRunStartResult begin_apk_run(std::chrono::milliseconds teardown_wait) {
    std::unique_lock<std::mutex> lock(s_runStateMutex);
    if (s_isApkRunning.load()) {
        if (!s_stopping.load()) {
            return ApkRunStartResult::AlreadyActive;
        }
        if (!s_runStateCv.wait_for(lock, teardown_wait, [] {
                return !s_isApkRunning.load();
            })) {
            return ApkRunStartResult::TeardownTimedOut;
        }
    }
    s_isApkRunning.store(true);
    s_stopping.store(false);
    return ApkRunStartResult::Acquired;
}

bool request_apk_stop() {
    std::lock_guard<std::mutex> lock(s_runStateMutex);
    if (!s_isApkRunning.load() || s_stopping.load()) {
        return false;
    }
    s_stopping.store(true);
    return true;
}

void complete_apk_run() {
    {
        std::lock_guard<std::mutex> lock(s_runStateMutex);
        s_isApkRunning.store(false);
        s_stopping.store(false);
    }
    s_runStateCv.notify_all();
}
