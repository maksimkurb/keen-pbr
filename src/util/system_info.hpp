#pragma once

#include <optional>
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

// Extract a human-readable Keenetic firmware version from an RCI response body.
std::optional<std::string> parse_keenetic_version_from_rci_response(
    const std::string& response);

// Parse the leading Keenetic firmware major version from strings such as
// "4.3.2" or legacy "2.16.D.12.0-12".
std::optional<int> parse_keenetic_major_version(const std::string& version);

// Encrypted upstream DNS via Keenetic built-in DNS requires KeeneticOS 3.x+.
bool keenetic_version_supports_encrypted_dns(const std::string& version);

#ifdef KEEN_PBR3_TESTING
void set_system_info_for_tests(std::optional<SystemInfo> info);
void reset_system_info_for_tests();
#endif

} // namespace keen_pbr3
