#pragma once

#include <chrono>
#include <cstdint>
#include <ctime>

namespace keen_pbr3 {

inline std::int64_t unix_timestamp_now_seconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

// CLOCK_BOOTTIME in milliseconds: monotonic, counts suspend time and is not
// affected by wall-clock steps; system-wide, so comparable across processes.
// Returns 0 if the clock is unavailable.
inline std::int64_t boottime_now_ms() {
    timespec ts{};
    if (clock_gettime(CLOCK_BOOTTIME, &ts) != 0) {
        return 0;
    }
    return static_cast<std::int64_t>(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
}

} // namespace keen_pbr3
