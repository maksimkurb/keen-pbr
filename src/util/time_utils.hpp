#pragma once

#include <chrono>
#include <cstdint>

namespace keen_pbr3 {

inline std::int64_t unix_timestamp_now_seconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

} // namespace keen_pbr3
