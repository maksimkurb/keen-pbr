#include <doctest/doctest.h>

#include "../src/firewall/firewall_rule_modules.hpp"
#include "../src/firewall/firewall_runtime.hpp"
#include "../src/firewall/firewall_snapshot.hpp"

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
            0xFFFFFFFFU, nullptr, nullptr, true, true, true, {}};
  }
};

struct BalanceModuleFixture {
  Config config = parse_config(R"({
    "outbounds": [
      {"type":"table","tag":"wan_a","table":100},
      {"type":"table","tag":"wan_b","table":101},
      {"type":"urltest","tag":"auto","strategy":"balance",
       "outbound_groups":[{"outbounds":["wan_a","wan_b"]}]}
    ],
    "route": {"rules": [{"outbound":"auto","dscp":46}]}
  })");
  const std::vector<RuleState> states =
      build_fw_rule_states(config, {{"auto", 0x100U}});
  const std::map<std::string, ListConfig> lists;
  const std::map<std::string, ListSetUsage> usage;
  const std::vector<DumpedRoute> main_routes;
  const std::vector<DumpedInterface> interfaces;
  FirewallBalanceCandidates candidates;

  FirewallBuildContext context() const {
    return {*config.route->rules, states, *config.outbounds, lists, usage,
            main_routes, interfaces, FirewallBackend::nftables, true,
            0xFFFFFFFFU, &candidates, nullptr, true, true, true, {}};
  }
};

template <typename Module, typename Fixture>
FirewallPlan build_module_plan(const Module& module, const Fixture& fixture) {
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

  REQUIRE(plan.rules.size() == 10);
  CHECK(plan.rules[0].key.module_id == "prefilter.restore_conntrack_mark");
  CHECK(plan.rules[1].key.module_id == "prefilter.skip_established_or_dnat");
  CHECK(plan.rules[2].key.module_id == "prefilter.skip_marked_packets");
  CHECK(plan.rules[3].source_rule_index == 0);
  CHECK(plan.rules[4].source_rule_index == 1);
  CHECK(plan.rules[5].source_rule_index == 2);
  CHECK(plan.rules[6].source_rule_index == 3);
  CHECK(plan.rules[7].source_rule_index == 3);
  CHECK(plan.rules[8].source_rule_index == 3);
  CHECK(plan.rules[9].source_rule_index == 3);
  CHECK(plan.rules[3].key.module_id == "route.mark");
  CHECK(plan.rules[4].key.module_id == "route.drop");
  CHECK(plan.rules[5].key.module_id == "route.pass");
}

TEST_CASE("prefilter modules emit canonical operations and honor inputs") {
  const ModuleFixture fixture;
  auto context = fixture.context();
  context.fwmark_mask = 0x00FF0000U;
  context.inbound_interfaces = {"br-lan", "wg0"};

  const auto build = [&](auto module) {
    FirewallPlan plan;
    plan.fwmark_mask = context.fwmark_mask;
    FirewallRuleRegistrar registrar(plan);
    module.register_rules(context, registrar);
    registrar.finish();
    return plan;
  };

  const auto restore = build(RestoreConntrackMarkRuleModule{});
  REQUIRE(restore.rules.size() == 1);
  CHECK(restore.rules.front().stage == FirewallRuleStage::restore_conntrack);
  CHECK(restore.rules.front().priority == 0);
  CHECK(restore.rules.front().family == FirewallFamily::any);
  CHECK(std::get<RestoreConntrackMarkAction>(restore.rules.front().action).mask ==
        context.fwmark_mask);

  const auto dnat = build(SkipEstablishedOrDnatRuleModule{});
  REQUIRE(dnat.rules.size() == 1);
  CHECK(dnat.rules.front().stage == FirewallRuleStage::global_bypass);
  CHECK(dnat.rules.front().priority == 0);
  CHECK(std::holds_alternative<SkipEstablishedOrDnatAction>(
      dnat.rules.front().action));

  const auto marked = build(SkipMarkedPacketsRuleModule{});
  REQUIRE(marked.rules.size() == 1);
  CHECK(marked.rules.front().priority == 1);
  CHECK(std::holds_alternative<SkipMarkedPacketsAction>(
      marked.rules.front().action));

  const auto inbound = build(InboundInterfaceFilterRuleModule{});
  REQUIRE(inbound.rules.size() == 1);
  CHECK(inbound.rules.front().priority == 2);
  CHECK(std::get<InboundInterfaceFilterAction>(inbound.rules.front().action)
            .interfaces == context.inbound_interfaces);
  CHECK(inbound.rules.front().key.module_id ==
        "prefilter.inbound_interface");
  CHECK(inbound.rules.front().key == FirewallRuleKey::compact(
      "prefilter.inbound_interface", "br-lan;wg0;"));

  context.restore_conntrack_mark = false;
  context.skip_established_or_dnat = false;
  context.skip_marked_packets = false;
  context.inbound_interfaces.clear();
  CHECK(build(RestoreConntrackMarkRuleModule{}).rules.empty());
  CHECK(build(SkipEstablishedOrDnatRuleModule{}).rules.empty());
  CHECK(build(SkipMarkedPacketsRuleModule{}).rules.empty());
  CHECK(build(InboundInterfaceFilterRuleModule{}).rules.empty());
}

TEST_CASE("nft restore prefilter follows owned mark materialization") {
  const ModuleFixture fixture;
  auto context = fixture.context();
  context.restore_conntrack_mark = false;

  FirewallPlan nft_plan;
  FirewallRuleRegistrar nft_registrar(nft_plan);
  RestoreConntrackMarkRuleModule{}.register_rules(context, nft_registrar);
  nft_registrar.finish();
  CHECK(nft_plan.rules.empty());

  context.backend = FirewallBackend::iptables;
  context.restore_conntrack_mark = true;
  FirewallPlan iptables_plan;
  FirewallRuleRegistrar iptables_registrar(iptables_plan);
  RestoreConntrackMarkRuleModule{}.register_rules(context, iptables_registrar);
  iptables_registrar.finish();
  REQUIRE(iptables_plan.rules.size() == 1);
  CHECK(std::holds_alternative<RestoreConntrackMarkAction>(
      iptables_plan.rules.front().action));
}

TEST_CASE("route balance module preserves fallback and candidate ordering") {
  BalanceModuleFixture fixture;
  const std::vector<std::vector<FirewallBalanceCandidate>> candidate_cases = {
      {},
      {{0x200U, true, false}},
      {{0x300U, true, true}, {0x200U, true, false}, {0x400U, false, true}}};

  for (const auto& candidates : candidate_cases) {
    fixture.candidates["auto"] = candidates;
    const auto plan = build_module_plan(RouteBalanceRuleModule{}, fixture);
    REQUIRE(plan.rules.size() == 1);
    REQUIRE(std::holds_alternative<BalanceAction>(plan.rules.front().action));
    const auto& action = std::get<BalanceAction>(plan.rules.front().action);
    CHECK(action.fallback_mark == 0x100U);
    CHECK(action.candidates == candidates);
  }
}

TEST_CASE("route balance candidate changes retain identity and change semantics") {
  BalanceModuleFixture fixture;
  fixture.candidates["auto"] = {{0x200U, true, true}, {0x300U, false, true}};
  const auto first = build_module_plan(RouteBalanceRuleModule{}, fixture);

  fixture.candidates["auto"] = {{0x300U, false, true}, {0x400U, true, true}};
  const auto changed = build_module_plan(RouteBalanceRuleModule{}, fixture);

  REQUIRE(first.rules.size() == 1);
  REQUIRE(changed.rules.size() == 1);
  CHECK(changed.rules.front().key == first.rules.front().key);
  CHECK(changed.rules.front().action != first.rules.front().action);
}

TEST_CASE("route balance module is included in the explicit manifest") {
  BalanceModuleFixture fixture;
  fixture.candidates["auto"] = {{0x200U, true, true}};
  const auto context = fixture.context();
  FirewallPlan plan;
  FirewallRuleRegistrar registrar(plan);
  for (const auto register_module : route_rule_module_manifest()) {
    register_module(context, registrar);
  }
  registrar.finish();

  REQUIRE(plan.rules.size() == 4);
  CHECK(plan.rules[0].key.module_id == "prefilter.restore_conntrack_mark");
  CHECK(plan.rules[3].key.module_id == "route.balance");
}

TEST_CASE("DNS detour module emits family-specific TCP then UDP marks") {
  const ModuleFixture fixture;
  const std::vector<DnsDetourTarget> targets = {
      {"upstream", "wan", "192.0.2.53", 5353, FirewallFamily::ipv4,
       0x200U},
      {"upstream", "wan", "2001:db8::53", 5353, FirewallFamily::ipv6,
       0x200U}};
  auto context = fixture.context();
  context.dns_detour_targets = &targets;

  FirewallPlan plan;
  FirewallRuleRegistrar registrar(plan);
  DnsDetourRuleModule{}.register_rules(context, registrar);
  registrar.finish();

  REQUIRE(plan.rules.size() == 4);
  CHECK(plan.rules[0].family == FirewallFamily::ipv4);
  CHECK(plan.rules[0].criteria.dst_addr ==
        std::vector<std::string>{"192.0.2.53"});
  CHECK(plan.rules[0].criteria.proto == L4Proto::Tcp);
  CHECK(plan.rules[1].criteria.proto == L4Proto::Udp);
  CHECK(plan.rules[2].family == FirewallFamily::ipv6);
  CHECK(plan.rules[2].criteria.dst_addr ==
        std::vector<std::string>{"2001:db8::53"});
  CHECK(plan.rules[2].criteria.proto == L4Proto::Tcp);
  CHECK(plan.rules[3].criteria.proto == L4Proto::Udp);
  for (const auto& rule : plan.rules) {
    CHECK(rule.hook == FirewallHook::output);
    CHECK(rule.criteria.apply_output);
    CHECK(rule.criteria.dst_port == PortSpec("5353"));
    CHECK(std::holds_alternative<MarkAction>(rule.action));
    CHECK(std::get<MarkAction>(rule.action) == MarkAction{0x200U, 0xFFFFFFFFU});
  }
}

TEST_CASE("DNS detour module keeps duplicate endpoints and stable identities") {
  const ModuleFixture fixture;
  const auto build = [&](std::vector<DnsDetourTarget> targets) {
    auto context = fixture.context();
    context.dns_detour_targets = &targets;
    FirewallPlan plan;
    FirewallRuleRegistrar registrar(plan);
    DnsDetourRuleModule{}.register_rules(context, registrar);
    registrar.finish();
    return plan;
  };

  const std::vector<DnsDetourTarget> ordered = {
      {"upstream", "route_z", "2001:db8::53", 5353,
       FirewallFamily::ipv6, 0x300U},
      {"upstream", "route_a", "2001:0db8::53", 5353,
       FirewallFamily::ipv6, 0x200U},
      {"upstream", "route_a", "2001:0db8::53", 5353,
       FirewallFamily::ipv6, 0x200U}};
  auto reversed = ordered;
  std::reverse(reversed.begin(), reversed.end());
  const auto first = build(ordered);
  const auto second = build(reversed);

  REQUIRE(first.rules.size() == 6);
  REQUIRE(second.rules.size() == first.rules.size());
  CHECK(first.rules[0].criteria.dst_addr ==
        std::vector<std::string>{"2001:db8::53"});
  CHECK(first.rules[0].criteria.proto == L4Proto::Tcp);
  CHECK(std::get<MarkAction>(first.rules[0].action).value == 0x300U);
  CHECK(first.rules[1].criteria.proto == L4Proto::Udp);
  CHECK(first.rules[2].criteria.dst_addr ==
        std::vector<std::string>{"2001:0db8::53"});
  CHECK(first.rules[2].criteria.proto == L4Proto::Tcp);
  CHECK(std::get<MarkAction>(first.rules[2].action).value == 0x200U);
  CHECK(first.rules[3].criteria.proto == L4Proto::Udp);
  CHECK(first.rules[4].key != first.rules[2].key);

  // The first configured endpoint owns precedence even when its tag/address
  // sorts after the second one. Reversing config reverses that precedence.
  CHECK(second.rules[0].criteria.dst_addr ==
        std::vector<std::string>{"2001:0db8::53"});
  CHECK(second.rules[0].criteria.proto == L4Proto::Tcp);
  CHECK(std::get<MarkAction>(second.rules[0].action).value == 0x200U);
  CHECK(second.rules[1].criteria.proto == L4Proto::Udp);
  CHECK(second.rules[4].criteria.dst_addr ==
        std::vector<std::string>{"2001:db8::53"});
  CHECK(std::get<MarkAction>(second.rules[4].action).value == 0x300U);
  CHECK(first.rules[0].key == second.rules[4].key);
  CHECK(first.rules[2].key == second.rules[0].key);

  auto changed_target = ordered;
  changed_target[1].fwmark = 0x300U;
  const auto changed = build(changed_target);
  REQUIRE(changed.rules.size() == first.rules.size());
  CHECK(changed.rules[2].key == first.rules[2].key);
  CHECK(changed.rules[2].action != first.rules[2].action);
}

TEST_CASE("DNS detour module emits no rules for absent or invalid endpoints") {
  const ModuleFixture fixture;
  const auto build = [&](const std::vector<DnsDetourTarget>* targets) {
    auto context = fixture.context();
    context.dns_detour_targets = targets;
    FirewallPlan plan;
    FirewallRuleRegistrar registrar(plan);
    DnsDetourRuleModule{}.register_rules(context, registrar);
    registrar.finish();
    return plan;
  };

  const auto absent = build(nullptr);
  CHECK(absent.rules.empty());
  const std::vector<DnsDetourTarget> invalid = {
      {"upstream", "wan", "", 0, FirewallFamily::any, 0}};
  const auto invalid_plan = build(&invalid);
  CHECK(invalid_plan.rules.empty());
}

TEST_CASE("health reports only the removed DNS physical instance as missing") {
  const ModuleFixture fixture;
  const std::vector<DnsDetourTarget> targets = {
      {"upstream", "wan", "192.0.2.53", 5353, FirewallFamily::ipv4,
       0x200U},
      {"upstream", "wan", "192.0.2.54", 5353, FirewallFamily::ipv4,
       0x200U}};
  auto context = fixture.context();
  context.dns_detour_targets = &targets;
  FirewallPlan plan;
  FirewallRuleRegistrar registrar(plan);
  DnsDetourRuleModule{}.register_rules(context, registrar);
  registrar.finish();

  FirewallSnapshot snapshot;
  snapshot.backend = FirewallBackend::nftables;
  snapshot.available = true;
  constexpr std::size_t removed = 1;
  for (std::size_t index = 0; index < plan.rules.size(); ++index) {
    if (index == removed) {
      continue;
    }
    const auto& expected = plan.rules[index];
    ObservedFirewallRule observed;
    observed.key = expected.key;
    observed.hook = expected.hook;
    observed.family = expected.family;
    observed.criteria = expected.criteria;
    observed.action = expected.action;
    snapshot.rules.push_back(std::move(observed));
  }

  const auto checks = verify_firewall_plan(plan, snapshot);
  REQUIRE(checks.size() == plan.rules.size());
  for (std::size_t index = 0; index < checks.size(); ++index) {
    CHECK(checks[index].status ==
          (index == removed ? CheckStatus::missing : CheckStatus::ok));
  }
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
      interfaces, FirewallBackend::nftables, true, 0xFFFFFFFFU, nullptr, nullptr,
      true, true, true, {}};
  FirewallPlan plan;
  FirewallRuleRegistrar registrar(plan);
  for (const auto register_module : route_rule_module_manifest()) {
    register_module(context, registrar);
  }
  registrar.finish();

  REQUIRE(plan.rules.size() == 6);
  CHECK(plan.rules[3].source_rule_index == 0);
  CHECK(plan.rules[4].source_rule_index == 1);
  CHECK(plan.rules[5].source_rule_index == 2);
  CHECK(std::holds_alternative<VerdictAction>(plan.rules[3].action));
  CHECK(std::holds_alternative<MarkAction>(plan.rules[4].action));
  CHECK(std::holds_alternative<VerdictAction>(plan.rules[5].action));
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
  REQUIRE(plan.rules.size() == 5);
  CHECK(plan.rules[3].criteria.dst_set_name == "kpbr4_remote");
  CHECK(plan.rules[4].criteria.dst_set_name == "kpbr4d_remote");
  for (const auto& rule : std::vector<FirewallRuleInstance>{plan.rules[3], plan.rules[4]}) {
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
  REQUIRE(plan.rules.size() == 5);
  CHECK(plan.rules[3].criteria.dst_set_name == "kpbr6_remote");
  CHECK(plan.rules[4].criteria.dst_set_name == "kpbr6d_remote");
  for (const auto& rule : std::vector<FirewallRuleInstance>{plan.rules[3], plan.rules[4]}) {
    CHECK(rule.family == FirewallFamily::ipv6);
    CHECK(rule.hook == FirewallHook::output);
    CHECK(rule.criteria.apply_output);
    CHECK(rule.criteria.default_gateway == DefaultGatewayFamily::Ipv6);
    CHECK(rule.criteria.proto == L4Proto::TcpUdp);
    CHECK(rule.criteria.dst_port == PortSpec("443"));
  }
}

} // namespace keen_pbr3
