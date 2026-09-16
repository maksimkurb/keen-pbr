#pragma once

#include "firewall_plan.hpp"

#include "../config/routing_state.hpp"
#include "../lists/list_set_usage.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace keen_pbr3 {

// Immutable inputs shared by route rule modules.  It deliberately contains
// data views only; backend mutation remains in the compatibility adapter.
struct FirewallBuildContext {
  const std::vector<RouteRule>& route_rules;
  const std::vector<RuleState>& rule_states;
  const std::vector<Outbound>& outbounds;
  const std::map<std::string, ListConfig>& lists;
  const std::map<std::string, ListSetUsage>& list_usage;
  const std::vector<DumpedRoute>& main_routes;
  const std::vector<DumpedInterface>& interfaces;
  FirewallBackend backend{FirewallBackend::iptables};
  bool ipv6_enabled{true};
  uint32_t fwmark_mask{0xFFFFFFFFu};
  const FirewallBalanceCandidates* balance_candidates{nullptr};
};

// One physical route selector target before an action is attached.
struct RouteRuleTarget {
  FirewallRuleCriteria criteria;
  std::optional<std::string> set_name;
  FirewallFamily family{FirewallFamily::any};
  uint32_t set_timeout{0};
  std::size_t occurrence{0};
  bool rule_enabled{true};
};

// Expand one route rule without registering rules or touching a backend.
std::vector<RouteRuleTarget> expand_route_rule_targets(
    const FirewallBuildContext& context, std::size_t rule_index);

class RouteMarkRuleModule final {
public:
  std::string_view id() const noexcept { return "route.mark"; }
  void register_rules(const FirewallBuildContext& context,
                      FirewallRuleRegistrar& registrar) const;
};

class RouteDropRuleModule final {
public:
  std::string_view id() const noexcept { return "route.drop"; }
  void register_rules(const FirewallBuildContext& context,
                      FirewallRuleRegistrar& registrar) const;
};

class RoutePassRuleModule final {
public:
  std::string_view id() const noexcept { return "route.pass"; }
  void register_rules(const FirewallBuildContext& context,
                      FirewallRuleRegistrar& registrar) const;
};

class RouteBalanceRuleModule final {
public:
  std::string_view id() const noexcept { return "route.balance"; }
  void register_rules(const FirewallBuildContext& context,
                      FirewallRuleRegistrar& registrar) const;
};

// Explicit order is part of the plan contract. Adding another route action
// means adding its registration function to this manifest, not a branch in
// the runtime apply loop.
using RouteRuleModuleRegistration =
    void (*)(const FirewallBuildContext&, FirewallRuleRegistrar&);

std::array<RouteRuleModuleRegistration, 4> route_rule_module_manifest();

} // namespace keen_pbr3
