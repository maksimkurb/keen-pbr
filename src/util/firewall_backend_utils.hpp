#pragma once

#include "../firewall/firewall.hpp"

namespace keen_pbr3 {

bool firewall_backend_command_exists(FirewallBackend backend);
FirewallBackend detect_firewall_backend();
FirewallBackend resolve_firewall_backend(FirewallBackendPreference backend_pref);

} // namespace keen_pbr3
