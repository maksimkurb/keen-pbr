#include "firewall_plan.hpp"

#include <algorithm>
#include <stdexcept>

namespace keen_pbr3 {

namespace {

void validate_firewall_rule(const FirewallRuleInstance& rule,
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
  } else if (const auto* restore =
                 std::get_if<RestoreConntrackMarkAction>(&rule.action)) {
    if (restore->mask == 0 || restore->mask != fwmark_mask) {
      throw std::invalid_argument(
          "firewall rule has an invalid conntrack mark mask");
    }
  } else if (const auto* inbound =
                 std::get_if<InboundInterfaceFilterAction>(&rule.action)) {
    if (inbound->interfaces.empty() ||
        std::any_of(inbound->interfaces.begin(), inbound->interfaces.end(),
                    [](const std::string& interface) { return interface.empty(); })) {
      throw std::invalid_argument(
          "firewall rule has an invalid inbound interface filter");
    }
  }
}

} // namespace

void FirewallRuleRegistrar::validate_rule(const FirewallRuleInstance& rule,
                                          uint32_t fwmark_mask) {
  validate_firewall_rule(rule, fwmark_mask);
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

void validate_firewall_plan_backend(const FirewallPlan& plan,
                                    FirewallBackend backend) {
  std::set<std::pair<std::string, std::string>> keys;
  std::map<std::string, FirewallSetDeclaration> sets;
  for (const auto& rule : plan.rules) {
    validate_firewall_rule(rule, plan.fwmark_mask);
    if (!keys.emplace(rule.key.module_id, rule.key.instance_id).second) {
      throw std::invalid_argument("duplicate firewall rule key: " +
                                  rule.key.module_id + ":" +
                                  rule.key.instance_id);
    }
  }
  for (const auto& declaration : plan.sets) {
    if (declaration.name.empty()) {
      throw std::invalid_argument("firewall set name must not be empty");
    }
    if (declaration.family != FirewallFamily::ipv4 &&
        declaration.family != FirewallFamily::ipv6) {
      throw std::invalid_argument("firewall set has an invalid family");
    }
    const auto [it, inserted] = sets.emplace(declaration.name, declaration);
    if (!inserted && (it->second.family != declaration.family ||
                      it->second.timeout != declaration.timeout)) {
      throw std::invalid_argument("conflicting firewall set declaration: " +
                                  declaration.name);
    }
  }

  const auto reject = [backend](const FirewallRuleInstance& rule,
                                const char* construct) {
    throw FirewallError(
        "unsupported firewall construct: module_id=" + rule.key.module_id +
        ", instance_id=" + rule.key.instance_id +
        ", backend=" + firewall_backend_name(backend) +
        ", construct=" + construct + " (requires nftables)");
  };

  for (const auto& rule : plan.rules) {
    if (std::holds_alternative<BalanceAction>(rule.action) &&
        backend != FirewallBackend::nftables) {
      reject(rule, "BalanceAction");
    }
    if (rule.criteria.default_gateway != DefaultGatewayFamily::None &&
        backend != FirewallBackend::nftables) {
      reject(rule, "default_gateway");
    }
  }
}

} // namespace keen_pbr3
