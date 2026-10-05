#include <doctest/doctest.h>

#include "../src/firewall/firewall_rule_modules.hpp"
#include "../src/firewall/firewall_lowering.hpp"
#include "../src/firewall/firewall_plan_verifier.hpp"
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
  const std::map<std::string, ListSetUsage> usage = {
      {"remote", {true, true, 30}}};
  const std::vector<DumpedRoute> main_routes;
  const std::vector<DumpedInterface> interfaces;

  FirewallBuildContext context() const {
    return {*config.route->rules, *config.outbounds, *config.lists,
            usage, main_routes, interfaces, FirewallBackend::nftables, true,
            0xFFFFFFFFU, nullptr, &config, &marks};
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
  const std::map<std::string, ListConfig> lists;
  const std::map<std::string, ListSetUsage> usage;
  const std::vector<DumpedRoute> main_routes;
  const std::vector<DumpedInterface> interfaces;
  FirewallBalanceCandidates candidates;

  FirewallBuildContext context() const {
    return {*config.route->rules, *config.outbounds, lists, usage,
            main_routes, interfaces, FirewallBackend::nftables, true,
            0xFFFFFFFFU, &candidates, &config, &marks};
  }
};

template <typename Module, typename Fixture>
FirewallPlan build_module_plan(const Module& module, const Fixture& fixture) {
  FirewallPlan plan;
  FirewallRuleRegistrar registrar(plan);
  const auto context = fixture.context();
  module(context, registrar);
  registrar.finish();
  return plan;
}

FirewallPlan build_plan(const Config& config, const OutboundMarkMap& marks,
                        bool ipv6_enabled = true) {
  const std::map<std::string, ListSetUsage> usage;
  const std::vector<DumpedRoute> main_routes;
  const std::vector<DumpedInterface> interfaces;
  return build_firewall_plan({config, marks, usage, main_routes, interfaces,
                              nullptr, ipv6_enabled, 0xFFFFFFFFU,
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
  const auto mark_plan = build_module_plan(register_route_mark_rules, fixture);
  const auto drop_plan = build_module_plan(register_route_drop_rules, fixture);
  const auto pass_plan = build_module_plan(register_route_pass_rules, fixture);

  REQUIRE(mark_plan.rules.size() == 5);
  CHECK(drop_plan.rules.size() == 1);
  CHECK(pass_plan.rules.size() == 1);
  REQUIRE(drop_plan.rules.front().source_rule_index == 1);
  CHECK(drop_plan.rules.front().key == FirewallRuleKey::compact(
      "route.drop",
      "rule=1;occurrence=0;target=none;family=any;proto=any"));
  CHECK(std::get<VerdictAction>(drop_plan.rules.front().action) ==
        VerdictAction::drop);
  REQUIRE(pass_plan.rules.front().source_rule_index == 2);
  CHECK(pass_plan.rules.front().key == FirewallRuleKey::compact(
      "route.pass",
      "rule=2;occurrence=0;target=none;family=any;proto=any"));
  CHECK(std::get<VerdictAction>(pass_plan.rules.front().action) ==
        VerdictAction::pass);
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
  const auto& no_routes = *no_rules.route->rules;
  const auto& no_outbounds = *no_rules.outbounds;
  const auto& no_lists = *no_rules.lists;
  const std::map<std::string, ListSetUsage> no_usage;
  const std::vector<DumpedRoute> no_main_routes;
  const std::vector<DumpedInterface> no_interfaces;
  const FirewallBuildContext no_context{
      no_routes, no_outbounds, no_lists, no_usage, no_main_routes,
      no_interfaces, FirewallBackend::nftables, true, 0xFFFFFFFFU};
  FirewallPlan no_plan;
  FirewallRuleRegistrar no_registrar(no_plan);
  register_route_mark_rules(no_context, no_registrar);
  no_registrar.finish();
  CHECK(no_plan.rules.empty());
}

TEST_CASE("route module keys are stable and mark changes are semantic") {
  const ModuleFixture fixture;
  const auto first = build_module_plan(register_route_mark_rules, fixture);
  const auto second = build_module_plan(register_route_mark_rules, fixture);
  REQUIRE(first.rules.size() == second.rules.size());
  for (std::size_t index = 0; index < first.rules.size(); ++index) {
    CHECK(first.rules[index].key == second.rules[index].key);
  }

  const OutboundMarkMap changed_marks{{"wan", 0x200U}};
  auto context = fixture.context();
  context.outbound_marks = &changed_marks;
  FirewallPlan changed;
  FirewallRuleRegistrar registrar(changed);
  register_route_mark_rules(context, registrar);
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
  const std::map<std::string, ListSetUsage> usage = {
      {"remote", {true, true, 30}}};
  const std::vector<DumpedRoute> main_routes;
  const std::vector<DumpedInterface> interfaces;
  const uint32_t mask = 0xFFFF0000U;
  const FirewallBuildContext context{
      *config.route->rules, *config.outbounds, *config.lists,
      usage, main_routes, interfaces, FirewallBackend::nftables, true, mask,
      nullptr, &config, &marks};
  FirewallPlan plan;
  plan.fwmark_mask = mask;
  FirewallRuleRegistrar registrar(plan);
  register_route_mark_rules(context, registrar);
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
                         "rule=0;occurrence=0;target=none;family=ipv4;proto=any"));

  const auto& ipv6 = plan.rules[1];
  CHECK(ipv6.source_rule_index == 1);
  CHECK(ipv6.priority == 1);
  CHECK(ipv6.family == FirewallFamily::ipv6);
  CHECK(ipv6.key == FirewallRuleKey::compact(
                         "route.mark",
                         "rule=1;occurrence=0;target=none;family=ipv6;proto=any"));

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

TEST_CASE("route module manifest is iterable without a manual count") {
  const auto manifest = route_rule_module_manifest();
  const std::vector<RouteRuleModuleRegistration> modules(manifest.begin(),
                                                         manifest.end());
  CHECK(modules.size() == manifest.size());
  const std::vector<RouteRuleModuleRegistration> expected = {
      register_restore_conntrack_mark_rules,
      register_skip_local_replies_rules,
      register_skip_lan_output_rules,
      register_skip_established_or_dnat_rules,
      register_skip_marked_packets_rules,
      register_inbound_interface_filter_rules,
      register_route_mark_rules,
      register_route_drop_rules,
      register_route_pass_rules,
      register_route_balance_rules,
      register_dns_detour_rules,
      register_intercept_dns_hold_rules,
      register_intercept_l7_sniff_rules,
  };
  CHECK(modules == expected);
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

  REQUIRE(plan.rules.size() == 13);
  CHECK(plan.rules[0].key.module_id == "prefilter.restore_conntrack_mark");
  CHECK(plan.rules[1].key.module_id == "prefilter.skip_local_replies");
  CHECK(plan.rules[2].key == FirewallRuleKey::compact(
                                 "prefilter.skip_lan_output", "bcast"));
  CHECK(plan.rules[3].key == FirewallRuleKey::compact(
                                 "prefilter.skip_lan_output", "mcast"));
  CHECK(plan.rules[4].key.module_id == "prefilter.skip_established_or_dnat");
  CHECK(plan.rules[5].key.module_id == "prefilter.skip_marked_packets");
  CHECK(plan.rules[6].source_rule_index == 0);
  CHECK(plan.rules[7].source_rule_index == 1);
  CHECK(plan.rules[8].source_rule_index == 2);
  CHECK(plan.rules[9].source_rule_index == 3);
  CHECK(plan.rules[10].source_rule_index == 3);
  CHECK(plan.rules[11].source_rule_index == 3);
  CHECK(plan.rules[12].source_rule_index == 3);
  CHECK(plan.rules[6].key.module_id == "route.mark");
  CHECK(plan.rules[7].key.module_id == "route.drop");
  CHECK(plan.rules[8].key.module_id == "route.pass");
}

FirewallPlan build_plan_with(RouteRuleModuleRegistration module,
                             const FirewallBuildContext& context) {
  FirewallPlan plan;
  plan.fwmark_mask = context.fwmark_mask;
  FirewallRuleRegistrar registrar(plan);
  module(context, registrar);
  registrar.finish();
  return plan;
}

TEST_CASE("prefilter modules emit canonical operations and honor inputs") {
  const ModuleFixture fixture;
  Config config = fixture.config;
  config.route->inbound_interfaces = std::vector<std::string>{"br-lan", "wg0"};
  auto context = fixture.context();
  context.fwmark_mask = 0x00FF0000U;
  context.config = &config;

  const auto restore = build_plan_with(register_restore_conntrack_mark_rules, context);
  REQUIRE(restore.rules.size() == 1);
  CHECK(restore.rules.front().stage == FirewallRuleStage::restore_conntrack);
  CHECK(restore.rules.front().priority == 0);
  CHECK(restore.rules.front().family == FirewallFamily::any);
  CHECK(std::get<RestoreConntrackMarkAction>(restore.rules.front().action).mask ==
        context.fwmark_mask);

  const auto dnat = build_plan_with(register_skip_established_or_dnat_rules, context);
  REQUIRE(dnat.rules.size() == 1);
  CHECK(dnat.rules.front().stage == FirewallRuleStage::global_bypass);
  CHECK(dnat.rules.front().priority == 0);
  CHECK(std::holds_alternative<SkipEstablishedOrDnatAction>(
      dnat.rules.front().action));

  const auto replies = build_plan_with(register_skip_local_replies_rules, context);
  REQUIRE(replies.rules.size() == 1);
  CHECK(replies.rules.front().stage == FirewallRuleStage::global_bypass);
  CHECK(replies.rules.front().priority < dnat.rules.front().priority);
  CHECK(replies.rules.front().hook == FirewallHook::prerouting);
  CHECK(replies.rules.front().family == FirewallFamily::any);
  CHECK(std::holds_alternative<SkipLocalRepliesAction>(
      replies.rules.front().action));

  const auto marked = build_plan_with(register_skip_marked_packets_rules, context);
  REQUIRE(marked.rules.size() == 1);
  CHECK(marked.rules.front().priority == 1);
  CHECK(std::holds_alternative<SkipMarkedPacketsAction>(
      marked.rules.front().action));

  const auto inbound = build_plan_with(register_inbound_interface_filter_rules, context);
  REQUIRE(inbound.rules.size() == 1);
  CHECK(inbound.rules.front().priority == 2);
  CHECK(std::get<InboundInterfaceFilterAction>(inbound.rules.front().action)
            .interfaces == std::vector<std::string>{"br-lan", "wg0"});
  CHECK(inbound.rules.front().key.module_id ==
        "prefilter.inbound_interface");
  CHECK(inbound.rules.front().key == FirewallRuleKey::compact(
      "prefilter.inbound_interface", "br-lan;wg0;"));
}

TEST_CASE("skip_lan_output: oif rule only with inbound interfaces, bcast/mcast always") {
  const ModuleFixture fixture;
  Config config = fixture.config;
  auto context = fixture.context();
  context.config = &config;

  const auto skip_kinds = [&] {
    std::vector<SkipLanOutputAction::Kind> kinds;
    for (const auto& rule :
         build_plan_with(register_skip_lan_output_rules, context).rules) {
      CHECK(rule.stage == FirewallRuleStage::global_bypass);
      CHECK(rule.hook == FirewallHook::output);
      CHECK(rule.family == FirewallFamily::any);
      CHECK(rule.priority == -1);
      kinds.push_back(std::get<SkipLanOutputAction>(rule.action).kind);
    }
    return kinds;
  };
  using Kind = SkipLanOutputAction::Kind;

  // Empty or missing inbound_interfaces: only the link-local classes.
  CHECK(skip_kinds() == std::vector<Kind>{Kind::broadcast, Kind::multicast});
  config.route->inbound_interfaces = std::vector<std::string>{};
  CHECK(skip_kinds() == std::vector<Kind>{Kind::broadcast, Kind::multicast});

  config.route->inbound_interfaces = std::vector<std::string>{"br-lan", "wg0"};
  CHECK(skip_kinds() ==
        std::vector<Kind>{Kind::lan_oif, Kind::broadcast, Kind::multicast});
  const auto plan = build_plan_with(register_skip_lan_output_rules, context);
  CHECK(std::get<SkipLanOutputAction>(plan.rules[0].action).interfaces ==
        std::vector<std::string>{"br-lan", "wg0"});
  CHECK(plan.rules[0].key ==
        FirewallRuleKey::compact("prefilter.skip_lan_output", "lan_oif"));
}

TEST_CASE("skip_lan_output sorts right after skip_local_replies") {
  const ModuleFixture fixture;
  Config config = fixture.config;
  config.route->inbound_interfaces = std::vector<std::string>{"br-lan"};
  auto context = fixture.context();
  context.config = &config;
  FirewallPlan plan;
  FirewallRuleRegistrar registrar(plan);
  for (const auto register_module : route_rule_module_manifest()) {
    register_module(context, registrar);
  }
  registrar.finish();
  REQUIRE(plan.rules.size() >= 6);
  CHECK(plan.rules[0].key.module_id == "prefilter.restore_conntrack_mark");
  CHECK(plan.rules[1].key.module_id == "prefilter.skip_local_replies");
  CHECK(plan.rules[2].key.module_id == "prefilter.skip_lan_output");
  CHECK(plan.rules[3].key.module_id == "prefilter.skip_lan_output");
  CHECK(plan.rules[4].key.module_id == "prefilter.skip_lan_output");
  CHECK(plan.rules[5].key.module_id == "prefilter.skip_established_or_dnat");
}

TEST_CASE("skip_marked_packets and inbound interface conditions follow config") {
  const ModuleFixture fixture;
  Config config = fixture.config;
  auto context = fixture.context();
  context.config = &config;

  const auto marked = [&] {
    return plan_has_action<SkipMarkedPacketsAction>(
        build_plan_with(register_skip_marked_packets_rules, context));
  };
  const auto inbound = [&] {
    return plan_has_action<InboundInterfaceFilterAction>(
        build_plan_with(register_inbound_interface_filter_rules, context));
  };

  // Defaults: marked-packet bypass on, no interface restriction.
  CHECK(marked());
  CHECK_FALSE(inbound());

  config.daemon = DaemonConfig{};
  config.daemon->skip_marked_packets = std::nullopt;
  CHECK(marked());
  config.daemon->skip_marked_packets = false;
  CHECK_FALSE(marked());
  config.daemon->skip_marked_packets = true;
  CHECK(marked());

  config.route->inbound_interfaces = std::vector<std::string>{};
  CHECK_FALSE(inbound());
  config.route->inbound_interfaces = std::vector<std::string>{"br0"};
  CHECK(inbound());
  config.route.reset();
  CHECK_FALSE(inbound());

  // Without a config the safe defaults apply.
  context.config = nullptr;
  CHECK(marked());
  CHECK_FALSE(inbound());
}

TEST_CASE("established/DNAT bypass is emitted on both backends") {
  const ModuleFixture fixture;
  for (const auto backend :
       {FirewallBackend::nftables, FirewallBackend::iptables}) {
    auto context = fixture.context();
    context.backend = backend;
    context.outbound_marks = nullptr;
    context.config = nullptr;
    CHECK(plan_has_action<SkipEstablishedOrDnatAction>(
        build_plan_with(register_skip_established_or_dnat_rules, context)));
  }
}

TEST_CASE("restore conntrack mark follows backend, owned marks and mask") {
  const ModuleFixture fixture;
  const OutboundMarkMap zero_marks{{"wan", 0U}};
  const OutboundMarkMap no_marks;
  const auto restores = [](const FirewallBuildContext& context) {
    return plan_has_action<RestoreConntrackMarkAction>(
        build_plan_with(register_restore_conntrack_mark_rules, context));
  };

  auto context = fixture.context();
  context.backend = FirewallBackend::nftables;
  CHECK(restores(context));  // owned non-zero mark present
  context.outbound_marks = &zero_marks;
  CHECK_FALSE(restores(context));
  context.outbound_marks = &no_marks;
  CHECK_FALSE(restores(context));
  context.outbound_marks = nullptr;
  CHECK_FALSE(restores(context));

  context.backend = FirewallBackend::iptables;
  CHECK(restores(context));  // iptables restores regardless of marks
  context.outbound_marks = &no_marks;
  CHECK(restores(context));
  context.outbound_marks = fixture.context().outbound_marks;
  CHECK(restores(context));

  // A zero fwmark mask leaves no owned bits to restore on either backend.
  context.fwmark_mask = 0;
  CHECK_FALSE(restores(context));
  context.backend = FirewallBackend::nftables;
  CHECK_FALSE(restores(context));
}

TEST_CASE("restore conntrack mark rule carries the configured mask") {
  const ModuleFixture fixture;
  auto context = fixture.context();
  context.fwmark_mask = 0x00FF0000U;
  const auto plan = build_plan_with(register_restore_conntrack_mark_rules, context);
  REQUIRE(plan.rules.size() == 1);
  CHECK(plan.rules.front().key ==
        FirewallRuleKey::compact("prefilter.restore_conntrack_mark",
                                 "mask=" + std::to_string(0x00FF0000U)));
}

TEST_CASE("route balance module preserves fallback and candidate ordering") {
  BalanceModuleFixture fixture;
  const std::vector<std::vector<FirewallBalanceCandidate>> candidate_cases = {
      {},
      {{0x200U, true, false}},
      {{0x300U, true, true}, {0x200U, true, false}, {0x400U, false, true}}};

  for (const auto& candidates : candidate_cases) {
    fixture.candidates["auto"] = candidates;
    const auto plan = build_module_plan(register_route_balance_rules, fixture);
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
  const auto first = build_module_plan(register_route_balance_rules, fixture);

  fixture.candidates["auto"] = {{0x300U, false, true}, {0x400U, true, true}};
  const auto changed = build_module_plan(register_route_balance_rules, fixture);

  REQUIRE(first.rules.size() == 1);
  REQUIRE(changed.rules.size() == 1);
  CHECK(changed.rules.front().key == first.rules.front().key);
  CHECK(changed.rules.front().action != first.rules.front().action);
}

TEST_CASE("route balance skips rules without an owned fallback mark") {
  BalanceModuleFixture fixture;
  fixture.candidates["auto"] = {{0x200U, true, true}};
  const auto build = [&](const OutboundMarkMap& marks) {
    auto context = fixture.context();
    context.outbound_marks = &marks;
    FirewallPlan plan;
    FirewallRuleRegistrar registrar(plan);
    register_route_balance_rules(context, registrar);
    registrar.finish();
    return plan;
  };

  CHECK(build({}).rules.empty());
  CHECK(build({{"auto", 0}}).rules.empty());
}

TEST_CASE("route balance keeps nftables-only backend validation") {
  BalanceModuleFixture fixture;
  fixture.candidates["auto"] = {{0x200U, true, true}};
  auto context = fixture.context();
  context.backend = FirewallBackend::iptables;

  FirewallPlan plan;
  plan.fwmark_mask = context.fwmark_mask;
  FirewallRuleRegistrar registrar(plan);
  register_route_balance_rules(context, registrar);
  registrar.finish();

  REQUIRE(plan.rules.size() == 1);
  CHECK_THROWS_WITH(
      validate_firewall_plan_backend(plan, FirewallBackend::iptables),
      "unsupported firewall construct: module_id=route.balance, instance_id=" +
          plan.rules.front().key.instance_id +
          ", backend=iptables, construct=BalanceAction (requires nftables)");
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

  REQUIRE(plan.rules.size() == 7);
  CHECK(plan.rules[0].key.module_id == "prefilter.restore_conntrack_mark");
  CHECK(plan.rules[6].key.module_id == "route.balance");
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

  // Lower the DNS plan the way the nft backend does and drop the physical
  // rules of the removed instance from the observed side.  The verifier knows
  // nothing about DNS: only that rules of that plan rule are gone.
  FirewallLoweringContext context;
  context.backend = FirewallBackend::nftables;
  context.fwmark_mask = dns_plan.fwmark_mask;
  const PhysicalRuleset expected = lower_firewall_plan(dns_plan, context);
  std::size_t removed_index = dns_plan.rules.size();
  for (std::size_t index = 0; index < dns_plan.rules.size(); ++index) {
    if (dns_plan.rules[index].key == removed_key) removed_index = index;
  }
  REQUIRE(removed_index < dns_plan.rules.size());

  FirewallSnapshot snapshot;
  snapshot.backend = FirewallBackend::nftables;
  snapshot.available = true;
  snapshot.ruleset = expected;
  std::size_t removed_rules = 0;
  for (auto& chain : snapshot.ruleset.chains) {
    const auto end = std::remove_if(
        chain.rules.begin(), chain.rules.end(), [&](const PhysicalRule& rule) {
          const bool remove = rule.plan_rule == removed_index;
          removed_rules += remove ? 1U : 0U;
          return remove;
        });
    chain.rules.erase(end, chain.rules.end());
  }
  REQUIRE(removed_rules > 0);

  const auto checks = verify_firewall_plan(dns_plan, expected, snapshot);
  REQUIRE(checks.size() == dns_plan.rules.size());
  for (std::size_t index = 0; index < checks.size(); ++index) {
    CHECK(checks[index].status ==
          (index == removed_index ? CheckStatus::missing : CheckStatus::ok));
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
  const std::vector<DumpedRoute> main_routes;
  const std::vector<DumpedInterface> interfaces;
  const std::map<std::string, ListConfig> lists;
  const std::map<std::string, ListSetUsage> usage;
  const FirewallBuildContext context{
      *config.route->rules, *config.outbounds, lists, usage, main_routes,
      interfaces, FirewallBackend::nftables, true, 0xFFFFFFFFU, nullptr,
      &config, &marks};
  FirewallPlan plan;
  FirewallRuleRegistrar registrar(plan);
  for (const auto register_module : route_rule_module_manifest()) {
    register_module(context, registrar);
  }
  registrar.finish();

  REQUIRE(plan.rules.size() == 9);
  CHECK(plan.rules[6].source_rule_index == 0);
  CHECK(plan.rules[7].source_rule_index == 1);
  CHECK(plan.rules[8].source_rule_index == 2);
  CHECK(std::holds_alternative<VerdictAction>(plan.rules[6].action));
  CHECK(std::holds_alternative<MarkAction>(plan.rules[7].action));
  CHECK(std::holds_alternative<VerdictAction>(plan.rules[8].action));
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
                              0xFFFFFFFFU, FirewallBackend::nftables});
}

} // namespace

TEST_CASE("IPv4 default gateway keeps both list families but emits IPv4 rules") {
  const auto plan = build_gateway_list_plan("ipv4");

  REQUIRE(plan.sets.size() == 4);
  CHECK(plan.sets[0].name == "kpbr4_remote");
  CHECK(plan.sets[1].name == "kpbr4d_remote");
  CHECK(plan.sets[2].name == "kpbr6_remote");
  CHECK(plan.sets[3].name == "kpbr6d_remote");
  REQUIRE(plan.rules.size() == 8);
  CHECK(plan.rules[6].criteria.dst_set_name == "kpbr4_remote");
  CHECK(plan.rules[7].criteria.dst_set_name == "kpbr4d_remote");
  for (const auto& rule : std::vector<FirewallRuleInstance>{plan.rules[6], plan.rules[7]}) {
    CHECK(rule.family == FirewallFamily::ipv4);
    CHECK(rule.hook == FirewallHook::prerouting);
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
  REQUIRE(plan.rules.size() == 8);
  CHECK(plan.rules[6].criteria.dst_set_name == "kpbr6_remote");
  CHECK(plan.rules[7].criteria.dst_set_name == "kpbr6d_remote");
  for (const auto& rule : std::vector<FirewallRuleInstance>{plan.rules[6], plan.rules[7]}) {
    CHECK(rule.family == FirewallFamily::ipv6);
    CHECK(rule.hook == FirewallHook::prerouting);
    CHECK(rule.criteria.default_gateway == DefaultGatewayFamily::Ipv6);
    CHECK(rule.criteria.proto == L4Proto::TcpUdp);
    CHECK(rule.criteria.dst_port == PortSpec("443"));
  }
}

// ---------------------------------------------------------------------------
// Interception modules (DNS hold, L7 sniff): off unless explicitly enabled
// ---------------------------------------------------------------------------

namespace {

FirewallPlan intercept_plan(const ModuleFixture& fixture,
                            RouteRuleModuleRegistration module,
                            const InterceptFirewallSettings& settings) {
  auto context = fixture.context();
  context.intercept = settings;
  return build_plan_with(module, context);
}

std::vector<std::string> sniff_instances(const FirewallPlan& plan) {
  std::vector<std::string> ids;
  for (const auto& rule : plan.rules) ids.push_back(rule.key.instance_id);
  return ids;
}

} // namespace

TEST_CASE("interception modules plan nothing without settings") {
  const ModuleFixture fixture;
  CHECK(build_module_plan(register_intercept_dns_hold_rules, fixture).rules.empty());
  CHECK(build_module_plan(register_intercept_l7_sniff_rules, fixture).rules.empty());
  // Present but both groups off.
  CHECK(intercept_plan(fixture, register_intercept_dns_hold_rules, {}).rules.empty());
  CHECK(intercept_plan(fixture, register_intercept_l7_sniff_rules, {}).rules.empty());
  // Each module honors only its own switch.
  InterceptFirewallSettings sniff_only;
  sniff_only.l7_sniff = true;
  CHECK(intercept_plan(fixture, register_intercept_dns_hold_rules, sniff_only)
            .rules.empty());
  InterceptFirewallSettings dns_only;
  dns_only.dns_hold = true;
  CHECK(intercept_plan(fixture, register_intercept_l7_sniff_rules, dns_only)
            .rules.empty());
  // The whole manifest adds nothing for a default context.
  FirewallPlan plan;
  FirewallRuleRegistrar registrar(plan);
  const auto context = fixture.context();
  for (const auto register_module : route_rule_module_manifest()) {
    register_module(context, registrar);
  }
  for (const auto& rule : plan.rules) {
    CHECK(rule.key.module_id != "dns.intercept_hold");
    CHECK(rule.key.module_id != "l7.sniff");
  }
}

TEST_CASE("dns.intercept_hold queues UDP and TCP responses at postrouting") {
  const ModuleFixture fixture;
  InterceptFirewallSettings settings;
  settings.dns_hold = true;
  settings.queue_num = 4242;
  const auto plan =
      intercept_plan(fixture, register_intercept_dns_hold_rules, settings);
  REQUIRE(plan.rules.size() == 2);
  const L4Proto protocols[] = {L4Proto::Udp, L4Proto::Tcp};
  for (std::size_t i = 0; i < 2; ++i) {
    const auto& rule = plan.rules[i];
    CHECK(rule.key.module_id == "dns.intercept_hold");
    CHECK(rule.stage == FirewallRuleStage::interception);
    CHECK(rule.hook == FirewallHook::postrouting);
    CHECK(rule.family == FirewallFamily::any);
    CHECK(rule.criteria.proto == protocols[i]);
    CHECK(rule.criteria.src_port == PortSpec("53"));
    CHECK(rule.criteria.dst_port.empty());
    CHECK(rule.criteria.ct_established_reply);
    CHECK_FALSE(rule.criteria.connbytes_original_packets.has_value());
    CHECK(std::get<QueueAction>(rule.action) == QueueAction{4242, true});
  }
  CHECK(plan.rules[0].key != plan.rules[1].key);
}

TEST_CASE("process_router_traffic=false: no output sniff, DNS hold skips loopback") {
  const ModuleFixture fixture;
  InterceptFirewallSettings settings;
  settings.dns_hold = true;
  settings.l7_sniff = true;
  const auto plan_for = [&](RouteRuleModuleRegistration module, bool router) {
    auto context = fixture.context();
    context.intercept = settings;
    context.process_router_traffic = router;
    return build_plan_with(module, context);
  };
  const auto sniff_off = plan_for(register_intercept_l7_sniff_rules, false);
  CHECK(sniff_instances(sniff_off) ==
        std::vector<std::string>{"tcp.forward", "udp.forward"});
  const auto sniff_on = plan_for(register_intercept_l7_sniff_rules, true);
  CHECK(sniff_on.rules.size() == 4);

  for (const auto& rule : plan_for(register_intercept_dns_hold_rules, false).rules) {
    CHECK(rule.criteria.exclude_oif == std::vector<std::string>{"lo"});
  }
  for (const auto& rule : plan_for(register_intercept_dns_hold_rules, true).rules) {
    CHECK(rule.criteria.exclude_oif.empty());
  }
}

TEST_CASE("l7.sniff copies the first packets of new flows at forward and output") {
  const ModuleFixture fixture;
  InterceptFirewallSettings settings;
  settings.l7_sniff = true;
  settings.nflog_group = 77;
  settings.snaplen = 1500;
  settings.max_packets = 4;

  SUBCASE("all protocols") {
    const auto plan =
        intercept_plan(fixture, register_intercept_l7_sniff_rules, settings);
    CHECK(sniff_instances(plan) ==
          std::vector<std::string>{"tcp.forward", "udp.forward", "tcp.output",
                                   "udp.output"});
    for (const auto& rule : plan.rules) {
      CHECK(rule.key.module_id == "l7.sniff");
      CHECK(rule.stage == FirewallRuleStage::interception);
      CHECK(rule.family == FirewallFamily::any);
      CHECK(rule.criteria.src_port.empty());
      CHECK_FALSE(rule.criteria.ct_established_reply);
      CHECK(rule.criteria.connbytes_original_packets->from == 1);
      CHECK(rule.criteria.connbytes_original_packets->to == 4);
      CHECK(std::get<LogAction>(rule.action) == LogAction{77, 1500});
    }
    CHECK(plan.rules[0].hook == FirewallHook::forward);
    CHECK(plan.rules[0].criteria.proto == L4Proto::Tcp);
    CHECK(plan.rules[0].criteria.dst_port == PortSpec("80,443"));
    CHECK(plan.rules[1].criteria.proto == L4Proto::Udp);
    CHECK(plan.rules[1].criteria.dst_port == PortSpec("443"));
    CHECK(plan.rules[2].hook == FirewallHook::output);
    CHECK(plan.rules[3].hook == FirewallHook::output);
  }
  SUBCASE("http disabled drops port 80") {
    settings.http = false;
    const auto plan =
        intercept_plan(fixture, register_intercept_l7_sniff_rules, settings);
    REQUIRE(plan.rules.size() == 4);
    CHECK(plan.rules[0].criteria.dst_port == PortSpec("443"));
  }
  SUBCASE("tls disabled drops 443/tcp") {
    settings.tls = false;
    const auto plan =
        intercept_plan(fixture, register_intercept_l7_sniff_rules, settings);
    REQUIRE(plan.rules.size() == 4);
    CHECK(plan.rules[0].criteria.dst_port == PortSpec("80"));
    CHECK(plan.rules[1].criteria.proto == L4Proto::Udp);
  }
  SUBCASE("quic disabled drops the UDP rule") {
    settings.quic = false;
    const auto plan =
        intercept_plan(fixture, register_intercept_l7_sniff_rules, settings);
    CHECK(sniff_instances(plan) ==
          std::vector<std::string>{"tcp.forward", "tcp.output"});
  }
  SUBCASE("only quic") {
    settings.tls = false;
    settings.http = false;
    const auto plan =
        intercept_plan(fixture, register_intercept_l7_sniff_rules, settings);
    CHECK(sniff_instances(plan) ==
          std::vector<std::string>{"udp.forward", "udp.output"});
  }
  SUBCASE("nothing to sniff") {
    settings.tls = settings.http = settings.quic = false;
    CHECK(intercept_plan(fixture, register_intercept_l7_sniff_rules, settings)
              .rules.empty());
  }
}

TEST_CASE("build_firewall_plan carries the interception settings") {
  const ModuleFixture fixture;
  const std::map<std::string, ListSetUsage> usage;
  const std::vector<DumpedRoute> main_routes;
  const std::vector<DumpedInterface> interfaces;
  const auto count_of = [](const FirewallPlan& plan, const char* module) {
    return std::count_if(plan.rules.begin(), plan.rules.end(),
                         [&](const FirewallRuleInstance& rule) {
                           return rule.key.module_id == module;
                         });
  };
  FirewallPlanBuildInputs inputs{fixture.config, fixture.marks, usage,
                                 main_routes,    interfaces,    nullptr,
                                 true,           0xFFFFFFFFU,   FirewallBackend::nftables};
  const auto without = build_firewall_plan(inputs);
  CHECK(count_of(without, "dns.intercept_hold") == 0);
  CHECK(count_of(without, "l7.sniff") == 0);

  inputs.intercept = InterceptFirewallSettings{};
  inputs.intercept->dns_hold = true;
  inputs.intercept->l7_sniff = true;
  const auto with = build_firewall_plan(inputs);
  // iproute.process_router_traffic defaults to false: no output sniff copies.
  CHECK_FALSE(with.process_router_traffic);
  CHECK(count_of(with, "dns.intercept_hold") == 2);
  CHECK(count_of(with, "l7.sniff") == 2);
  // Interception rules sort after every classification rule, and the other
  // rules are exactly the ones planned without them.
  CHECK(with.rules.size() == without.rules.size() + 4);
  for (std::size_t i = 0; i < without.rules.size(); ++i) {
    CHECK(with.rules[i].key == without.rules[i].key);
  }
  // Both validate for every backend.
  CHECK_NOTHROW(validate_firewall_plan_backend(with, FirewallBackend::nftables));
  CHECK_NOTHROW(validate_firewall_plan_backend(with, FirewallBackend::iptables));

  // process_router_traffic=true restores the output sniff copies.
  Config router_config = fixture.config;
  router_config.iproute = IprouteConfig{};
  router_config.iproute->process_router_traffic = true;
  FirewallPlanBuildInputs router_inputs{router_config, fixture.marks, usage,
                                        main_routes,   interfaces,    nullptr,
                                        true,          0xFFFFFFFFU,
                                        FirewallBackend::nftables};
  router_inputs.intercept = inputs.intercept;
  const auto router = build_firewall_plan(router_inputs);
  CHECK(router.process_router_traffic);
  CHECK(count_of(router, "l7.sniff") == 4);
  CHECK(router.rules.size() == without.rules.size() + 6);
}

} // namespace keen_pbr3
