#pragma once

#include "firewall_plan.hpp"

#include "../config/config.hpp"
#include "../lists/list_set_usage.hpp"
#include "../routing/netlink.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace keen_pbr3 {

struct ObservedFirewallRule;

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
  bool restore_conntrack_mark{true};
  bool skip_established_or_dnat{true};
  bool skip_marked_packets{true};
  std::vector<std::string> inbound_interfaces;
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

std::string balance_rule_mismatch_detail(const ObservedFirewallRule& observed,
                                         const BalanceAction& expected,
                                         uint32_t fwmark_mask);

class DnsDetourRuleModule final {
public:
  std::string_view id() const noexcept { return "dns.detour"; }
  void register_rules(const FirewallBuildContext& context,
                      FirewallRuleRegistrar& registrar) const;
};

class RestoreConntrackMarkRuleModule final {
public:
  std::string_view id() const noexcept { return "prefilter.restore_conntrack_mark"; }
  void register_rules(const FirewallBuildContext& context,
                      FirewallRuleRegistrar& registrar) const;
};

class SkipEstablishedOrDnatRuleModule final {
public:
  std::string_view id() const noexcept { return "prefilter.skip_established_or_dnat"; }
  void register_rules(const FirewallBuildContext& context,
                      FirewallRuleRegistrar& registrar) const;
};

class SkipMarkedPacketsRuleModule final {
public:
  std::string_view id() const noexcept { return "prefilter.skip_marked_packets"; }
  void register_rules(const FirewallBuildContext& context,
                      FirewallRuleRegistrar& registrar) const;
};

class InboundInterfaceFilterRuleModule final {
public:
  std::string_view id() const noexcept { return "prefilter.inbound_interface"; }
  void register_rules(const FirewallBuildContext& context,
                      FirewallRuleRegistrar& registrar) const;
};

// Explicit order is part of the plan contract. Adding another route action
// means adding its registration function to this manifest, not a branch in
// the runtime apply loop.
using RouteRuleModuleRegistration =
    void (*)(const FirewallBuildContext&, FirewallRuleRegistrar&);

std::array<RouteRuleModuleRegistration, 9> route_rule_module_manifest();

} // namespace keen_pbr3
