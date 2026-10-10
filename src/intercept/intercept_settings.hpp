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
    bool dns_requested{false};      // intercept.dns.enabled (dns_hold stays off when unusable)
    bool l7{false};                 // SNI/Host/QUIC sniffing is active
    uint16_t queue_num{9053};
    uint16_t nflog_group{9054};
    int hold_timeout_ms{30};
    // Public configuration remains in milliseconds until the snapshot is
    // built; the netfilter writer consumes whole seconds.
    uint64_t min_ttl_ms{300000};
    uint64_t max_ttl_ms{86400000};
    std::string marker_domain{"check.keen.pbr"};
    std::array<uint8_t, 4> marker_ipv4{127, 0, 0, 88};
    bool tls{true};
    bool http{true};
    bool quic{true};
    // Conntrack entries of re-resolved destinations are deleted so that new
    // flows follow the routing set; off when the ctnetlink probe failed.
    bool conntrack_cleanup{true};
    InterceptCapabilities capabilities;
    // Why a configured part is not active (capability gaps).
    std::vector<std::string> reasons;
    // Degraded but still running (no fail-open, no conntrack cleanup, ...).
    std::vector<std::string> warnings;

    bool active() const { return dns_hold || l7; }

    // Interception is enabled in config but the DNS hold cannot run (kernel
    // capability missing, NFQUEUE bind or set write failed): domain-based
    // routing sets are not filled.
    bool dns_hold_unavailable() const { return config_enabled && dns_requested && !dns_hold; }

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

// Folds the results of binding the listeners (NFQUEUE queue, NFLOG group) into
// `effective`: stores them in `effective.capabilities.probe` and disables the
// part whose listener could not be bound.  Fail-open / replacement gaps only
// add warnings.  `listeners` uses the nfqueue, fail_open, replacement and nflog
// members; not_run members are ignored.
void apply_listener_probe(InterceptEffective& effective, const InterceptRuntimeProbe& listeners);

// Folds the dynamic-set write test into `effective`: a blocking result disables
// DNS hold and L7 (the daemon could not fill the sets), `skipped`/`not_run` do
// not change anything.
void apply_set_write_probe(InterceptEffective& effective, const nfnl::ProbeResult& set_write);
// Records the nft_timeout_update probe for health; never blocks anything.
void apply_timeout_update_probe(InterceptEffective& effective,
                                const nfnl::ProbeResult& timeout_update);


} // namespace keen_pbr3
