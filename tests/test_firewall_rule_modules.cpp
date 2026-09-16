#include <doctest/doctest.h>

#include "../src/firewall/firewall_rule_modules.hpp"
#include "../src/firewall/firewall_runtime.hpp"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

namespace keen_pbr3 {
namespace {

struct ModuleFixture {
  Config config = parse_config(R"({
    "outbounds": [
      {"type":"table","tag":"wan","table":100},
      {"type":"blackhole","tag":"blocked"},
      {"type":"ignore","tag":"direct"}
    ],
    "lists": {"remote": {"ip_cidrs":["192.0.2.0/24"],
                            "domains":["example.test"]}},
    "route": {"rules": [
      {"outbound":"wan","dscp":46},
      {"outbound":"blocked","dscp":47},
      {"outbound":"direct","dscp":48},
      {"list":["remote"],"outbound":"wan"}
    ]}
  })");
  const std::vector<RuleState> states =
      build_fw_rule_states(config, {{"wan", 0x100U}});
  const std::map<std::string, ListSetUsage> usage = {
      {"remote", {true, true, 30}}};
  const std::vector<DumpedRoute> main_routes;
  const std::vector<DumpedInterface> interfaces;

  FirewallBuildContext context() const {
    return {*config.route->rules, states, *config.outbounds, *config.lists,
            usage, main_routes, interfaces, FirewallBackend::nftables, true,
            0xFFFFFFFFU};
  }
};

template <typename Module>
FirewallPlan build_module_plan(const Module& module, const ModuleFixture& fixture) {
  FirewallPlan plan;
  FirewallRuleRegistrar registrar(plan);
  const auto context = fixture.context();
  module.register_rules(context, registrar);
  registrar.finish();
  return plan;
}

} // namespace

TEST_CASE("route modules emit zero, one, and many canonical instances") {
  const ModuleFixture fixture;
  const auto mark_plan = build_module_plan(RouteMarkRuleModule{}, fixture);
  const auto drop_plan = build_module_plan(RouteDropRuleModule{}, fixture);
  const auto pass_plan = build_module_plan(RoutePassRuleModule{}, fixture);

  REQUIRE(mark_plan.rules.size() == 5);
  CHECK(drop_plan.rules.size() == 1);
  CHECK(pass_plan.rules.size() == 1);
  const auto static_target = std::find_if(
      mark_plan.rules.begin(), mark_plan.rules.end(), [](const auto& rule) {
        return rule.criteria.dst_set_name == "kpbr4_remote";
      });
  REQUIRE(static_target != mark_plan.rules.end());
  CHECK(static_target->family == FirewallFamily::ipv4);

  auto no_rules = fixture.config;
  no_rules.route->rules = {RouteRule{}};
  no_rules.route->rules->front().outbound = "wan";
  no_rules.route->rules->front().enabled = false;
  const auto no_states = build_fw_rule_states(no_rules, {{"wan", 0x100U}});
  const auto& no_routes = *no_rules.route->rules;
  const auto& no_outbounds = *no_rules.outbounds;
  const auto& no_lists = *no_rules.lists;
  const std::map<std::string, ListSetUsage> no_usage;
  const std::vector<DumpedRoute> no_main_routes;
  const std::vector<DumpedInterface> no_interfaces;
  const FirewallBuildContext no_context{
      no_routes, no_states, no_outbounds, no_lists, no_usage, no_main_routes,
      no_interfaces, FirewallBackend::nftables, true, 0xFFFFFFFFU};
  FirewallPlan no_plan;
  FirewallRuleRegistrar no_registrar(no_plan);
  RouteMarkRuleModule{}.register_rules(no_context, no_registrar);
  no_registrar.finish();
  CHECK(no_plan.rules.empty());
}

TEST_CASE("route module keys are stable and mark changes are semantic") {
  const ModuleFixture fixture;
  const auto first = build_module_plan(RouteMarkRuleModule{}, fixture);
  const auto second = build_module_plan(RouteMarkRuleModule{}, fixture);
  REQUIRE(first.rules.size() == second.rules.size());
  for (std::size_t index = 0; index < first.rules.size(); ++index) {
    CHECK(first.rules[index].key == second.rules[index].key);
  }

  const auto& routes = *fixture.config.route->rules;
  const auto& outbounds = *fixture.config.outbounds;
  const auto& lists = *fixture.config.lists;
  const auto changed_states =
      build_fw_rule_states(fixture.config, {{"wan", 0x200U}});
  const auto context = FirewallBuildContext{
      routes, changed_states, outbounds, lists, fixture.usage,
      fixture.main_routes, fixture.interfaces, FirewallBackend::nftables, true,
      0xFFFFFFFFU};
  FirewallPlan changed;
  FirewallRuleRegistrar registrar(changed);
  RouteMarkRuleModule{}.register_rules(context, registrar);
  registrar.finish();

  REQUIRE(changed.rules.size() == first.rules.size());
  CHECK(changed.rules.front().key == first.rules.front().key);
  CHECK(changed.rules.front().action != first.rules.front().action);
}

TEST_CASE("route module manifest has explicit deterministic order") {
  const ModuleFixture fixture;
  const auto context = fixture.context();
  FirewallPlan plan;
  FirewallRuleRegistrar registrar(plan);
  for (const auto register_module : route_rule_module_manifest()) {
    register_module(context, registrar);
  }
  registrar.finish();

  REQUIRE(plan.rules.size() == 7);
  CHECK(plan.rules[0].source_rule_index == 0);
  CHECK(plan.rules[1].source_rule_index == 1);
  CHECK(plan.rules[2].source_rule_index == 2);
  CHECK(plan.rules[3].source_rule_index == 3);
  CHECK(plan.rules[4].source_rule_index == 3);
  CHECK(plan.rules[5].source_rule_index == 3);
  CHECK(plan.rules[6].source_rule_index == 3);
  CHECK(plan.rules[0].key.module_id == "route.mark");
  CHECK(plan.rules[1].key.module_id == "route.drop");
  CHECK(plan.rules[2].key.module_id == "route.pass");
}

TEST_CASE("route module manifest keeps reordered config rules in priority order") {
  const Config config = parse_config(R"({
    "outbounds": [
      {"type":"table","tag":"wan","table":100},
      {"type":"blackhole","tag":"blocked"},
      {"type":"ignore","tag":"direct"}
    ],
    "route": {"rules": [
      {"outbound":"blocked","dscp":47},
      {"outbound":"wan","dscp":46},
      {"outbound":"direct","dscp":48}
    ]}
  })");
  const auto states = build_fw_rule_states(config, {{"wan", 0x100U}});
  const std::vector<DumpedRoute> main_routes;
  const std::vector<DumpedInterface> interfaces;
  const std::map<std::string, ListConfig> lists;
  const std::map<std::string, ListSetUsage> usage;
  const FirewallBuildContext context{
      *config.route->rules, states, *config.outbounds, lists, usage, main_routes,
      interfaces, FirewallBackend::nftables, true, 0xFFFFFFFFU};
  FirewallPlan plan;
  FirewallRuleRegistrar registrar(plan);
  for (const auto register_module : route_rule_module_manifest()) {
    register_module(context, registrar);
  }
  registrar.finish();

  REQUIRE(plan.rules.size() == 3);
  CHECK(plan.rules[0].source_rule_index == 0);
  CHECK(plan.rules[1].source_rule_index == 1);
  CHECK(plan.rules[2].source_rule_index == 2);
  CHECK(std::holds_alternative<VerdictAction>(plan.rules[0].action));
  CHECK(std::holds_alternative<MarkAction>(plan.rules[1].action));
  CHECK(std::holds_alternative<VerdictAction>(plan.rules[2].action));
}

namespace {

Config gateway_list_config(const char* gateway) {
  std::string document = R"({
    "daemon": {"firewall_backend":"nftables","ipv6_enabled":true},
    "outbounds": [{"type":"table","tag":"wan","table":100}],
    "lists": {"remote": {"ip_cidrs":["192.0.2.0/24"],
                             "domains":["example.test"]}},
    "route": {"rules": [{"list":["remote"],
                            "default_gateway":"gateway_value",
                            "proto":"tcp/udp","dest_port":"443",
                            "outbound":"wan"}]}
  })";
  const std::string placeholder = "gateway_value";
  document.replace(document.find(placeholder), placeholder.size(), gateway);
  return parse_config(document);
}

FirewallPlan build_gateway_list_plan(const char* gateway) {
  const auto config = gateway_list_config(gateway);
  const std::map<std::string, ListSetUsage> usage = {
      {"remote", {true, true, 30}}};
  const OutboundMarkMap marks = {{"wan", 0x100U}};
  return build_firewall_plan({config, marks, usage, {}, {}, nullptr, true,
                              0xFFFFFFFFU, nullptr,
                              FirewallBackend::nftables});
}

} // namespace

TEST_CASE("IPv4 default gateway keeps both list families but emits IPv4 rules") {
  const auto plan = build_gateway_list_plan("ipv4");

  REQUIRE(plan.sets.size() == 4);
  CHECK(plan.sets[0].name == "kpbr4_remote");
  CHECK(plan.sets[1].name == "kpbr4d_remote");
  CHECK(plan.sets[2].name == "kpbr6_remote");
  CHECK(plan.sets[3].name == "kpbr6d_remote");
  REQUIRE(plan.rules.size() == 2);
  CHECK(plan.rules[0].criteria.dst_set_name == "kpbr4_remote");
  CHECK(plan.rules[1].criteria.dst_set_name == "kpbr4d_remote");
  for (const auto& rule : plan.rules) {
    CHECK(rule.family == FirewallFamily::ipv4);
    CHECK(rule.hook == FirewallHook::output);
    CHECK(rule.criteria.apply_output);
    CHECK(rule.criteria.default_gateway == DefaultGatewayFamily::Ipv4);
    CHECK(rule.criteria.proto == L4Proto::TcpUdp);
    CHECK(rule.criteria.dst_port == PortSpec("443"));
  }
}

TEST_CASE("IPv6 default gateway keeps both list families but emits IPv6 rules") {
  const auto plan = build_gateway_list_plan("ipv6");

  REQUIRE(plan.sets.size() == 4);
  CHECK(plan.sets[0].name == "kpbr4_remote");
  CHECK(plan.sets[1].name == "kpbr4d_remote");
  CHECK(plan.sets[2].name == "kpbr6_remote");
  CHECK(plan.sets[3].name == "kpbr6d_remote");
  REQUIRE(plan.rules.size() == 2);
  CHECK(plan.rules[0].criteria.dst_set_name == "kpbr6_remote");
  CHECK(plan.rules[1].criteria.dst_set_name == "kpbr6d_remote");
  for (const auto& rule : plan.rules) {
    CHECK(rule.family == FirewallFamily::ipv6);
    CHECK(rule.hook == FirewallHook::output);
    CHECK(rule.criteria.apply_output);
    CHECK(rule.criteria.default_gateway == DefaultGatewayFamily::Ipv6);
    CHECK(rule.criteria.proto == L4Proto::TcpUdp);
    CHECK(rule.criteria.dst_port == PortSpec("443"));
  }
}

} // namespace keen_pbr3
