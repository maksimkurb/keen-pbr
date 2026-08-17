#include <doctest/doctest.h>

#include "daemon/runtime_state_store.hpp"

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
