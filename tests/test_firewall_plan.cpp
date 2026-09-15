#include <doctest/doctest.h>

#include "../src/firewall/firewall_plan.hpp"
#include "../src/firewall/firewall_runtime.hpp"
#include "../src/lists/list_entry_visitor.hpp"

#include <memory>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace keen_pbr3 {
namespace {

class PlanFirewall final : public Firewall {
public:
  std::string static_set_name(const std::string& list_name,
                              int family) const override {
    return std::string(family == AF_INET6 ? "kpbr6S_" : "kpbr4S_") + list_name;
  }

  void create_ipset(const std::string&, int, uint32_t) override {}

  void create_mark_rule(uint32_t mark,
                        const FirewallRuleCriteria& criteria) override {
    seen_mark = mark;
    seen_criteria = criteria;
    replayed.push_back(criteria.apply_output ? "dns" : "route");
  }

  void create_balance_rule(
      uint32_t mark, const std::vector<FirewallBalanceCandidate>& candidates,
      const FirewallRuleCriteria& criteria) override {
    seen_mark = mark;
    seen_candidates = candidates;
    seen_criteria = criteria;
    replayed.push_back(criteria.apply_output ? "dns" : "route");
  }

  void create_drop_rule(const FirewallRuleCriteria& criteria) override {
    replayed.push_back(criteria.apply_output ? "dns" : "route");
  }
  void create_pass_rule(const FirewallRuleCriteria& criteria) override {
    replayed.push_back(criteria.apply_output ? "dns" : "route");
  }

  std::unique_ptr<ListEntryVisitor>
  create_batch_loader(const std::string&) override {
    throw std::runtime_error("unexpected set streaming");
  }

  void apply(FirewallApplyMode) override {}
  void cleanup() override {}
  FirewallBackend backend() const override { return FirewallBackend::nftables; }

  uint32_t seen_mark{0};
  FirewallRuleCriteria seen_criteria;
  std::vector<FirewallBalanceCandidate> seen_candidates;
  std::vector<std::string> replayed;
};

FirewallRuleInstance rule(const char* module, const char* instance,
                          FirewallRuleStage stage, int priority) {
  FirewallRuleInstance result;
  result.key = {module, instance};
  result.stage = stage;
  result.priority = priority;
  result.action = VerdictAction::pass;
  return result;
}

} // namespace

TEST_CASE("FirewallRuleRegistrar assigns stable insertion order and sorts") {
  FirewallPlan plan;
  FirewallRuleRegistrar registrar(plan);
  registrar.register_rule(rule("route", "late", FirewallRuleStage::terminal, 0));
  registrar.register_rule(rule("route", "first", FirewallRuleStage::route_classification, 10));
  registrar.register_rule(rule("route", "second", FirewallRuleStage::route_classification, 10));
  registrar.finish();

  REQUIRE(plan.rules.size() == 3);
  CHECK(plan.rules[0].key.instance_id == "first");
  CHECK(plan.rules[1].key.instance_id == "second");
  CHECK(plan.rules[2].key.instance_id == "late");
  CHECK(plan.rules[0].insertion_order == 1);
  CHECK(plan.rules[1].insertion_order == 2);
  CHECK(plan.rules[2].insertion_order == 0);
}

TEST_CASE("FirewallRuleRegistrar rejects duplicate keys and invalid placement") {
  FirewallPlan plan;
  FirewallRuleRegistrar registrar(plan);
  registrar.register_rule(rule("route", "same", FirewallRuleStage::terminal, 0));
  CHECK_THROWS_AS(
      registrar.register_rule(rule("route", "same", FirewallRuleStage::terminal, 1)),
      std::invalid_argument);

  FirewallPlan invalid_plan;
  FirewallRuleRegistrar invalid_registrar(invalid_plan);
  auto invalid = rule("route", "output", FirewallRuleStage::terminal, 0);
  invalid.hook = FirewallHook::output;
  CHECK_THROWS_AS(invalid_registrar.register_rule(std::move(invalid)),
                  std::invalid_argument);
}

TEST_CASE("FirewallRuleRegistrar deduplicates identical sets and rejects conflicts") {
  FirewallPlan plan;
  FirewallRuleRegistrar registrar(plan);
  registrar.register_set({"kpbr4_remote", FirewallFamily::ipv4, 0});
  registrar.register_set({"kpbr4_remote", FirewallFamily::ipv4, 0});
  registrar.register_set({"kpbr6_remote", FirewallFamily::ipv6, 30});
  CHECK_THROWS_AS(
      registrar.register_set({"kpbr4_remote", FirewallFamily::ipv4, 30}),
      std::invalid_argument);
  registrar.finish();

  REQUIRE(plan.sets.size() == 2);
  CHECK(plan.sets[0].name == "kpbr4_remote");
  CHECK(plan.sets[1].name == "kpbr6_remote");
}

TEST_CASE("Firewall plan adapter resolves logical sets and preserves actions") {
  FirewallPlan plan;
  plan.fwmark_mask = 0xFF00U;
  FirewallRuleRegistrar registrar(plan);
  auto marked = rule("route.mark", "remote", FirewallRuleStage::route_classification, 0);
  marked.action = MarkAction{0x100U, 0xFF00U};
  marked.criteria.dst_set_name = "kpbr4_remote";
  registrar.register_rule(marked);
  registrar.finish();

  PlanFirewall firewall;
  replay_firewall_plan(plan, firewall);
  CHECK(firewall.seen_mark == 0x100U);
  CHECK(firewall.seen_criteria.dst_set_name == "kpbr4S_remote");
  CHECK(firewall.fwmark_mask() == 0xFF00U);
}

TEST_CASE("build_firewall_plan keeps route config order in canonical output") {
  const Config config = parse_config(R"({
    "daemon": {"ipv6_enabled": false},
    "outbounds": [
      {"type":"table","tag":"wan","table":100},
      {"type":"blackhole","tag":"blocked"},
      {"type":"ignore","tag":"direct"}
    ],
    "route": {"rules": [
      {"outbound":"wan","dscp":46},
      {"outbound":"blocked","dscp":47},
      {"outbound":"direct","dscp":48}
    ]}
  })");
  const std::map<std::string, ListSetUsage> list_usage;
  const FirewallPlanBuildInputs inputs{
      config, { {"wan", 0x100U} }, list_usage, {}, {}, nullptr, false,
      0x00FF0000U};
  const auto plan = build_firewall_plan(inputs);

  REQUIRE(plan.rules.size() == 3);
  CHECK(plan.rules[0].source_rule_index == 0);
  CHECK(plan.rules[1].source_rule_index == 1);
  CHECK(plan.rules[2].source_rule_index == 2);
  CHECK(std::holds_alternative<MarkAction>(plan.rules[0].action));
  CHECK(std::get<VerdictAction>(plan.rules[1].action) == VerdictAction::drop);
  CHECK(std::get<VerdictAction>(plan.rules[2].action) == VerdictAction::pass);
}

TEST_CASE("build and replay order keeps routes before DNS detours") {
  const Config config = parse_config(R"({
    "daemon": {"ipv6_enabled": false},
    "outbounds": [{"type":"table","tag":"wan","table":100}],
    "route": {"rules": [{"outbound":"wan","dscp":46}]},
    "dns": {"servers":[{"tag":"upstream","address":"192.0.2.53:5353",
                           "detour":"wan"}]}
  })");
  const std::map<std::string, ListSetUsage> list_usage;
  const FirewallPlanBuildInputs inputs{
      config,
      {{"wan", 0x100U}, {internal_detour_mark_key("wan"), 0x200U}},
      list_usage,
      {},
      {},
      nullptr,
      false,
      0x00FF0000U};
  const auto plan = build_firewall_plan(inputs);

  REQUIRE(plan.rules.size() == 2);
  CHECK(plan.rules[0].source_rule_index == 0);
  CHECK(plan.rules[1].source_rule_index ==
        std::numeric_limits<std::size_t>::max());
  CHECK(plan.rules[0].stage == FirewallRuleStage::route_classification);
  CHECK(plan.rules[1].stage == FirewallRuleStage::route_classification);
  CHECK(plan.rules[0].priority < plan.rules[1].priority);

  PlanFirewall firewall;
  replay_firewall_plan(plan, firewall);
  CHECK(firewall.replayed == std::vector<std::string>{"route", "dns"});
}

TEST_CASE("build_firewall_plan preserves truthful rule families") {
  const Config config = parse_config(R"({
    "daemon": {"ipv6_enabled": true},
    "outbounds": [{"type":"table","tag":"wan","table":100}],
    "route": {"rules": [
      {"outbound":"wan","dscp":46},
      {"outbound":"wan","dest_addr":"192.0.2.1/32"},
      {"outbound":"wan","dest_addr":"2001:db8::1/128"}
    ]}
  })");
  const std::map<std::string, ListSetUsage> list_usage;
  const FirewallPlanBuildInputs inputs{
      config, {{"wan", 0x100U}}, list_usage, {}, {}, nullptr, true,
      0x00FF0000U};
  const auto plan = build_firewall_plan(inputs);

  REQUIRE(plan.rules.size() == 3);
  CHECK(plan.rules[0].family == FirewallFamily::any);
  CHECK(plan.rules[1].family == FirewallFamily::ipv4);
  CHECK(plan.rules[2].family == FirewallFamily::ipv6);
}

TEST_CASE("balance action keeps complete candidate availability") {
  FirewallPlan plan;
  plan.fwmark_mask = 0xFF00U;
  FirewallRuleRegistrar registrar(plan);
  auto balanced = rule("route.balance", "wan", FirewallRuleStage::route_classification,
                       0);
  balanced.action = BalanceAction{
      0x100U, {{0x200U, true, false}, {0x300U, false, true}}};
  registrar.register_rule(balanced);
  registrar.finish();

  PlanFirewall firewall;
  replay_firewall_plan(plan, firewall);
  CHECK(firewall.seen_mark == 0x100U);
  REQUIRE(firewall.seen_candidates.size() == 2);
  CHECK(firewall.seen_candidates[0] == FirewallBalanceCandidate{0x200U, true, false});
  CHECK(firewall.seen_candidates[1] == FirewallBalanceCandidate{0x300U, false, true});
}

TEST_CASE("backend validation accepts nftables-only constructs on nftables") {
  FirewallPlan plan;
  auto balanced = rule("route.balance", "balance", FirewallRuleStage::route_classification,
                       0);
  balanced.action = BalanceAction{0x100U, {{0x200U, true, true}}};
  plan.rules.push_back(balanced);

  auto gateway = rule("route.mark", "gateway", FirewallRuleStage::route_classification,
                      1);
  gateway.hook = FirewallHook::output;
  gateway.family = FirewallFamily::ipv4;
  gateway.criteria.apply_output = true;
  gateway.criteria.default_gateway = DefaultGatewayFamily::Ipv4;
  gateway.action = MarkAction{0x100U, 0xFFFFFFFFU};
  plan.rules.push_back(gateway);

  CHECK_NOTHROW(
      validate_firewall_plan_backend(plan, FirewallBackend::nftables));
}

TEST_CASE("backend validation rejects unsupported construct with stable detail") {
  FirewallPlan plan;
  auto balanced = rule("route.balance", "balance", FirewallRuleStage::route_classification,
                       0);
  balanced.action = BalanceAction{0x100U, {{0x200U, true, true}}};
  plan.rules.push_back(std::move(balanced));

  CHECK_THROWS_WITH(
      validate_firewall_plan_backend(plan, FirewallBackend::iptables),
      "unsupported firewall construct: module_id=route.balance, instance_id=balance, backend=iptables, construct=BalanceAction (requires nftables)");

  FirewallPlan gateway_plan;
  auto gateway = rule("route.mark", "gateway", FirewallRuleStage::route_classification,
                      0);
  gateway.hook = FirewallHook::output;
  gateway.family = FirewallFamily::ipv4;
  gateway.criteria.apply_output = true;
  gateway.criteria.default_gateway = DefaultGatewayFamily::Ipv4;
  gateway_plan.rules.push_back(std::move(gateway));
  CHECK_THROWS_WITH(
      validate_firewall_plan_backend(gateway_plan, FirewallBackend::iptables),
      "unsupported firewall construct: module_id=route.mark, instance_id=gateway, backend=iptables, construct=default_gateway (requires nftables)");
}

} // namespace keen_pbr3
