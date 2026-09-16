#include <doctest/doctest.h>

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

const char* active_mark_snapshot() {
    return R"({"nftables":[
      {"table":{"family":"inet","name":"KeenPbrTable"}},
      {"chain":{"family":"inet","table":"KeenPbrTable","name":"prerouting",
                  "type":"filter","hook":"prerouting"}},
      {"rule":{"family":"inet","table":"KeenPbrTable","chain":"prerouting",
                "comment":"kpbr:v1:route.mark:active","expr":[
                  {"match":{"op":"==","left":{"payload":{"protocol":"ip","field":"daddr"}},
                             "right":"@kpbr4_list"}},
                  {"mangle":{"key":{"meta":{"key":"mark"}},"value":65536}},
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
    state.set_active_plan(std::move(plan), {projection});

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

TEST_CASE("routing health reports missing active direct criteria") {
    FirewallState state;
    state.set_active_plan(active_direct_plan(), {});

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
        CHECK(check.status == CheckStatus::missing);
    }
    CHECK(direct_rule_missing);
}

} // namespace keen_pbr3
