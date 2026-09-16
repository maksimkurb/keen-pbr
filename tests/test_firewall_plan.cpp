#include <doctest/doctest.h>

#include "../src/firewall/firewall_plan.hpp"
#include "../src/firewall/firewall_runtime.hpp"
#include "../src/lists/list_entry_visitor.hpp"

#include <algorithm>
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

  std::unique_ptr<ListEntryVisitor>
  create_batch_loader(const std::string&) override {
    throw std::runtime_error("unexpected set streaming");
  }

  void apply(const FirewallPlan& plan,
             FirewallApplyMode = FirewallApplyMode::Destructive) override {
    set_fwmark_mask(plan.fwmark_mask);
    for (const auto& current : plan.rules) {
      if (const auto* mark = std::get_if<MarkAction>(&current.action)) {
        seen_mark = mark->value;
        seen_key = current.key;
        seen_criteria = materialize(current.criteria);
        replayed.push_back(seen_criteria.apply_output ? "dns" : "route");
      } else if (const auto* balance =
                     std::get_if<BalanceAction>(&current.action)) {
        seen_mark = balance->fallback_mark;
        seen_candidates = balance->candidates;
        seen_key = current.key;
        seen_criteria = materialize(current.criteria);
        replayed.push_back(seen_criteria.apply_output ? "dns" : "route");
      } else if (const auto* verdict = std::get_if<VerdictAction>(&current.action)) {
        if (*verdict == VerdictAction::drop || *verdict == VerdictAction::pass) {
          const auto criteria = materialize(current.criteria);
          replayed.push_back(criteria.apply_output ? "dns" : "route");
        }
      }
    }
  }
  void cleanup() override {}
  FirewallBackend backend() const override { return FirewallBackend::nftables; }

  uint32_t seen_mark{0};
  FirewallRuleKey seen_key;
  FirewallRuleCriteria seen_criteria;
  std::vector<FirewallBalanceCandidate> seen_candidates;
  std::vector<std::string> replayed;

private:
  FirewallRuleCriteria materialize(FirewallRuleCriteria criteria) const {
    if (!criteria.dst_set_name.has_value()) {
      return criteria;
    }
    if (criteria.dst_set_name->rfind("kpbr4_", 0) == 0) {
      criteria.dst_set_name = "kpbr4S_" + criteria.dst_set_name->substr(6);
    } else if (criteria.dst_set_name->rfind("kpbr6_", 0) == 0) {
      criteria.dst_set_name = "kpbr6S_" + criteria.dst_set_name->substr(6);
    }
    return criteria;
  }
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

TEST_CASE("Firewall backend plan entry point preserves actions") {
  FirewallPlan plan;
  plan.fwmark_mask = 0xFF00U;
  FirewallRuleRegistrar registrar(plan);
  auto marked = rule("route.mark", "remote", FirewallRuleStage::route_classification, 0);
  marked.action = MarkAction{0x100U, 0xFF00U};
  marked.criteria.dst_set_name = "kpbr4_remote";
  registrar.register_rule(marked);
  registrar.finish();

  PlanFirewall firewall;
  firewall.apply(plan);
  CHECK(firewall.seen_mark == 0x100U);
  CHECK(firewall.seen_key == marked.key);
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

  REQUIRE(plan.rules.size() == 6);
  CHECK(std::holds_alternative<RestoreConntrackMarkAction>(plan.rules[0].action));
  CHECK(std::holds_alternative<SkipEstablishedOrDnatAction>(plan.rules[1].action));
  CHECK(std::holds_alternative<SkipMarkedPacketsAction>(plan.rules[2].action));
  CHECK(plan.rules[3].source_rule_index == 0);
  CHECK(plan.rules[4].source_rule_index == 1);
  CHECK(plan.rules[5].source_rule_index == 2);
  CHECK(std::holds_alternative<MarkAction>(plan.rules[3].action));
  CHECK(std::get<VerdictAction>(plan.rules[4].action) == VerdictAction::drop);
  CHECK(std::get<VerdictAction>(plan.rules[5].action) == VerdictAction::pass);
}

TEST_CASE("empty nft mark ownership omits restore while iptables preserves it") {
  const Config config = parse_config(R"({
    "daemon": {"firewall_backend":"nftables"}
  })");
  const OutboundMarkMap no_marks;
  const std::map<std::string, ListSetUsage> list_usage;
  const std::vector<DumpedRoute> routes;
  const std::vector<DumpedInterface> interfaces;

  const auto build = [&](FirewallBackend backend) {
    return build_firewall_plan({config, no_marks, list_usage, routes, interfaces,
                                 nullptr, true, 0xFFFFFFFFU, nullptr, backend});
  };
  const auto nft_plan = build(FirewallBackend::nftables);
  CHECK(std::none_of(
      nft_plan.rules.begin(), nft_plan.rules.end(), [](const auto& rule) {
        return std::holds_alternative<RestoreConntrackMarkAction>(rule.action);
      }));

  const auto iptables_plan = build(FirewallBackend::iptables);
  REQUIRE(!iptables_plan.rules.empty());
  CHECK(std::holds_alternative<RestoreConntrackMarkAction>(
      iptables_plan.rules.front().action));
}

TEST_CASE("backend plan order keeps routes before DNS detours") {
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

  REQUIRE(plan.rules.size() == 6);
  CHECK(plan.rules[0].source_rule_index ==
        std::numeric_limits<std::size_t>::max());
  CHECK(plan.rules[1].source_rule_index ==
        std::numeric_limits<std::size_t>::max());
  CHECK(plan.rules[2].source_rule_index ==
        std::numeric_limits<std::size_t>::max());
  CHECK(plan.rules[3].source_rule_index == 0);
  CHECK(plan.rules[4].source_rule_index ==
        std::numeric_limits<std::size_t>::max());
  CHECK(plan.rules[5].source_rule_index ==
        std::numeric_limits<std::size_t>::max());
  CHECK(plan.rules[3].stage == FirewallRuleStage::route_classification);
  CHECK(plan.rules[4].stage == FirewallRuleStage::route_classification);
  CHECK(plan.rules[5].stage == FirewallRuleStage::route_classification);
  CHECK(plan.rules[3].priority < plan.rules[4].priority);
  CHECK(plan.rules[4].priority < plan.rules[5].priority);
  CHECK(plan.rules[4].criteria.proto == L4Proto::Tcp);
  CHECK(plan.rules[5].criteria.proto == L4Proto::Udp);

  PlanFirewall firewall;
  firewall.apply(plan);
  CHECK(firewall.replayed == std::vector<std::string>{"route", "dns", "dns"});
}

TEST_CASE("DNS detour plan preserves configured order for equivalent IPv6 endpoints") {
  Config config = parse_config(R"({
    "daemon": {"ipv6_enabled": true},
    "outbounds": [
      {"type":"table","tag":"route_z","table":100},
      {"type":"table","tag":"route_a","table":101}
    ],
    "dns": {"servers":[
      {"tag":"upstream_z","address":"[2001:db8::53]:5353",
       "detour":"route_z"},
      {"tag":"upstream_a","address":"[2001:0db8::53]:5353",
       "detour":"route_a"}
    ]}
  })");
  const std::map<std::string, ListSetUsage> list_usage;
  const OutboundMarkMap marks{
      {internal_detour_mark_key("route_z"), 0x300U},
      {internal_detour_mark_key("route_a"), 0x200U}};
  const auto build = [&](const Config& candidate) {
    const FirewallPlanBuildInputs inputs{
        candidate, marks, list_usage, {}, {}, nullptr, true, 0xFFFFFFFFU};
    return build_firewall_plan(inputs);
  };

  const auto first = build(config);
  REQUIRE(first.rules.size() == 7);
  CHECK(first.rules[3].criteria.dst_addr ==
        std::vector<std::string>{"2001:db8::53"});
  CHECK(first.rules[3].criteria.proto == L4Proto::Tcp);
  CHECK(std::get<MarkAction>(first.rules[3].action).value == 0x300U);
  CHECK(first.rules[4].criteria.proto == L4Proto::Udp);
  CHECK(first.rules[5].criteria.dst_addr ==
        std::vector<std::string>{"2001:0db8::53"});
  CHECK(first.rules[5].criteria.proto == L4Proto::Tcp);
  CHECK(std::get<MarkAction>(first.rules[5].action).value == 0x200U);
  CHECK(first.rules[6].criteria.proto == L4Proto::Udp);

  std::reverse(config.dns->servers->begin(), config.dns->servers->end());
  const auto reversed = build(config);
  REQUIRE(reversed.rules.size() == first.rules.size());
  CHECK(reversed.rules[3].criteria.dst_addr ==
        std::vector<std::string>{"2001:0db8::53"});
  CHECK(reversed.rules[3].criteria.proto == L4Proto::Tcp);
  CHECK(std::get<MarkAction>(reversed.rules[3].action).value == 0x200U);
  CHECK(reversed.rules[4].criteria.proto == L4Proto::Udp);
  CHECK(reversed.rules[5].criteria.dst_addr ==
        std::vector<std::string>{"2001:db8::53"});
  CHECK(reversed.rules[5].criteria.proto == L4Proto::Tcp);
  CHECK(std::get<MarkAction>(reversed.rules[5].action).value == 0x300U);
  CHECK(reversed.rules[6].criteria.proto == L4Proto::Udp);
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

  REQUIRE(plan.rules.size() == 6);
  CHECK(plan.rules[3].family == FirewallFamily::any);
  CHECK(plan.rules[4].family == FirewallFamily::ipv4);
  CHECK(plan.rules[5].family == FirewallFamily::ipv6);
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
  firewall.apply(plan);
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
