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
// their dynamic counterparts). The compatibility adapter resolves them to
// attempt-specific backend names after prepare_apply().
struct FirewallSetDeclaration {
  std::string name;
  FirewallFamily family{FirewallFamily::ipv4};
  uint32_t timeout{0};
};

struct FirewallPlan {
  std::vector<FirewallRuleInstance> rules;
  std::vector<FirewallSetDeclaration> sets;
  FirewallGlobalPrefilter global_prefilter;
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

class Firewall;

// Reject canonical constructs that the selected backend cannot compile before
// any compatibility adapter or backend lifecycle operation is invoked.
void validate_firewall_plan_backend(const FirewallPlan& plan,
                                    FirewallBackend backend);

// Replay one canonical rule through the legacy create_* API. The plan's
// logical set references are resolved using the backend's current generation.
void replay_firewall_rule(const FirewallRuleInstance& rule, Firewall& firewall);

// Apply plan-wide compatibility settings without replaying any rules.
void configure_firewall_plan(const FirewallPlan& plan, Firewall& firewall);

// Configure and replay a complete plan through the legacy Firewall API.
void replay_firewall_plan(const FirewallPlan& plan, Firewall& firewall);

} // namespace keen_pbr3
