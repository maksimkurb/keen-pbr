#pragma once

#include "firewall_plan.hpp"

#include "../config/config.hpp"
#include "../lists/list_set_usage.hpp"
#include "../routing/netlink.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace keen_pbr3 {

// Immutable inputs shared by route rule modules. It deliberately contains
// data views only; backend mutation happens when the complete plan is applied.
struct FirewallBuildContext {
  const std::vector<RouteRule>& route_rules;
  const std::vector<Outbound>& outbounds;
  const std::map<std::string, ListConfig>& lists;
  const std::map<std::string, ListSetUsage>& list_usage;
  const std::vector<DumpedRoute>& main_routes;
  const std::vector<DumpedInterface>& interfaces;
  FirewallBackend backend{FirewallBackend::iptables};
  bool ipv6_enabled{true};
  uint32_t fwmark_mask{0xFFFFFFFFu};
  const FirewallBalanceCandidates* balance_candidates{nullptr};
  const Config* config{nullptr};
  const OutboundMarkMap* outbound_marks{nullptr};
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

void register_route_rule_targets(const FirewallBuildContext& context,
                                 FirewallRuleRegistrar& registrar,
                                 std::string_view module_id,
                                 std::size_t rule_index,
                                 const FirewallRuleAction& action,
                                 bool action_enabled = true);

// Policy modules. Each registers its rules into the plan, deciding for itself
// from the context whether it applies; none touches a backend.
void register_route_mark_rules(const FirewallBuildContext& context,
                               FirewallRuleRegistrar& registrar);
void register_route_drop_rules(const FirewallBuildContext& context,
                               FirewallRuleRegistrar& registrar);
void register_route_pass_rules(const FirewallBuildContext& context,
                               FirewallRuleRegistrar& registrar);
void register_route_balance_rules(const FirewallBuildContext& context,
                                  FirewallRuleRegistrar& registrar);
void register_dns_detour_rules(const FirewallBuildContext& context,
                               FirewallRuleRegistrar& registrar);
void register_restore_conntrack_mark_rules(const FirewallBuildContext& context,
                                           FirewallRuleRegistrar& registrar);
void register_skip_established_or_dnat_rules(
    const FirewallBuildContext& context, FirewallRuleRegistrar& registrar);
void register_skip_marked_packets_rules(const FirewallBuildContext& context,
                                        FirewallRuleRegistrar& registrar);
void register_inbound_interface_filter_rules(
    const FirewallBuildContext& context, FirewallRuleRegistrar& registrar);

using RouteRuleModuleRegistration =
    void (*)(const FirewallBuildContext&, FirewallRuleRegistrar&);

// Iterable view over the module manifest; its size is deduced where the
// manifest is defined (firewall_rule_modules.cpp).
struct RouteRuleModuleManifest {
  const RouteRuleModuleRegistration* first;
  const RouteRuleModuleRegistration* last;

  const RouteRuleModuleRegistration* begin() const { return first; }
  const RouteRuleModuleRegistration* end() const { return last; }
  std::size_t size() const { return static_cast<std::size_t>(last - first); }
};

// Explicit order is part of the plan contract. Adding another module means
// adding its registration function to the manifest in the .cpp, not a branch
// in the runtime apply loop.
RouteRuleModuleManifest route_rule_module_manifest();

} // namespace keen_pbr3
