#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace keen_pbr3::icmptest_limits {

constexpr int64_t default_interval_ms = 60000;
constexpr int64_t default_timeout_ms = 1000;
constexpr int64_t default_count = 3;
constexpr int64_t default_max_failed = 0;
constexpr int64_t default_packet_interval_ms = 200;
constexpr int64_t default_max_rtt_ms = 500;
constexpr int64_t default_tolerance_ms = 10;
constexpr int64_t default_breaker_timeout_ms = 60000;

constexpr int64_t min_interval_ms = 1000;
constexpr int64_t max_interval_ms = 24 * 60 * 60 * 1000;
constexpr int64_t max_sweep_ms = 10 * 60 * 1000;
constexpr int64_t max_count = 10;
constexpr std::size_t max_candidates = 16;
constexpr int64_t max_packets_per_sweep = 160;

inline int64_t candidate_worst_case_ms(int64_t count, int64_t timeout_ms,
                                       int64_t packet_interval_ms) {
    return count * timeout_ms + (count - 1) * packet_interval_ms;
}

inline int64_t minimum_interval_with_reserve_ms(int64_t sweep_ms) {
    return std::max(min_interval_ms, (sweep_ms * 125 + 99) / 100);
}

} // namespace keen_pbr3::icmptest_limits
