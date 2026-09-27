#include "firewall_rule_modules.hpp"

#include "../config/routing_state.hpp"
#include "../routing/target.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace keen_pbr3 {
namespace {

std::string logical_set_name(const std::string& list_name,
                             FirewallFamily family, bool dynamic) {
  const std::string prefix = family == FirewallFamily::ipv6 ? "kpbr6" : "kpbr4";
  return prefix + (dynamic ? "d_" : "_") + list_name;
}

FirewallFamily family_for_criteria(const FirewallRuleCriteria& criteria) {
  if (criteria.default_gateway == DefaultGatewayFamily::Ipv6) {
    return FirewallFamily::ipv6;
  }
  if (criteria.default_gateway == DefaultGatewayFamily::Ipv4) {
    return FirewallFamily::ipv4;
  }

  bool has_ipv4 = false;
  bool has_ipv6 = false;
  const auto inspect_addresses = [&](const std::vector<std::string>& addresses) {
    for (const auto& address : addresses) {
      if (address.find(':') == std::string::npos) {
        has_ipv4 = true;
      } else {
        has_ipv6 = true;
      }
    }
  };
  inspect_addresses(criteria.src_addr);
  inspect_addresses(criteria.dst_addr);
  if (has_ipv6 && !has_ipv4) {
    return FirewallFamily::ipv6;
  }
  if (has_ipv4 && !has_ipv6) {
    return FirewallFamily::ipv4;
  }
  return FirewallFamily::any;
}

std::string family_name(FirewallFamily family) {
  switch (family) {
  case FirewallFamily::ipv4:
    return "ipv4";
  case FirewallFamily::ipv6:
    return "ipv6";
  case FirewallFamily::any:
    return "any";
  }
  return "any";
}

std::string protocol_name(const FirewallRuleCriteria& criteria,
                          FirewallBackend backend) {
  if (criteria.proto == L4Proto::TcpUdp ||
      (backend == FirewallBackend::iptables &&
       criteria.proto == L4Proto::Any &&
       (!criteria.src_port.empty() || !criteria.dst_port.empty()))) {
    return "tcp-udp";
  }
  switch (criteria.proto) {
  case L4Proto::Any:
    return "any";
  case L4Proto::Tcp:
    return "tcp";
  case L4Proto::Udp:
    return "udp";
  case L4Proto::TcpUdp:
    return "tcp-udp";
  }
  return "any";
}

std::string instance_id(const FirewallBuildContext& context,
                        std::size_t rule_index,
                        const RouteRuleTarget& target) {
  const auto& criteria = target.criteria;
  return "rule=" + std::to_string(rule_index) +
         ";occurrence=" + std::to_string(target.occurrence) +
         ";target=" +
         (target.set_name.has_value() ? *target.set_name : "none") +
         ";family=" + family_name(target.family) +
         ";proto=" + protocol_name(criteria, context.backend) +
         ";hook=" + (criteria.apply_output ? "output" : "prerouting");
}

bool family_matches_default_gateway(const FirewallRuleCriteria& criteria,
                                    FirewallFamily family) {
  return criteria.default_gateway == DefaultGatewayFamily::None ||
         (criteria.default_gateway == DefaultGatewayFamily::Ipv4 &&
          family == FirewallFamily::ipv4) ||
         (criteria.default_gateway == DefaultGatewayFamily::Ipv6 &&
          family == FirewallFamily::ipv6);
}

template <typename ActionFactory>
void register_route_rule_targets(const FirewallBuildContext& context,
                                 FirewallRuleRegistrar& registrar,
                                 std::string_view module_id,
                                 std::size_t rule_index,
                                 ActionFactory action_factory,
                                 bool action_enabled = true) {
  for (const auto& target : expand_route_rule_targets(context, rule_index)) {
    if (target.set_name.has_value()) {
      registrar.register_set(
          {*target.set_name, target.family, target.set_timeout});
    }
    if (!target.rule_enabled || !action_enabled) {
      continue;
    }

    FirewallRuleInstance rule;
    rule.key = FirewallRuleKey::compact(
        module_id, instance_id(context, rule_index, target));
    rule.stage = FirewallRuleStage::route_classification;
    rule.priority = static_cast<int>(rule_index);
    rule.hook = target.criteria.apply_output ? FirewallHook::output
                                             : FirewallHook::prerouting;
    rule.family = target.family;
    rule.criteria = target.criteria;
    rule.criteria.dst_set_name = target.set_name;
    rule.action = action_factory();
    rule.source_rule_index = rule_index;
    registrar.register_rule(std::move(rule));
  }
}

} // namespace

std::vector<RouteRuleTarget> expand_route_rule_targets(
    const FirewallBuildContext& context, std::size_t rule_index) {
  if (rule_index >= context.route_rules.size()) {
    return {};
  }

  const auto& route_rule = context.route_rules[rule_index];
  const auto criteria = build_firewall_rule_criteria(
      route_rule, context.main_routes, context.interfaces);
  const auto& list_names = route_rule_lists(route_rule);
  std::vector<RouteRuleTarget> targets;

  const auto add_target = [&](const FirewallRuleCriteria& base_criteria,
                              const std::optional<std::string>& set_name,
                              FirewallFamily family, uint32_t timeout,
                              std::size_t occurrence) {
    targets.push_back({base_criteria, set_name, family, timeout, occurrence,
                       family_matches_default_gateway(base_criteria, family)});
  };

  if (!list_names.empty()) {
    bool emitted_rule = false;
    std::size_t list_occurrence = 0;
    for (const auto& list_name : list_names) {
      if (context.lists.find(list_name) == context.lists.end()) {
        ++list_occurrence;
        continue;
      }
      const auto usage = context.list_usage.find(list_name);
      if (usage == context.list_usage.end()) {
        ++list_occurrence;
        continue;
      }
      if (usage->second.has_static_entries) {
        add_target(criteria,
                   logical_set_name(list_name, FirewallFamily::ipv4, false),
                   FirewallFamily::ipv4, 0, list_occurrence * 4U);
        if (context.ipv6_enabled) {
          add_target(criteria,
                     logical_set_name(list_name, FirewallFamily::ipv6, false),
                     FirewallFamily::ipv6, 0, list_occurrence * 4U + 1U);
        }
        emitted_rule = true;
      }
      if (usage->second.has_domain_entries) {
        add_target(criteria,
                   logical_set_name(list_name, FirewallFamily::ipv4, true),
                   FirewallFamily::ipv4, usage->second.dynamic_timeout,
                   list_occurrence * 4U + 2U);
        if (context.ipv6_enabled) {
          add_target(criteria,
                     logical_set_name(list_name, FirewallFamily::ipv6, true),
                     FirewallFamily::ipv6, usage->second.dynamic_timeout,
                     list_occurrence * 4U + 3U);
        }
        emitted_rule = true;
      }
      ++list_occurrence;
    }
    if (!emitted_rule && criteria.has_rule_selector()) {
      add_target(criteria, std::nullopt, family_for_criteria(criteria), 0,
                 list_names.size() * 4U);
    }
  } else if (criteria.has_rule_selector()) {
    add_target(criteria, std::nullopt, family_for_criteria(criteria), 0, 0);
  }

  return targets;
}

void RouteMarkRuleModule::register_rules(
    const FirewallBuildContext& context, FirewallRuleRegistrar& registrar) const {
  if (context.outbound_marks == nullptr) {
    return;
  }

  for (std::size_t rule_index = 0; rule_index < context.route_rules.size();
       ++rule_index) {
    const auto& route_rule = context.route_rules[rule_index];
    if (!route_rule_enabled(route_rule)) {
      continue;
    }

    const auto decision =
        resolve_route_action(route_rule.outbound, context.outbounds);
    if (decision.is_skip || decision.is_passthrough ||
        !decision.outbound.has_value() || !*decision.outbound) {
      continue;
    }

    const auto& outbound = **decision.outbound;
    if (outbound.type == OutboundType::BLACKHOLE ||
        outbound_uses_balance(outbound)) {
      continue;
    }

    const auto mark_it = context.outbound_marks->find(outbound.tag);
    const uint32_t fwmark = mark_it == context.outbound_marks->end()
                                ? 0
                                : mark_it->second;
    register_route_rule_targets(
        context, registrar, id(), rule_index,
        [&] { return FirewallRuleAction{
                   MarkAction{fwmark, context.fwmark_mask}}; },
        fwmark != 0);
  }
}

void RouteDropRuleModule::register_rules(
    const FirewallBuildContext& context, FirewallRuleRegistrar& registrar) const {
  for (std::size_t rule_index = 0; rule_index < context.route_rules.size();
       ++rule_index) {
    const auto& route_rule = context.route_rules[rule_index];
    if (!route_rule_enabled(route_rule)) {
      continue;
    }
    const auto decision =
        resolve_route_action(route_rule.outbound, context.outbounds);
    if (!decision.outbound.has_value() || !*decision.outbound ||
        (*decision.outbound)->type != OutboundType::BLACKHOLE) {
      continue;
    }
    register_route_rule_targets(
        context, registrar, id(), rule_index,
        [] { return FirewallRuleAction{VerdictAction::drop}; });
  }
}

void RoutePassRuleModule::register_rules(
    const FirewallBuildContext& context, FirewallRuleRegistrar& registrar) const {
  for (std::size_t rule_index = 0; rule_index < context.route_rules.size();
       ++rule_index) {
    const auto& route_rule = context.route_rules[rule_index];
    if (!route_rule_enabled(route_rule)) {
      continue;
    }
    const auto decision =
        resolve_route_action(route_rule.outbound, context.outbounds);
    if (!decision.is_passthrough) {
      continue;
    }
    register_route_rule_targets(
        context, registrar, id(), rule_index,
        [] { return FirewallRuleAction{VerdictAction::pass}; });
  }
}

void RouteBalanceRuleModule::register_rules(
    const FirewallBuildContext& context, FirewallRuleRegistrar& registrar) const {
  for (std::size_t rule_index = 0; rule_index < context.route_rules.size();
       ++rule_index) {
    const auto& route_rule = context.route_rules[rule_index];
    if (!route_rule_enabled(route_rule)) {
      continue;
    }
    const auto decision =
        resolve_route_action(route_rule.outbound, context.outbounds);
    if (!decision.outbound.has_value() || !*decision.outbound ||
        !outbound_uses_balance(**decision.outbound) ||
        context.outbound_marks == nullptr) {
      continue;
    }
    const auto mark_it = context.outbound_marks->find((*decision.outbound)->tag);
    if (mark_it == context.outbound_marks->end() || mark_it->second == 0) {
      continue;
    }

    if (context.backend == FirewallBackend::iptables &&
        std::any_of(route_rule_lists(route_rule).begin(),
                    route_rule_lists(route_rule).end(), [&](const auto& list_name) {
                      return context.lists.find(list_name) != context.lists.end();
                    })) {
      throw FirewallError(
          "unsupported firewall construct: module_id=route.balance, rule=" +
          std::to_string(rule_index) +
          ", backend=iptables, construct=BalanceAction (requires nftables)");
    }

    // Preserve the prepared vector; nftables owns zero/one/many candidate
    // expansion, filtering, and fallback compilation.
    static const std::vector<FirewallBalanceCandidate> empty_candidates;
    const auto candidates = context.balance_candidates == nullptr
                                ? empty_candidates
                                : [&] {
                                    const auto it = context.balance_candidates->find(
                                        (*decision.outbound)->tag);
                                    return it == context.balance_candidates->end()
                                               ? empty_candidates
                                               : it->second;
                                  }();
    register_route_rule_targets(
        context, registrar, id(), rule_index,
        [&] {
          return FirewallRuleAction{
              BalanceAction{mark_it->second, candidates}};
        });
  }
}

namespace {

void register_mark_rules(const FirewallBuildContext& context,
                         FirewallRuleRegistrar& registrar) {
  RouteMarkRuleModule{}.register_rules(context, registrar);
}

void register_drop_rules(const FirewallBuildContext& context,
                         FirewallRuleRegistrar& registrar) {
  RouteDropRuleModule{}.register_rules(context, registrar);
}

void register_pass_rules(const FirewallBuildContext& context,
                         FirewallRuleRegistrar& registrar) {
  RoutePassRuleModule{}.register_rules(context, registrar);
}

void register_balance_rules(const FirewallBuildContext& context,
                            FirewallRuleRegistrar& registrar) {
  RouteBalanceRuleModule{}.register_rules(context, registrar);
}

} // namespace

namespace {

void register_dns_detour_rules(const FirewallBuildContext& context,
                               FirewallRuleRegistrar& registrar) {
  DnsDetourRuleModule{}.register_rules(context, registrar);
}

void register_restore_conntrack_mark_rules(const FirewallBuildContext& context,
                                           FirewallRuleRegistrar& registrar) {
  RestoreConntrackMarkRuleModule{}.register_rules(context, registrar);
}

void register_skip_established_or_dnat_rules(
    const FirewallBuildContext& context, FirewallRuleRegistrar& registrar) {
  SkipEstablishedOrDnatRuleModule{}.register_rules(context, registrar);
}

void register_skip_marked_packets_rules(const FirewallBuildContext& context,
                                        FirewallRuleRegistrar& registrar) {
  SkipMarkedPacketsRuleModule{}.register_rules(context, registrar);
}

void register_inbound_interface_filter_rules(
    const FirewallBuildContext& context, FirewallRuleRegistrar& registrar) {
  InboundInterfaceFilterRuleModule{}.register_rules(context, registrar);
}

} // namespace

std::array<RouteRuleModuleRegistration, 9> route_rule_module_manifest() {
  return {register_restore_conntrack_mark_rules,
          register_skip_established_or_dnat_rules,
          register_skip_marked_packets_rules,
          register_inbound_interface_filter_rules,
          register_mark_rules,
          register_drop_rules,
          register_pass_rules,
          register_balance_rules,
          register_dns_detour_rules};
}

} // namespace keen_pbr3
