#pragma once

#include "firewall_rule.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace keen_pbr3 {

// Set names in a plan are stable logical references (kpbr4_*/kpbr6_* and
// their dynamic counterparts). Backends resolve them to attempt-specific
// physical names while compiling the plan.
struct FirewallSetDeclaration {
  std::string name;
  FirewallFamily family{FirewallFamily::ipv4};
  uint32_t timeout{0};
};

struct FirewallPlan {
  std::vector<FirewallRuleInstance> rules;
  std::vector<FirewallSetDeclaration> sets;
  uint32_t fwmark_mask{0xFFFFFFFFu};
};

class FirewallRuleRegistrar {
public:
  explicit FirewallRuleRegistrar(FirewallPlan& plan) : plan_(plan) {}

  void register_rule(FirewallRuleInstance rule);
  void register_set(FirewallSetDeclaration declaration);
  void finish();

private:
  static void validate_rule(const FirewallRuleInstance& rule,
                            uint32_t fwmark_mask);

  FirewallPlan& plan_;
  std::set<std::pair<std::string, std::string>> keys_;
  std::map<std::string, FirewallSetDeclaration> sets_;
  std::size_t next_insertion_order_{0};
  bool finished_{false};
};

// Validate the complete desired plan before a backend mutates pending or live
// firewall state, including backend-specific capability checks.
void validate_firewall_plan_backend(const FirewallPlan& plan,
                                    FirewallBackend backend);

} // namespace keen_pbr3
