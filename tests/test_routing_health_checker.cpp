#include <doctest/doctest.h>

#include "../src/firewall/firewall_lowering.hpp"
#include "../src/health/routing_health_checker.hpp"

#include <utility>

namespace keen_pbr3 {
namespace {

CommandResult command_result(const char* output) {
    return CommandResult{output, 0, false};
}

FirewallPlan active_mark_plan() {
    FirewallPlan plan;
    FirewallRuleInstance rule;
    rule.key = FirewallRuleKey{"route.mark", "active"};
    rule.family = FirewallFamily::ipv4;
    rule.criteria.dst_set_name = "kpbr4_list";
    rule.action = MarkAction{0x10000u};
    FirewallRuleRegistrar registrar(plan);
    registrar.register_rule(std::move(rule));
    registrar.finish();
    return plan;
}

FirewallPlan active_direct_plan() {
    FirewallPlan plan;
    FirewallRuleInstance rule;
    rule.key = FirewallRuleKey{"route.mark", "direct"};
    rule.family = FirewallFamily::ipv4;
    rule.criteria.dst_addr = {"192.0.2.0/24"};
    rule.action = MarkAction{0x10000u};
    FirewallRuleRegistrar registrar(plan);
    registrar.register_rule(std::move(rule));
    registrar.finish();
    return plan;
}

// What the nft backend publishes with an apply: the lowered ruleset of the
// plan, computed once.
ActiveFirewall active_nft(FirewallPlan plan, std::vector<RuleState> states) {
    FirewallLoweringContext context;
    context.backend = FirewallBackend::nftables;
    context.fwmark_mask = plan.fwmark_mask;
    FirewallApplyResult result;
    result.expected_ruleset = std::make_shared<const PhysicalRuleset>(
        lower_firewall_plan(plan, context));
    return ActiveFirewall{std::move(plan), std::move(result),
                          std::move(states)};
}

const char* active_mark_snapshot() {
    return R"({"nftables":[
      {"table":{"family":"inet","name":"KeenPbrTable"}},
      {"chain":{"family":"inet","table":"KeenPbrTable","name":"prerouting",
                  "type":"filter","hook":"prerouting","prio":-150,
                  "policy":"accept"}},
      {"chain":{"family":"inet","table":"KeenPbrTable","name":"output",
                  "type":"route","hook":"output","prio":-150,
                  "policy":"accept"}},
      {"rule":{"family":"inet","table":"KeenPbrTable","chain":"prerouting",
                "comment":"kpbr:v1:route.mark:active","expr":[
                  {"match":{"op":"==","left":{"payload":{"protocol":"ip","field":"daddr"}},
                             "right":"@kpbr4_list"}},
                  {"mangle":{"key":{"meta":{"key":"mark"}},"value":65536}},
                  {"accept":null}]}},
      {"chain":{"family":"inet","table":"KeenPbrTable","name":"setmark_00010000"}},
      {"rule":{"family":"inet","table":"KeenPbrTable","chain":"setmark_00010000",
                "expr":[
                  {"mangle":{"key":{"meta":{"key":"mark"}},"value":65536}},
                  {"mangle":{"key":{"ct":{"key":"mark"}},"value":65536}},
                  {"accept":null}]}}
    ]})";
}

} // namespace

TEST_CASE("routing health compares the active plan instead of RuleState") {
    FirewallState state;
    auto plan = active_mark_plan();
    RuleState projection{};
    projection.action_type = RuleActionType::Mark;
    projection.fwmark = 0x20000u;
    state.publish_active_firewall(active_nft(std::move(plan), {projection}));

    int calls = 0;
    NetlinkManager netlink;
    const auto report = build_routing_health_report(
        FirewallBackend::nftables, RawPreroutingMode{}, state, {}, {}, netlink,
        [&calls](const std::vector<std::string>&) {
            ++calls;
            return command_result(active_mark_snapshot());
        });

    CHECK(calls == 1);
    REQUIRE(report.firewall_rules.size() == 1);
    CHECK(report.firewall_rules.front().status == CheckStatus::ok);
    CHECK(report.overall_ok);
}

TEST_CASE("routing health is explicitly not ready without an active plan") {
    FirewallState state;
    int calls = 0;
    NetlinkManager netlink;
    const auto report = build_routing_health_report(
        FirewallBackend::nftables, RawPreroutingMode{}, state, {}, {}, netlink,
        [&calls](const std::vector<std::string>&) {
            ++calls;
            return command_result(active_mark_snapshot());
        });

    CHECK(calls == 0);
    CHECK_FALSE(report.overall_ok);
    CHECK(report.firewall_rules.empty());
    CHECK(report.firewall_chain.detail.find("not ready") != std::string::npos);
}

TEST_CASE("routing health reports an active direct rule that the kernel lacks") {
    FirewallState state;
    state.publish_active_firewall(active_nft(active_direct_plan(), {}));

    NetlinkManager netlink;
    const auto report = build_routing_health_report(
        FirewallBackend::nftables, RawPreroutingMode{}, state, {}, {}, netlink,
        [](const std::vector<std::string>&) {
            return command_result(active_mark_snapshot());
        });

    bool direct_rule_missing = false;
    for (const auto& check : report.firewall_rules) {
        if (check.detail.find("key=route.mark:direct") == std::string::npos) {
            continue;
        }
        direct_rule_missing = true;
        // The kernel holds a different rule in its place.
        CHECK(check.status == CheckStatus::mismatch);
        CHECK(check.detail.find("192.0.2.0/24") != std::string::npos);
    }
    CHECK(direct_rule_missing);
}

} // namespace keen_pbr3
