#include "runtime_reconciler.hpp"

#include <exception>
#include <utility>

namespace keen_pbr3 {

namespace {

void merge_component(RuntimeActualState& destination,
                     const RuntimeActualState& source,
                     RuntimeComponent component) {
    switch (component) {
    case RuntimeComponent::routing:
        destination.routing = source.routing;
        break;
    case RuntimeComponent::firewall:
        destination.firewall = source.firewall;
        break;
    case RuntimeComponent::resolver:
        destination.resolver = source.resolver;
        break;
    case RuntimeComponent::conntrack:
        destination.conntrack = source.conntrack;
        break;
    }
}

} // namespace

RuntimeReconciler::RuntimeReconciler(
    std::vector<std::reference_wrapper<RuntimeSubsystem>> subsystems)
    : subsystems_(std::move(subsystems)) {}

RuntimeReconcileResult RuntimeReconciler::reconcile(const RuntimeDesiredState& desired,
                                                    const std::function<void()>& commit) {
    std::vector<RuntimeOperationPtr> attempt_plan;
    try {
        for (RuntimeSubsystem& subsystem : subsystems_) {
            const RuntimeActualState actual = subsystem.inspect();
            std::vector<RuntimeOperationPtr> operations = subsystem.plan(desired, actual);
            attempt_plan.insert(attempt_plan.end(), operations.begin(), operations.end());
        }

        for (const RuntimeOperationPtr& operation : attempt_plan) {
            operation->apply();
        }

        for (RuntimeSubsystem& subsystem : subsystems_) {
            std::string verification_error;
            if (!subsystem.verify(desired, subsystem.inspect(), verification_error)) {
                RuntimeReconcileResult result;
                result.drift_detected = true;
                result.error = verification_error.empty() ? "runtime state drift detected"
                                                          : std::move(verification_error);
                result.operation_count = attempt_plan.size();
                return result;
            }
        }

        if (commit) {
            commit();
        }
        RuntimeReconcileResult result;
        result.committed = true;
        result.operation_count = attempt_plan.size();
        return result;
    } catch (const std::exception& error) {
        RuntimeReconcileResult result;
        result.error = error.what();
        result.operation_count = attempt_plan.size();
        return result;
    } catch (...) {
        RuntimeReconcileResult result;
        result.error = "unknown runtime reconciliation error";
        result.operation_count = attempt_plan.size();
        return result;
    }
}

RuntimeActualState RuntimeReconciler::inspect() const {
    RuntimeActualState result;
    for (RuntimeSubsystem& subsystem : subsystems_) {
        merge_component(result, subsystem.inspect(), subsystem.component());
    }
    return result;
}

} // namespace keen_pbr3
