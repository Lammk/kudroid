#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Host bridge for android.location.LocationManager, following the
// KuDroidSensorHost pattern: the iOS shell registers a vtable backed by
// CoreLocation, and pushes fixes back through kudroid_location_inject_fix().
// The core keeps the latest fix per subscription plus a small queue, which
// the guest Java side drains from its main-thread poll loop (see
// framework/android/location/LocationManager.java), so LocationListener
// callbacks always run on the guest main thread.
typedef struct KuDroidLocationHost {
    // Start updates for a subscription handle. minTimeMs/minDistanceM mirror
    // LocationManager.requestLocationUpdates semantics.
    void (*start)(int64_t handle, int64_t minTimeMs, float minDistanceM);
    void (*stop)(int64_t handle);
    // One-shot fix request; the answer arrives via inject_fix like any other.
    void (*request_single)(int64_t handle);
} KuDroidLocationHost;

void kudroid_set_location_host(const KuDroidLocationHost* host);

// Provider availability as last reported by the host OS (CoreLocation
// authorization + services enabled). Returns 1 when location can be used.
int32_t kudroid_location_services_enabled(void);
void kudroid_location_set_services_enabled(int32_t enabled);

// Push one fix into the core. `handle` is the subscription it belongs to;
// handle 0 updates only the global last-known fix.
void kudroid_location_inject_fix(int64_t handle, double latitude, double longitude,
                                 float accuracyM, double altitudeM, float speedMps,
                                 float bearingDeg, int64_t timeMs);

#ifdef __cplusplus
}  // extern "C"
#endif

#ifdef __cplusplus
#include <string>

namespace kudroid {
namespace location {

struct Fix {
    double latitude = 0;
    double longitude = 0;
    float accuracyM = 0;
    double altitudeM = 0;
    float speedMps = 0;
    float bearingDeg = 0;
    int64_t timeMs = 0;
};

void Start(int64_t handle, int64_t minTimeMs, float minDistanceM);
void Stop(int64_t handle);
void RequestSingle(int64_t handle);
bool ServicesEnabled();

// Drains the oldest queued fix for a subscription. Returns false when the
// queue is empty. Encoded form for the Java poll loop:
// "<lat>|<lon>|<accuracy>|<altitude>|<speed>|<bearing>|<timeMs>".
bool PollFix(int64_t handle, Fix* out);
bool LastKnown(Fix* out);
std::string Encode(const Fix& fix);

}  // namespace location
}  // namespace kudroid
#endif
