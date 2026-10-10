#pragma once

#include "firewall_rule.hpp"   // for FirewallFamily
#include <optional>
#include <string_view>

namespace keen_pbr3 {

// Family of an IP address or CIDR ("1.2.3.4", "10.0.0.0/8", "2001:db8::1",
// "2001:db8::/32"), classified by the list parser's validators so there is one
// address grammar in the project. Returns std::nullopt for anything else
// (empty, garbage, out-of-range prefix, surrounding spaces, a leading '!',
// zone ids). IPv4-mapped IPv6 ("::ffff:1.2.3.4") is IPv6.
std::optional<FirewallFamily> ip_family_of(std::string_view address);

}  // namespace keen_pbr3
