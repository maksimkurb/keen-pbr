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

} // namespace keen_pbr3
