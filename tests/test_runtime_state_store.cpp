#include <doctest/doctest.h>

#include "daemon/runtime_state_store.hpp"

#include <atomic>
#include <thread>

namespace keen_pbr3 {

TEST_CASE("RuntimeStateStore publishes runtime state and transition reason") {
    RuntimeStateStore store;
    RuntimeStateSnapshot expected;
    expected.runtime_state = RuntimeState::broken;
    expected.runtime_state_reason = "rollback failed";

    store.publish(expected);
    const auto actual = store.snapshot();

    CHECK(actual.runtime_state == RuntimeState::broken);
    CHECK(actual.runtime_state_reason == "rollback failed");
}

TEST_CASE("RuntimeStateStore publishes active plan and API projection together") {
    RuntimeStateStore store;
    RuntimeStateSnapshot state;

    FirewallPlan plan;
    plan.fwmark_mask = 0x00FF0000u;
    FirewallRuleInstance planned_rule;
    planned_rule.key = FirewallRuleKey{"route.mark", "active"};
    planned_rule.family = FirewallFamily::ipv4;
    planned_rule.action = MarkAction{0x00010000u, plan.fwmark_mask};
    FirewallRuleRegistrar registrar(plan);
    registrar.register_rule(std::move(planned_rule));
    registrar.finish();

    RuleState projection{};
    projection.rule_index = 3;
    projection.action_type = RuleActionType::Mark;
    projection.fwmark = 0x00010000u;
    state.firewall_state.set_active_plan(std::move(plan), {projection});
    store.publish(std::move(state));

    const auto snapshot = store.snapshot();
    REQUIRE(snapshot.firewall_state.get_active_plan().has_value());
    CHECK(snapshot.firewall_state.get_active_plan()->rules.size() == 1);
    REQUIRE(snapshot.firewall_state.get_rules().size() == 1);
    CHECK(snapshot.firewall_state.get_rules().front().rule_index == 3);
    CHECK(snapshot.firewall_state.get_rules().front().fwmark ==
          std::get<MarkAction>(snapshot.firewall_state.get_active_plan()->rules.front().action).value);
}

TEST_CASE("FirewallState clears active plan and projection after teardown") {
    FirewallState state;
    FirewallPlan plan;
    FirewallRuleInstance rule;
    rule.key = FirewallRuleKey{"route.mark", "teardown"};
    rule.action = MarkAction{1};
    FirewallRuleRegistrar registrar(plan);
    registrar.register_rule(std::move(rule));
    registrar.finish();
    state.set_active_plan(std::move(plan), {RuleState{}});

    state.clear_active_plan();

    CHECK_FALSE(state.get_active_plan().has_value());
    CHECK(state.get_rules().empty());
}

TEST_CASE("RuntimeStateStore readers never observe a mixed plan projection") {
    RuntimeStateSnapshot first;
    FirewallPlan first_plan;
    first_plan.fwmark_mask = 0xFFFFFFFFu;
    FirewallRuleInstance first_rule;
    first_rule.key = FirewallRuleKey{"route.mark", "first"};
    first_rule.family = FirewallFamily::ipv4;
    first_rule.action = MarkAction{1};
    FirewallRuleRegistrar first_registrar(first_plan);
    first_registrar.register_rule(std::move(first_rule));
    first_registrar.finish();
    RuleState first_projection{};
    first_projection.action_type = RuleActionType::Mark;
    first_projection.fwmark = 1;
    first.firewall_state.set_active_plan(std::move(first_plan),
                                         {first_projection});

    RuntimeStateSnapshot second = first;
    FirewallPlan second_plan;
    FirewallRuleInstance second_rule;
    second_rule.key = FirewallRuleKey{"route.mark", "second"};
    second_rule.family = FirewallFamily::ipv4;
    second_rule.action = MarkAction{2};
    FirewallRuleRegistrar second_registrar(second_plan);
    second_registrar.register_rule(std::move(second_rule));
    second_registrar.finish();
    RuleState second_projection{};
    second_projection.action_type = RuleActionType::Mark;
    second_projection.fwmark = 2;
    second.firewall_state.set_active_plan(std::move(second_plan),
                                          {second_projection});

    RuntimeStateStore store;
    store.publish(first);
    std::atomic<bool> done{false};
    std::atomic<bool> consistent{true};
    std::thread writer([&] {
        for (int index = 0; index < 1000; ++index) {
            store.publish(index % 2 == 0 ? first : second);
        }
        done.store(true, std::memory_order_release);
    });
    while (!done.load(std::memory_order_acquire)) {
        const auto snapshot = store.snapshot();
        const auto& active = snapshot.firewall_state.get_active_plan();
        if (!active.has_value() || active->rules.size() != 1 ||
            snapshot.firewall_state.get_rules().size() != 1 ||
            std::get<MarkAction>(active->rules.front().action).value !=
                snapshot.firewall_state.get_rules().front().fwmark) {
            consistent.store(false, std::memory_order_release);
            break;
        }
    }
    writer.join();
    CHECK(consistent.load(std::memory_order_acquire));
}

TEST_CASE("RuntimeStateStore exposes only compact realized rule data to control clients") {
    RuntimeStateStore store;
    RuntimeStateSnapshot state;
    RuleState rule{};
    rule.rule_index = 7;
    rule.list_names = {"large-source-list-name"};
    rule.set_names = {"kpbr_a_7"};
    rule.outbound_tag = "vpn";
    rule.action_type = RuleActionType::Mark;
    rule.fwmark = 0x10000;
    state.firewall_state.set_rules({rule});
    state.runtime_state = RuntimeState::running;
    store.publish(std::move(state));

    const auto compact = store.control_snapshot(true);
    REQUIRE(compact.realized_rules.size() == 1);
    CHECK(compact.realized_rules[0].rule_index == 7);
    CHECK(compact.realized_rules[0].set_names == std::vector<std::string>{"kpbr_a_7"});
    CHECK(compact.realized_rules[0].outbound_tag == "vpn");
    CHECK(compact.realized_rules[0].fwmark == 0x10000);
    CHECK(store.control_snapshot(false).realized_rules.empty());
}

TEST_CASE("RuntimeStateStore resolver updates preserve routing and urltest state") {
    RuntimeStateStore store;
    RuntimeStateSnapshot state;
    RouteSpec route;
    route.destination = "default";
    route.table = 100;
    state.route_specs.push_back(route);
    RuleSpec rule;
    rule.fwmark = 1;
    rule.table = 100;
    state.policy_rule_specs.push_back(rule);
    state.urltest_states.emplace("auto", UrltestState{});
    store.publish(std::move(state));

    ResolverRuntimeStateUpdate update;
    update.resolver_config_hash = "expected";
    update.resolver_config_hash_actual = "actual";
    update.resolver_last_probe_ts = 123;
    update.resolver_live_status = api::ResolverLiveStatus::HEALTHY;
    store.update_resolver(std::move(update));

    const auto full = store.snapshot();
    CHECK(full.route_specs.size() == 1);
    CHECK(full.policy_rule_specs.size() == 1);
    CHECK(full.urltest_states.count("auto") == 1);
    const auto service = store.service_snapshot();
    CHECK(service.resolver_config_hash == "expected");
    CHECK(service.resolver_config_hash_actual == "actual");
    CHECK(service.resolver_last_probe_ts == 123);
}

TEST_CASE("RuntimeStateStore updates one urltest without changing other state") {
    RuntimeStateStore store;
    RuntimeStateSnapshot state;
    state.resolver_config_hash = "resolver";
    state.urltest_states.emplace("other", UrltestState{});
    store.publish(std::move(state));

    UrltestState replacement;
    store.update_urltest("auto", replacement);
    const auto outbound = store.outbound_snapshot();
    CHECK(outbound.urltest_states.count("auto") == 1);
    CHECK(outbound.urltest_states.count("other") == 1);
    CHECK(store.service_snapshot().resolver_config_hash == "resolver");

    store.update_urltest("auto", std::nullopt);
    CHECK(store.outbound_snapshot().urltest_states.count("auto") == 0);
}

} // namespace keen_pbr3
