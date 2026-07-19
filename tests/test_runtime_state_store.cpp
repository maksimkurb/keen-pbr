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

} // namespace keen_pbr3
