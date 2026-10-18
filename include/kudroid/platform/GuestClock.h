#pragma once

#include <cstdint>
#include <ctime>

namespace kudroid {

inline clockid_t host_clock_for_guest(int clock_id) {
#if defined(__APPLE__)
    switch (clock_id) {
        case 0: case 5: return CLOCK_REALTIME;
        // Android monotonic time excludes suspend and shares Metal's host timeline.
        case 1: case 4: case 6: return CLOCK_UPTIME_RAW;
        case 2: return CLOCK_PROCESS_CPUTIME_ID;
        case 3: return CLOCK_THREAD_CPUTIME_ID;
        case 7: return CLOCK_MONOTONIC;
        default: return static_cast<clockid_t>(-1);
    }
#else
    return static_cast<clockid_t>(clock_id);
#endif
}

inline int64_t guest_clock_ns(int clock_id) {
    struct timespec ts{};
    if (::clock_gettime(host_clock_for_guest(clock_id), &ts) != 0) return 0;
    return static_cast<int64_t>(ts.tv_sec) * 1000000000ll + ts.tv_nsec;
}

inline int64_t guest_monotonic_ns() { return guest_clock_ns(1); }

}  // namespace kudroid
