#pragma once

#include "firewall_plan.hpp"
#include "firewall_snapshot.hpp"
#include "../health/routing_health.hpp"

#include <cstdint>
#include <vector>

namespace keen_pbr3 {

// Compare canonical desired rules with one backend-neutral observation.  The
// plan is expanded only to the physical forms the selected backend emits;
// repeated comments from MARK/CONNMARK/RETURN or family/protocol expansion are
// treated as one logical bundle.
std::vector<FirewallRuleCheck> verify_firewall_plan(
    const FirewallPlan& plan, const FirewallSnapshot& snapshot,
    uint32_t fwmark_mask = 0xFFFFFFFFu);

} // namespace keen_pbr3
