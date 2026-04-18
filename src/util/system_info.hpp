#pragma once

#include <string>

namespace keen_pbr3 {

struct SystemInfo {
    std::string os_type{"unknown"};
    std::string os_version{"unknown"};
    std::string build_variant{"unknown"};
};

// Detect host OS metadata used by the API/UI to expose platform-specific features.
SystemInfo detect_system_info();

// Cached process-wide snapshot of detect_system_info().
const SystemInfo& cached_system_info();

} // namespace keen_pbr3
