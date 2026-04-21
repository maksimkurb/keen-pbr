#pragma once

#include "../cache/cache_manager.hpp"
#include "../config/config.hpp"
#include "../routing/firewall_state.hpp"
#include "firewall.hpp"

#include <map>
#include <string>
#include <vector>

namespace keen_pbr3 {

// Materialize the runtime firewall configuration using the real backend.
// Returns the realized rule-state snapshot that should be stored for later
// verification and status reporting.
std::vector<RuleState> apply_runtime_firewall(
    const Config& config,
    const OutboundMarkMap& outbound_marks,
    const std::map<std::string, std::string>& urltest_selections,
    const CacheManager& cache_manager,
    Firewall& firewall,
    FirewallApplyMode mode = FirewallApplyMode::Destructive);

} // namespace keen_pbr3
