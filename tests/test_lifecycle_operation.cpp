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

TEST_CASE("LifecycleOperationCoordinator skips every remaining stage after failure") {
    LifecycleOperationStore store;
    LifecycleOperationCoordinator coordinator(store);
    LifecycleOperationSnapshot operation;
    REQUIRE_FALSE(coordinator.begin(
        LifecycleOperationType::ApplyConfig,
        {{"validate_config", "Validate"}, {"reconcile_runtime", "Reconcile"},
         {"commit_config", "Commit"}}, operation));

    coordinator.start_stage(operation.id, "validate_config");
    coordinator.fail_stage(operation.id, "validate_config", "invalid config");
    coordinator.finish(operation.id, "invalid config");

    const auto snapshot = store.snapshot();
    REQUIRE(snapshot);
    CHECK(snapshot->stages[0].status == LifecycleOperationStatus::Failed);
    CHECK(snapshot->stages[1].status == LifecycleOperationStatus::Skipped);
    CHECK(snapshot->stages[2].status == LifecycleOperationStatus::Skipped);
}

TEST_CASE("LifecycleOperationStore publishes every lifecycle mutation") {
    LifecycleOperationStore store;
    int publications = 0;
    store.set_publish_callback([&publications] { ++publications; });
    LifecycleOperationCoordinator coordinator(store);
    LifecycleOperationSnapshot operation;
    REQUIRE_FALSE(coordinator.begin(LifecycleOperationType::Start,
                                    {{"start_routing", "Start"}}, operation));
    coordinator.start_stage(operation.id, "start_routing");
    coordinator.succeed_stage(operation.id, "start_routing");
    coordinator.finish(operation.id);
    CHECK(publications == 4);
}

TEST_CASE("LifecycleOperationCoordinator stage transitions are monotonic") {
    LifecycleOperationStore store;
    LifecycleOperationCoordinator coordinator(store);
    LifecycleOperationSnapshot operation;
    REQUIRE_FALSE(coordinator.begin(LifecycleOperationType::Start,
                                    {{"start_routing", "Start"}}, operation));
    coordinator.start_stage(operation.id, "start_routing");
    coordinator.succeed_stage(operation.id, "start_routing");
    coordinator.fail_stage(operation.id, "start_routing", "late failure");
    coordinator.skip_stage(operation.id, "start_routing", "late skip");
    const auto snapshot = store.snapshot();
    REQUIRE(snapshot);
    CHECK(snapshot->stages[0].status == LifecycleOperationStatus::Succeeded);
    CHECK(snapshot->error.empty());
}

} // namespace keen_pbr3
