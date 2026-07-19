#pragma once

#include "../config/config.hpp"

#include <string>

namespace keen_pbr3 {

struct DiskConfigState {
    bool matches_active{false};
    std::string error;
};

// Read-only comparison used by status paths. Invalid or missing disk config is
// a mismatch, but never changes the committed in-memory configuration.
DiskConfigState inspect_disk_config_state(const std::string& config_path,
                                          const Config& active_config);

} // namespace keen_pbr3
