#pragma once

#include "../config/config.hpp"

namespace keen_pbr3 {

struct Ipv6SupportDecision {
    bool enabled{true};
    enum class Reason {
        Enabled,
        DisabledByConfig,
        UnsupportedBySystem
    } reason{Reason::Enabled};
};

// Raw probes, run once by probe_kernel_capabilities() (util/kernel_capabilities.hpp).
// Everything else decides through resolve_ipv6_support(), which only reads the
// cached snapshot.
bool system_ipv6_supported();
bool iptables_ipv6_supported();
bool nft_ipv6_supported();
Ipv6SupportDecision resolve_ipv6_support(const Config& config);
void log_ipv6_support_decision_once(const Ipv6SupportDecision& decision);

} // namespace keen_pbr3
