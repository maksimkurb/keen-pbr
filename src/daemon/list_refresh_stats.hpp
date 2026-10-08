#pragma once

#include <cstdint>
#include <optional>

namespace keen_pbr3 {

// Per remote list refresh telemetry kept in memory by ListService: written when
// a download finishes (cold path) and read at Prometheus scrape time.
struct ListRefreshStats {
    // Wall clock of the last successful check (content updated or confirmed
    // unchanged); empty until a refresh succeeded in this process.
    std::optional<int64_t> last_success_unix_s;
    uint64_t errors{0};  // failed refresh attempts
};

} // namespace keen_pbr3
