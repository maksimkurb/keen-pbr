#pragma once

#include "firewall_physical.hpp"
#include "firewall_plan.hpp"
#include "firewall_snapshot.hpp"
#include "../health/routing_health.hpp"

#include <vector>

namespace keen_pbr3 {

// True when `expected` contains IPv6 chains, i.e. the IPv6 tables have to be
// inspected.
bool firewall_expected_uses_ipv6(const PhysicalRuleset& expected);

// Policy-agnostic ordered diff of the kernel state (`snapshot.ruleset`)
// against the ruleset the backend produced for the active apply (`expected`,
// FirewallApplyResult::expected_ruleset).
//
//   * every expected chain is compared rule by rule, in order; base chain
//     attributes (nft hook/priority/policy) are compared as well;
//   * observed keen-pbr chains that are not expected are reported, including
//     leftovers of the retired iptables A/B layout (apply deletes them, so a
//     leftover is real drift the daemon fixes on the next apply);
//   * hook jumps are ordinary rules of the expected system chains,
//     foreign rules in system chains never reach the observed ruleset.
//
// One FirewallRuleCheck is produced per plan rule that was lowered to at least
// one physical rule (attributed through PhysicalRule::plan_rule, so it works
// without rule comments); differences that belong to no plan rule (hooks,
// setter chains, unexpected rules or chains) are appended as extra checks.
std::vector<FirewallRuleCheck> verify_firewall_plan(
    const FirewallPlan& plan, const PhysicalRuleset& expected,
    const FirewallSnapshot& snapshot);

} // namespace keen_pbr3
