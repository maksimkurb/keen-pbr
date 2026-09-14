#pragma once

#include "../cache/cache_manager.hpp"
#include "../config/config.hpp"
#include "../routing/firewall_state.hpp"
#include "../routing/netlink.hpp"
#include "firewall.hpp"

#include <vector>

namespace keen_pbr3 {

struct FirewallConfigApplyPolicy {
  FirewallApplyMode mode{FirewallApplyMode::PreserveSets};
  bool force_clear_dynamic_sets{false};
};

// Select the apply mode required when a candidate configuration replaces the
// currently active runtime configuration. iptables cannot change an existing
// ipset's maxelem in place, so capacity changes require recreating owned sets.
FirewallConfigApplyPolicy firewall_config_apply_policy(
    FirewallBackend backend, const Config &current, const Config &candidate);

// Materialize the runtime firewall configuration using the real backend.
// Returns the realized rule-state snapshot that should be stored for later
// verification and status reporting.
std::vector<RuleState> apply_runtime_firewall(
    const Config& config,
    const OutboundMarkMap& outbound_marks,
    const CacheManager& cache_manager,
    Firewall& firewall,
    FirewallApplyMode mode = FirewallApplyMode::Destructive,
    const std::vector<RuleState>* previous_rule_states = nullptr,
    bool force_clear_dynamic_sets = false,
    const std::vector<DumpedRoute>& main_routes = {},
    const std::vector<DumpedInterface>& interfaces = {},
    const FirewallBalanceCandidates* balance_candidates = nullptr);

} // namespace keen_pbr3
