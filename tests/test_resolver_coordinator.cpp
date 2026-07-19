#include <doctest/doctest.h>

#include "runtime/resolver_coordinator.hpp"

namespace keen_pbr3 {

TEST_CASE("ResolverCoordinator tracks desired and independently observed metadata") {
    ResolverCoordinator coordinator;
    CHECK(coordinator.reconcile("expected"));
    CHECK_FALSE(coordinator.reconcile("expected"));
    coordinator.observe_actual("actual");

    const auto state = coordinator.inspect();
    CHECK(state.expected_config_hash == "expected");
    CHECK(state.actual_config_hash == "actual");
    coordinator.clear_actual();
    CHECK(coordinator.inspect().actual_config_hash.empty());
}

} // namespace keen_pbr3
