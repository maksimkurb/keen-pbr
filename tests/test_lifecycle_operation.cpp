#include <doctest/doctest.h>

#include "runtime/lifecycle_operation.hpp"

namespace keen_pbr3 {

TEST_CASE("LifecycleOperationCoordinator serializes operations and persists the final snapshot") {
    LifecycleOperationStore store;
    LifecycleOperationCoordinator coordinator(store);
    LifecycleOperationSnapshot first;
    CHECK_FALSE(coordinator.begin(LifecycleOperationType::ApplyConfig,
                                  {{"prepare", "Prepare"}, {"commit", "Commit"}}, first));
    LifecycleOperationSnapshot ignored;
    const auto active = coordinator.begin(LifecycleOperationType::Restart, {}, ignored);
    REQUIRE(active);
    CHECK(*active == first.id);

    coordinator.start_stage(first.id, "prepare");
    coordinator.succeed_stage(first.id, "prepare");
    coordinator.skip_stage(first.id, "commit", "nothing to commit");
    coordinator.finish(first.id);

    const auto snapshot = store.snapshot();
    REQUIRE(snapshot);
    CHECK(snapshot->result == LifecycleOperationResult::Succeeded);
    CHECK(snapshot->finished_at.has_value());
    CHECK(snapshot->stages[0].status == LifecycleOperationStatus::Succeeded);
    CHECK(snapshot->stages[1].status == LifecycleOperationStatus::Skipped);
}

TEST_CASE("LifecycleOperationCoordinator records a failing stage") {
    LifecycleOperationStore store;
    LifecycleOperationCoordinator coordinator(store);
    LifecycleOperationSnapshot operation;
    REQUIRE_FALSE(coordinator.begin(LifecycleOperationType::Start, {{"resolver", "Verify"}}, operation));
    coordinator.fail_stage(operation.id, "resolver", "dnsmasq process disappeared");
    coordinator.finish(operation.id, "dnsmasq process disappeared");

    const auto snapshot = store.snapshot();
    REQUIRE(snapshot);
    CHECK(snapshot->result == LifecycleOperationResult::Failed);
    CHECK(snapshot->error == "dnsmasq process disappeared");
    CHECK(snapshot->stages[0].status == LifecycleOperationStatus::Failed);
}

} // namespace keen_pbr3
