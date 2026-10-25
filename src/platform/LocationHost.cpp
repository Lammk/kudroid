// Core-side state for the LocationManager host bridge (see LocationHost.h).
// Platform-free like WebViewHost.cpp: on headless builds subscriptions are
// recorded and fixes can be injected by tests; on iOS the shell registers a
// CoreLocation-backed vtable.
#include "kudroid/platform/LocationHost.h"

#include <cstdio>
#include <deque>
#include <mutex>
#include <unordered_map>

namespace {

std::mutex gMutex;
KuDroidLocationHost gHost{};
bool gHasHost = false;
int32_t gServicesEnabled = 0;
std::unordered_map<int64_t, std::deque<kudroid::location::Fix>> gQueues;
kudroid::location::Fix gLastKnown{};
bool gHasLastKnown = false;

}  // namespace

extern "C" void kudroid_set_location_host(const KuDroidLocationHost* host) {
    std::lock_guard<std::mutex> lock(gMutex);
    if (host) {
        gHost = *host;
        gHasHost = true;
    } else {
        gHost = KuDroidLocationHost{};
        gHasHost = false;
    }
}

extern "C" int32_t kudroid_location_services_enabled(void) {
    std::lock_guard<std::mutex> lock(gMutex);
    return gServicesEnabled;
}

extern "C" void kudroid_location_set_services_enabled(int32_t enabled) {
    std::lock_guard<std::mutex> lock(gMutex);
    gServicesEnabled = enabled ? 1 : 0;
}

extern "C" void kudroid_location_inject_fix(int64_t handle, double latitude,
                                            double longitude, float accuracyM,
                                            double altitudeM, float speedMps,
                                            float bearingDeg, int64_t timeMs) {
    kudroid::location::Fix fix;
    fix.latitude = latitude;
    fix.longitude = longitude;
    fix.accuracyM = accuracyM;
    fix.altitudeM = altitudeM;
    fix.speedMps = speedMps;
    fix.bearingDeg = bearingDeg;
    fix.timeMs = timeMs;

    std::lock_guard<std::mutex> lock(gMutex);
    gLastKnown = fix;
    gHasLastKnown = true;
    if (handle != 0) {
        auto& queue = gQueues[handle];
        // Bound per-subscription backlog; location consumers want the newest
        // fixes, so drop the oldest when a guest stops polling.
        if (queue.size() >= 256) queue.pop_front();
        queue.push_back(fix);
    }
}

namespace kudroid {
namespace location {

void Start(int64_t handle, int64_t minTimeMs, float minDistanceM) {
    KuDroidLocationHost host{};
    bool hasHost;
    {
        std::lock_guard<std::mutex> lock(gMutex);
        gQueues[handle];  // ensure the queue exists before fixes arrive
        host = gHost;
        hasHost = gHasHost;
    }
    if (hasHost && host.start) host.start(handle, minTimeMs, minDistanceM);
}

void Stop(int64_t handle) {
    KuDroidLocationHost host{};
    bool hasHost;
    {
        std::lock_guard<std::mutex> lock(gMutex);
        gQueues.erase(handle);
        host = gHost;
        hasHost = gHasHost;
    }
    if (hasHost && host.stop) host.stop(handle);
}

void RequestSingle(int64_t handle) {
    KuDroidLocationHost host{};
    bool hasHost;
    {
        std::lock_guard<std::mutex> lock(gMutex);
        gQueues[handle];
        host = gHost;
        hasHost = gHasHost;
    }
    if (hasHost && host.request_single) host.request_single(handle);
}

bool ServicesEnabled() { return kudroid_location_services_enabled() != 0; }

bool PollFix(int64_t handle, Fix* out) {
    std::lock_guard<std::mutex> lock(gMutex);
    auto it = gQueues.find(handle);
    if (it == gQueues.end() || it->second.empty()) return false;
    if (out) *out = it->second.front();
    it->second.pop_front();
    return true;
}

bool LastKnown(Fix* out) {
    std::lock_guard<std::mutex> lock(gMutex);
    if (!gHasLastKnown) return false;
    if (out) *out = gLastKnown;
    return true;
}

std::string Encode(const Fix& fix) {
    char buf[256];
    std::snprintf(buf, sizeof(buf), "%.17g|%.17g|%.9g|%.17g|%.9g|%.9g|%lld",
                  fix.latitude, fix.longitude, static_cast<double>(fix.accuracyM),
                  fix.altitudeM, static_cast<double>(fix.speedMps),
                  static_cast<double>(fix.bearingDeg),
                  static_cast<long long>(fix.timeMs));
    return std::string(buf);
}

}  // namespace location
}  // namespace kudroid
