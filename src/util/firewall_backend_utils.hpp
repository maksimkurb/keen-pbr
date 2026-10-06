#pragma once

#include "../firewall/firewall.hpp"

namespace keen_pbr3 {

bool firewall_backend_command_exists(FirewallBackend backend);
FirewallBackend detect_firewall_backend();
FirewallBackend resolve_firewall_backend(FirewallBackendPreference backend_pref);

#ifdef KEEN_PBR3_TESTING
void set_detected_firewall_backend_for_tests(std::optional<FirewallBackend> backend);
void reset_detected_firewall_backend_for_tests();
#endif

} // namespace keen_pbr3
