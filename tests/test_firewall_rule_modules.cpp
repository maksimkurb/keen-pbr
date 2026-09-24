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
  const OutboundMarkMap marks{{"wan", 0x100U}};
  const std::vector<RuleState> states = build_fw_rule_states(config, marks);
  const std::map<std::string, ListSetUsage> usage = {
      {"remote", {true, true, 30}}};
  const std::vector<DumpedRoute> main_routes;
  const std::vector<DumpedInterface> interfaces;

  FirewallBuildContext context() const {
    return {*config.route->rules, states, *config.outbounds, *config.lists,
            usage, main_routes, interfaces, FirewallBackend::nftables, true,
            0xFFFFFFFFU, nullptr, true, true, true, {}, &config, &marks};
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
  const OutboundMarkMap marks{{"auto", 0x100U}, {"wan_a", 0x200U},
                              {"wan_b", 0x300U}};
  const std::vector<RuleState> states = build_fw_rule_states(config, marks);
  const std::map<std::string, ListConfig> lists;
  const std::map<std::string, ListSetUsage> usage;
  const std::vector<DumpedRoute> main_routes;
  const std::vector<DumpedInterface> interfaces;
  FirewallBalanceCandidates candidates;

  FirewallBuildContext context() const {
    return {*config.route->rules, states, *config.outbounds, lists, usage,
            main_routes, interfaces, FirewallBackend::nftables, true,
            0xFFFFFFFFU, &candidates, true, true, true, {}, &config, &marks};
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

FirewallPlan build_plan(const Config& config, const OutboundMarkMap& marks,
                        bool ipv6_enabled = true) {
  const std::map<std::string, ListSetUsage> usage;
  const std::vector<DumpedRoute> main_routes;
  const std::vector<DumpedInterface> interfaces;
  return build_firewall_plan({config, marks, usage, main_routes, interfaces,
                              nullptr, ipv6_enabled, 0xFFFFFFFFU, nullptr,
                              FirewallBackend::nftables});
}

std::vector<const FirewallRuleInstance*> dns_rules(const FirewallPlan& plan) {
  std::vector<const FirewallRuleInstance*> rules;
  for (const auto& rule : plan.rules) {
    if (rule.key.module_id == "dns.detour") {
      rules.push_back(&rule);
    }
  }
  return rules;
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

  const OutboundMarkMap changed_marks{{"wan", 0x200U}};
  auto context = fixture.context();
  context.outbound_marks = &changed_marks;
  FirewallPlan changed;
  FirewallRuleRegistrar registrar(changed);
  RouteMarkRuleModule{}.register_rules(context, registrar);
  registrar.finish();

  REQUIRE(changed.rules.size() == first.rules.size());
  CHECK(changed.rules.front().key == first.rules.front().key);
  CHECK(changed.rules.front().action != first.rules.front().action);
}

TEST_CASE("route mark owns selection and reads runtime marks") {
  const auto config = parse_config(R"({
    "outbounds": [
      {"type":"table","tag":"wan","table":100},
      {"type":"table","tag":"missing","table":101},
      {"type":"table","tag":"zero","table":102},
      {"type":"blackhole","tag":"blocked"},
      {"type":"ignore","tag":"direct"},
      {"type":"urltest","tag":"balanced","strategy":"balance",
       "outbound_groups":[{"outbounds":["wan"]}]}
    ],
    "lists": {"remote": {"ip_cidrs":["192.0.2.0/24"],
                            "domains":["example.test"]}},
    "route": {"rules": [
      {"outbound":"wan","dscp":1,"dest_addr":"192.0.2.1"},
      {"outbound":"wan","dscp":2,"dest_addr":"2001:db8::1"},
      {"list":["remote"],"outbound":"wan","dscp":3},
      {"outbound":"wan","dscp":4,"enabled":false},
      {"outbound":"direct","dscp":5},
      {"outbound":"blocked","dscp":6},
      {"outbound":"balanced","dscp":7},
      {"outbound":"missing","dscp":8},
      {"outbound":"zero","dscp":9},
      {"outbound":"unknown","dscp":10}
    ]}
  })");
  const OutboundMarkMap marks{{"wan", 0x12340000U}, {"zero", 0}};
  const std::vector<RuleState> stale_states;
  const std::map<std::string, ListSetUsage> usage = {
      {"remote", {true, true, 30}}};
  const std::vector<DumpedRoute> main_routes;
  const std::vector<DumpedInterface> interfaces;
  const uint32_t mask = 0xFFFF0000U;
  const FirewallBuildContext context{
      *config.route->rules, stale_states, *config.outbounds, *config.lists,
      usage, main_routes, interfaces, FirewallBackend::nftables, true, mask,
      nullptr, true, true, true, {}, &config, &marks};
  FirewallPlan plan;
  plan.fwmark_mask = mask;
  FirewallRuleRegistrar registrar(plan);
  RouteMarkRuleModule{}.register_rules(context, registrar);
  registrar.finish();

  REQUIRE(plan.rules.size() == 6);
  REQUIRE(plan.sets.size() == 4);
  CHECK(plan.sets[0].name == "kpbr4_remote");
  CHECK(plan.sets[1].name == "kpbr4d_remote");
  CHECK(plan.sets[2].name == "kpbr6_remote");
  CHECK(plan.sets[3].name == "kpbr6d_remote");

  const auto& ipv4 = plan.rules[0];
  CHECK(ipv4.source_rule_index == 0);
  CHECK(ipv4.priority == 0);
  CHECK(ipv4.family == FirewallFamily::ipv4);
  CHECK(ipv4.key == FirewallRuleKey::compact(
                         "route.mark",
                         "rule=0;occurrence=0;target=none;family=ipv4;proto=any;hook=prerouting"));

  const auto& ipv6 = plan.rules[1];
  CHECK(ipv6.source_rule_index == 1);
  CHECK(ipv6.priority == 1);
  CHECK(ipv6.family == FirewallFamily::ipv6);
  CHECK(ipv6.key == FirewallRuleKey::compact(
                         "route.mark",
                         "rule=1;occurrence=0;target=none;family=ipv6;proto=any;hook=prerouting"));

  for (std::size_t index = 0; index < plan.rules.size(); ++index) {
    const auto& rule = plan.rules[index];
    CHECK(rule.insertion_order == index);
    REQUIRE(std::holds_alternative<MarkAction>(rule.action));
    CHECK(std::get<MarkAction>(rule.action).value == 0x12340000U);
    CHECK(std::get<MarkAction>(rule.action).mask == mask);
  }
  CHECK(plan.rules[2].source_rule_index == 2);
  CHECK(plan.rules[2].family == FirewallFamily::ipv4);
  CHECK(plan.rules[2].criteria.dst_set_name == "kpbr4_remote");
  CHECK(plan.rules[3].family == FirewallFamily::ipv6);
  CHECK(plan.rules[3].criteria.dst_set_name == "kpbr6_remote");
  CHECK(plan.rules[4].family == FirewallFamily::ipv4);
  CHECK(plan.rules[4].criteria.dst_set_name == "kpbr4d_remote");
  CHECK(plan.rules[5].family == FirewallFamily::ipv6);
  CHECK(plan.rules[5].criteria.dst_set_name == "kpbr6d_remote");
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

TEST_CASE("config builds ordered DNS detour rules in the firewall plan") {
  Config config = parse_config(R"({
    "outbounds": [
      {"type":"table","tag":"route_z","table":100},
      {"type":"table","tag":"route_a","table":101},
      {"type":"table","tag":"unmarked","table":102},
      {"type":"table","tag":"zero_mark","table":103}
    ],
    "dns": {"servers":[
      {"tag":"upstream_z","address":"[2001:db8::53]:5353",
       "detour":"route_z"},
      {"tag":"upstream_a","address":"192.0.2.53:5353",
       "detour":"route_a"},
      {"tag":"no_detour","address":"192.0.2.54:5353"},
      {"tag":"unknown_outbound","address":"192.0.2.55:5353",
       "detour":"missing"},
      {"tag":"missing_mark","address":"192.0.2.56:5353",
       "detour":"unmarked"},
      {"tag":"zero_mark","address":"192.0.2.57:5353",
       "detour":"zero_mark"}
    ]}
  })");
  const OutboundMarkMap marks{
      {internal_detour_mark_key("route_z"), 0x300U},
      {internal_detour_mark_key("route_a"), 0x200U},
      {internal_detour_mark_key("zero_mark"), 0U}};
  const Config ordered_config = config;

  const auto first_plan = build_plan(config, marks);
  const auto first = dns_rules(first_plan);
  REQUIRE(first.size() == 4);
  CHECK(first[0]->family == FirewallFamily::ipv6);
  CHECK(first[0]->criteria.dst_addr == std::vector<std::string>{"2001:db8::53"});
  CHECK(first[0]->criteria.proto == L4Proto::Tcp);
  CHECK(first[1]->criteria.proto == L4Proto::Udp);
  CHECK(first[2]->family == FirewallFamily::ipv4);
  CHECK(first[2]->criteria.dst_addr == std::vector<std::string>{"192.0.2.53"});
  CHECK(first[2]->criteria.proto == L4Proto::Tcp);
  CHECK(first[3]->criteria.proto == L4Proto::Udp);
  for (const auto* rule : first) {
    CHECK(rule->hook == FirewallHook::output);
    CHECK(rule->criteria.apply_output);
    CHECK(rule->criteria.dst_port == PortSpec("5353"));
  }
  CHECK(std::get<MarkAction>(first[0]->action).value == 0x300U);
  CHECK(std::get<MarkAction>(first[2]->action).value == 0x200U);

  std::reverse(config.dns->servers->begin(), config.dns->servers->end());
  const auto reversed_plan = build_plan(config, marks);
  const auto reversed = dns_rules(reversed_plan);
  REQUIRE(reversed.size() == first.size());
  CHECK(reversed[0]->criteria.dst_addr == std::vector<std::string>{"192.0.2.53"});
  CHECK(reversed[2]->criteria.dst_addr == std::vector<std::string>{"2001:db8::53"});
  CHECK(first[0]->key == reversed[2]->key);
  CHECK(first[1]->key == reversed[3]->key);
  CHECK(first[2]->key == reversed[0]->key);
  CHECK(first[3]->key == reversed[1]->key);

  OutboundMarkMap changed_marks = marks;
  changed_marks[internal_detour_mark_key("route_a")] = 0x400U;
  const auto changed_plan = build_plan(ordered_config, changed_marks);
  const auto changed = dns_rules(changed_plan);
  REQUIRE(changed.size() == first.size());
  CHECK(changed[2]->key == first[2]->key);
  CHECK(changed[2]->action != first[2]->action);

  const auto ipv4_plan = build_plan(ordered_config, marks, false);
  const auto ipv4_only = dns_rules(ipv4_plan);
  REQUIRE(ipv4_only.size() == 2);
  CHECK(ipv4_only[0]->family == FirewallFamily::ipv4);
  CHECK(ipv4_only[1]->family == FirewallFamily::ipv4);

  const Config without_dns = parse_config(R"({"outbounds":[]})");
  const auto no_dns_plan = build_plan(without_dns, {});
  CHECK(dns_rules(no_dns_plan).empty());

  const Config duplicate_endpoints = parse_config(R"({
    "outbounds": [{"type":"table","tag":"wan","table":100}],
    "dns": {"servers":[
      {"tag":"upstream","address":"192.0.2.53:5353","detour":"wan"},
      {"tag":"upstream","address":"192.0.2.53:5353","detour":"wan"}
    ]}
  })");
  const auto duplicate_plan = build_plan(
      duplicate_endpoints, {{internal_detour_mark_key("wan"), 0x200U}});
  const auto duplicates = dns_rules(duplicate_plan);
  REQUIRE(duplicates.size() == 8);
  for (std::size_t index = 2; index < duplicates.size(); index += 2) {
    CHECK(duplicates[index]->key != duplicates[index - 2]->key);
  }
}

TEST_CASE("health reports only the removed DNS physical instance as missing") {
  const Config config = parse_config(R"({
    "outbounds": [{"type":"table","tag":"wan","table":100}],
    "dns": {"servers":[
      {"tag":"upstream_a","address":"192.0.2.53:5353","detour":"wan"},
      {"tag":"upstream_b","address":"192.0.2.54:5353","detour":"wan"}
    ]}
  })");
  const auto plan = build_plan(
      config, {{internal_detour_mark_key("wan"), 0x200U}});
  const auto instances = dns_rules(plan);
  REQUIRE(instances.size() == 4);
  const auto removed_key = instances[1]->key;
  FirewallPlan dns_plan;
  dns_plan.fwmark_mask = plan.fwmark_mask;
  for (const auto* instance : instances) {
    dns_plan.rules.push_back(*instance);
  }

  FirewallSnapshot snapshot;
  snapshot.backend = FirewallBackend::nftables;
  snapshot.available = true;
  for (const auto& expected : dns_plan.rules) {
    if (expected.key == removed_key) {
      continue;
    }
    ObservedFirewallRule observed;
    observed.key = expected.key;
    observed.hook = expected.hook;
    observed.family = expected.family;
    observed.criteria = expected.criteria;
    observed.action = expected.action;
    snapshot.rules.push_back(std::move(observed));
  }

  const auto checks = verify_firewall_plan(dns_plan, snapshot);
  REQUIRE(checks.size() == dns_plan.rules.size());
  for (std::size_t index = 0; index < checks.size(); ++index) {
    CHECK(checks[index].status ==
          (dns_plan.rules[index].key == removed_key ? CheckStatus::missing
                                                    : CheckStatus::ok));
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
  const OutboundMarkMap marks{{"wan", 0x100U}};
  const auto states = build_fw_rule_states(config, marks);
  const std::vector<DumpedRoute> main_routes;
  const std::vector<DumpedInterface> interfaces;
  const std::map<std::string, ListConfig> lists;
  const std::map<std::string, ListSetUsage> usage;
  const FirewallBuildContext context{
      *config.route->rules, states, *config.outbounds, lists, usage, main_routes,
      interfaces, FirewallBackend::nftables, true, 0xFFFFFFFFU, nullptr, true,
      true, true, {}, &config, &marks};
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
