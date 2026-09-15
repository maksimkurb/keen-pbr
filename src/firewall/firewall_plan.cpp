#include "firewall_plan.hpp"

#include "firewall.hpp"

#include <algorithm>
#include <stdexcept>

namespace keen_pbr3 {
namespace {

bool has_prefix(const std::string& value, const char* prefix) {
  return value.rfind(prefix, 0) == 0;
}

std::string resolve_set_name(const std::string& logical_name,
                             Firewall& firewall) {
  if (has_prefix(logical_name, "kpbr4d_")) {
    return firewall.dynamic_set_name(logical_name.substr(7), AF_INET);
  }
  if (has_prefix(logical_name, "kpbr6d_")) {
    return firewall.dynamic_set_name(logical_name.substr(7), AF_INET6);
  }
  if (has_prefix(logical_name, "kpbr4_")) {
    return firewall.static_set_name(logical_name.substr(6), AF_INET);
  }
  if (has_prefix(logical_name, "kpbr6_")) {
    return firewall.static_set_name(logical_name.substr(6), AF_INET6);
  }
  return logical_name;
}

FirewallRuleCriteria materialize_criteria(const FirewallRuleCriteria& criteria,
                                          Firewall& firewall) {
  FirewallRuleCriteria result = criteria;
  if (result.dst_set_name.has_value()) {
    result.dst_set_name = resolve_set_name(*result.dst_set_name, firewall);
  }
  return result;
}

} // namespace

void FirewallRuleRegistrar::validate_rule(const FirewallRuleInstance& rule,
                                          uint32_t fwmark_mask) {
  if (rule.key.module_id.empty() || rule.key.instance_id.empty()) {
    throw std::invalid_argument(
        "firewall rule module_id and instance_id must not be empty");
  }
  (void)rule.key.comment();
  if (rule.hook != FirewallHook::prerouting &&
      rule.hook != FirewallHook::output) {
    throw std::invalid_argument("firewall rule has an invalid hook");
  }
  if (rule.family != FirewallFamily::ipv4 &&
      rule.family != FirewallFamily::ipv6 &&
      rule.family != FirewallFamily::any) {
    throw std::invalid_argument("firewall rule has an invalid family");
  }
  if (rule.hook == FirewallHook::output) {
    if (!rule.criteria.apply_output) {
      throw std::invalid_argument(
          "output firewall rule must set criteria.apply_output");
    }
  } else if (rule.criteria.apply_output) {
    throw std::invalid_argument(
        "prerouting firewall rule cannot set criteria.apply_output");
  }

  if ((rule.criteria.default_gateway == DefaultGatewayFamily::Ipv4 &&
      rule.family != FirewallFamily::ipv4) ||
      (rule.criteria.default_gateway == DefaultGatewayFamily::Ipv6 &&
       rule.family != FirewallFamily::ipv6)) {
    throw std::invalid_argument(
        "firewall rule family is incompatible with default gateway");
  }

  if (const auto* mark = std::get_if<MarkAction>(&rule.action)) {
    if (mark->value == 0 || mark->mask == 0 || mark->mask != fwmark_mask) {
      throw std::invalid_argument("firewall rule has an invalid mark or mask");
    }
  } else if (const auto* balance = std::get_if<BalanceAction>(&rule.action)) {
    if (balance->fallback_mark == 0 ||
        std::any_of(balance->candidates.begin(), balance->candidates.end(),
                    [](const FirewallBalanceCandidate& candidate) {
                      return candidate.fwmark == 0;
                    })) {
      throw std::invalid_argument("firewall rule has an invalid balance mark");
    }
  }
}

void FirewallRuleRegistrar::register_rule(FirewallRuleInstance rule) {
  if (finished_) {
    throw std::logic_error("cannot register a firewall rule after finish");
  }
  validate_rule(rule, plan_.fwmark_mask);
  if (!keys_.emplace(rule.key.module_id, rule.key.instance_id).second) {
    throw std::invalid_argument("duplicate firewall rule key: " +
                                rule.key.module_id + ":" +
                                rule.key.instance_id);
  }
  rule.insertion_order = next_insertion_order_++;
  plan_.rules.push_back(std::move(rule));
}

void FirewallRuleRegistrar::register_set(FirewallSetDeclaration declaration) {
  if (finished_) {
    throw std::logic_error("cannot register a firewall set after finish");
  }
  if (declaration.name.empty()) {
    throw std::invalid_argument("firewall set name must not be empty");
  }
  if (declaration.family != FirewallFamily::ipv4 &&
      declaration.family != FirewallFamily::ipv6) {
    throw std::invalid_argument("firewall set has an invalid family");
  }
  const auto [it, inserted] = sets_.emplace(declaration.name, declaration);
  if (!inserted && (it->second.family != declaration.family ||
                    it->second.timeout != declaration.timeout)) {
    throw std::invalid_argument("conflicting firewall set declaration: " +
                                declaration.name);
  }
}

void FirewallRuleRegistrar::finish() {
  if (finished_) {
    return;
  }
  plan_.sets.clear();
  plan_.sets.reserve(sets_.size());
  for (const auto& [name, declaration] : sets_) {
    (void)name;
    plan_.sets.push_back(declaration);
  }
  std::stable_sort(plan_.rules.begin(), plan_.rules.end(),
                   [](const FirewallRuleInstance& left,
                      const FirewallRuleInstance& right) {
                     if (left.stage != right.stage) {
                       return left.stage < right.stage;
                     }
                     if (left.priority != right.priority) {
                       return left.priority < right.priority;
                     }
                     return left.insertion_order < right.insertion_order;
                   });
  finished_ = true;
}

void replay_firewall_rule(const FirewallRuleInstance& rule, Firewall& firewall) {
  const FirewallRuleCriteria criteria =
      materialize_criteria(rule.criteria, firewall);
  if (const auto* mark = std::get_if<MarkAction>(&rule.action)) {
    firewall.create_mark_rule(mark->value, criteria);
  } else if (const auto* balance = std::get_if<BalanceAction>(&rule.action)) {
    firewall.create_balance_rule(balance->fallback_mark, balance->candidates,
                                 criteria);
  } else if (std::get<VerdictAction>(rule.action) == VerdictAction::drop) {
    firewall.create_drop_rule(criteria);
  } else {
    firewall.create_pass_rule(criteria);
  }
}

void configure_firewall_plan(const FirewallPlan& plan, Firewall& firewall) {
  firewall.set_global_prefilter(plan.global_prefilter);
  firewall.set_fwmark_mask(plan.fwmark_mask);
}

void replay_firewall_plan(const FirewallPlan& plan, Firewall& firewall) {
  configure_firewall_plan(plan, firewall);
  for (const auto& rule : plan.rules) {
    replay_firewall_rule(rule, firewall);
  }
}

} // namespace keen_pbr3
