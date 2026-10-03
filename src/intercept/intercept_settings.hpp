#pragma once

#include "../config/config.hpp"
#include "../firewall/firewall.hpp"
#include "../firewall/firewall_rule_modules.hpp"
#include "intercept_capabilities.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace keen_pbr3 {

// Effective interception settings: the `intercept` config section (with
// defaults applied) restricted by what the kernel supports.
struct InterceptEffective {
    bool config_enabled{false};     // intercept.enabled
    bool dns_hold{false};           // DNS hold rules + set filling are active
    bool l7{false};                 // SNI/Host/QUIC sniffing is active
    uint16_t queue_num{9053};
    uint16_t nflog_group{9054};
    int hold_timeout_ms{30};
    uint32_t min_ttl_s{300};
    uint32_t max_ttl_s{86400};
    std::string marker_domain{"check.keen.pbr"};
    std::array<uint8_t, 4> marker_ipv4{127, 0, 0, 88};
    bool tls{true};
    bool http{true};
    bool quic{true};
    InterceptCapabilities capabilities;
    // Why a configured part is not active (capability gaps).
    std::vector<std::string> reasons;

    bool active() const { return dns_hold || l7; }

    // nullopt when nothing is active, i.e. no interception rules are planned.
    std::optional<InterceptFirewallSettings> firewall_settings() const;

    bool operator==(const InterceptEffective& other) const;
    bool operator!=(const InterceptEffective& other) const { return !(*this == other); }
};

// config x capabilities.  Never throws; disabled parts are reported in
// `reasons`.
InterceptEffective resolve_effective_intercept(const Config& config,
                                               FirewallBackend backend,
                                               const InterceptCapabilities& capabilities);

// Whether the dnsmasq config must omit ipset=/nftset= directives (the daemon
// fills the dynamic sets itself).
inline bool intercept_replaces_resolver_sets(const InterceptEffective& effective) {
    return effective.dns_hold;
}

} // namespace keen_pbr3
