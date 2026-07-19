#include <doctest/doctest.h>

#include "runtime/runtime_reconciler.hpp"

#include <stdexcept>
#include <utility>

namespace keen_pbr3 {
namespace {

class LambdaOperation final : public RuntimeOperation {
public:
    explicit LambdaOperation(std::function<void()> action) : action_(std::move(action)) {}
    void apply() const override { action_(); }
private:
    std::function<void()> action_;
};

class FakeSubsystem final : public RuntimeSubsystem {
public:
    RuntimeComponent runtime_component{RuntimeComponent::routing};
    RuntimeActualState actual;
    mutable int inspect_calls{0};
    mutable int plan_calls{0};
    mutable int verify_calls{0};
    std::vector<RuntimeOperationPtr> next_plan;
    bool verified{true};
    mutable const RuntimeDesiredState* planned_desired{nullptr};

    RuntimeComponent component() const override { return runtime_component; }

    RuntimeActualState inspect() const override {
        ++inspect_calls;
        return actual;
    }
    std::vector<RuntimeOperationPtr> plan(const RuntimeDesiredState& desired,
                                          const RuntimeActualState&) const override {
        ++plan_calls;
        planned_desired = &desired;
        return next_plan;
    }
    bool verify(const RuntimeDesiredState&, const RuntimeActualState&, std::string& error) const override {
        ++verify_calls;
        if (!verified) error = "injected drift";
        return verified;
    }
};

RuntimeDesiredState desired_state() {
    RuntimeDesiredState desired;
    desired.routing.routes = {"route-a"};
    desired.firewall.sets = {"set-a"};
    desired.resolver.configuration_id = "resolver-a";
    desired.conntrack.policy_id = "conntrack-a";
    return desired;
}

} // namespace

TEST_CASE("runtime reconciler commits an empty plan without mutations") {
    FakeSubsystem subsystem;
    RuntimeReconciler reconciler({subsystem});
    bool committed = false;

    const auto result = reconciler.reconcile(desired_state(), [&] { committed = true; });

    CHECK(result.committed);
    CHECK(result.operation_count == 0);
    CHECK(committed);
    CHECK(subsystem.plan_calls == 1);
    CHECK(subsystem.verify_calls == 1);
}

TEST_CASE("runtime reconciliation discards failed attempt operations") {
    FakeSubsystem subsystem;
    int first_operation_runs = 0;
    int stale_operation_runs = 0;
    subsystem.next_plan = {
        std::make_shared<LambdaOperation>([&] { ++first_operation_runs; }),
        std::make_shared<LambdaOperation>([] { throw std::runtime_error("apply failed"); }),
        std::make_shared<LambdaOperation>([&] { ++stale_operation_runs; }),
    };
    RuntimeReconciler reconciler({subsystem});

    const auto failed = reconciler.reconcile(desired_state());
    CHECK_FALSE(failed.committed);
    CHECK(first_operation_runs == 1);
    CHECK(stale_operation_runs == 0);

    subsystem.next_plan = {std::make_shared<LambdaOperation>([&] { ++first_operation_runs; })};
    const auto retried = reconciler.reconcile(desired_state());
    CHECK(retried.committed);
    CHECK(first_operation_runs == 2);
    CHECK(stale_operation_runs == 0);
}

TEST_CASE("all runtime subsystems plan from one desired state snapshot") {
    FakeSubsystem routing;
    FakeSubsystem firewall;
    FakeSubsystem resolver;
    FakeSubsystem conntrack;
    firewall.runtime_component = RuntimeComponent::firewall;
    resolver.runtime_component = RuntimeComponent::resolver;
    conntrack.runtime_component = RuntimeComponent::conntrack;
    RuntimeReconciler reconciler({routing, firewall, resolver, conntrack});
    const auto desired = desired_state();

    CHECK(reconciler.reconcile(desired).committed);
    CHECK(routing.planned_desired == &desired);
    CHECK(firewall.planned_desired == &desired);
    CHECK(resolver.planned_desired == &desired);
    CHECK(conntrack.planned_desired == &desired);
}

TEST_CASE("runtime reconciler detects drift before commit") {
    FakeSubsystem subsystem;
    subsystem.verified = false;
    RuntimeReconciler reconciler({subsystem});
    bool committed = false;

    const auto result = reconciler.reconcile(desired_state(), [&] { committed = true; });

    CHECK_FALSE(result.committed);
    CHECK(result.drift_detected);
    CHECK_FALSE(committed);
    CHECK(result.error == "injected drift");
}

TEST_CASE("runtime reconciler status inspection does not mutate") {
    FakeSubsystem subsystem;
    subsystem.runtime_component = RuntimeComponent::resolver;
    subsystem.actual.resolver.configuration_id = "live";
    RuntimeReconciler reconciler({subsystem});

    const auto actual = reconciler.inspect();

    CHECK(actual.resolver.configuration_id == "live");
    CHECK(subsystem.inspect_calls == 1);
    CHECK(subsystem.plan_calls == 0);
    CHECK(subsystem.verify_calls == 0);
}

} // namespace keen_pbr3
