#pragma once

#include <cstdint>
#include <limits>
#include <time.h>

namespace kudroid {

inline std::int64_t timer_deadline_delay_ns(const timespec& value,
                                          const timespec& now,
                                          bool absolute) {
    constexpr std::int64_t billion = 1000000000;
    __int128 delay = static_cast<__int128>(value.tv_sec) * billion + value.tv_nsec;
    if (absolute) {
        delay -= static_cast<__int128>(now.tv_sec) * billion + now.tv_nsec;
    }
    if (delay <= 0) return 0;
    if (delay > std::numeric_limits<std::int64_t>::max()) {
        return std::numeric_limits<std::int64_t>::max();
    }
    return static_cast<std::int64_t>(delay);
}

} // namespace kudroid
