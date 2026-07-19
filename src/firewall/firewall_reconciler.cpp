#include "firewall_reconciler.hpp"

#include <exception>
#include <algorithm>
#include <sstream>

namespace keen_pbr3 {

namespace {

std::vector<std::string> missing_from(const std::vector<std::string>& expected,
                                      const std::vector<std::string>& actual) {
    std::vector<std::string> result;
    for (const auto& value : expected) {
        if (std::find(actual.begin(), actual.end(), value) == actual.end()) result.push_back(value);
    }
    return result;
}

const FirewallSetSnapshot* find_set(const std::vector<FirewallSetSnapshot>& sets,
                                    const std::string& name) {
    const auto it = std::find_if(sets.begin(), sets.end(), [&](const FirewallSetSnapshot& set) {
        return set.name == name;
    });
    return it == sets.end() ? nullptr : &*it;
}

} // namespace

bool FirewallStateDiff::empty() const {
    return missing_chains.empty() && extra_chains.empty() &&
           missing_jumps.empty() && extra_jumps.empty() &&
           missing_sets.empty() && extra_sets.empty() &&
           schema_mismatches.empty() && !rules_reordered;
}

std::string FirewallStateDiff::summary() const {
    std::vector<std::string> issues;
    if (!missing_chains.empty()) issues.push_back("missing chain");
    if (!extra_chains.empty()) issues.push_back("extra chain");
    if (!missing_jumps.empty()) issues.push_back("missing jump");
    if (!extra_jumps.empty()) issues.push_back("extra jump");
    if (!missing_sets.empty()) issues.push_back("missing set");
    if (!extra_sets.empty()) issues.push_back("extra set");
    if (!schema_mismatches.empty()) issues.push_back("set schema mismatch");
    if (rules_reordered) issues.push_back("rule order mismatch");
    std::ostringstream output;
    for (size_t index = 0; index < issues.size(); ++index) {
        if (index != 0) output << "; ";
        output << issues[index];
    }
    return output.str();
}

FirewallStateDiff diff_firewall_state(const FirewallDesiredState& desired,
                                      const FirewallActualState& actual) {
    FirewallStateDiff diff;
    diff.missing_chains = missing_from(desired.chains, actual.chains);
    diff.extra_chains = missing_from(actual.chains, desired.chains);
    diff.missing_jumps = missing_from(desired.jumps, actual.jumps);
    diff.extra_jumps = missing_from(actual.jumps, desired.jumps);
    diff.rules_reordered = desired.ordered_rules != actual.ordered_rules;

    for (const auto& expected : desired.sets) {
        const auto* observed = find_set(actual.sets, expected.name);
        if (!observed) {
            diff.missing_sets.push_back(expected.name);
        } else if (observed->family != expected.family ||
                   observed->timeout_seconds != expected.timeout_seconds ||
                   observed->dynamic != expected.dynamic) {
            diff.schema_mismatches.push_back(expected.name);
        }
    }
    for (const auto& observed : actual.sets) {
        if (!find_set(desired.sets, observed.name)) diff.extra_sets.push_back(observed.name);
    }
    return diff;
}

FirewallReconcileResult FirewallReconciler::reconcile(const FirewallDesiredState& desired) {
    try {
        std::string probe_error;
        if (!backend_.probe(probe_error)) {
            return {.error = probe_error.empty() ? "firewall backend is unavailable" : probe_error};
        }

        const FirewallActualState actual = backend_.inspect();
        std::vector<FirewallOperation> plan = backend_.plan(desired, actual);
        for (const auto& operation : plan) {
            operation.apply();
        }

        std::string verification_error;
        if (!backend_.verify(desired, backend_.inspect(), verification_error)) {
            return {.drift_detected = true,
                    .error = verification_error.empty() ? "firewall state drift detected"
                                                       : verification_error,
                    .operation_count = plan.size()};
        }
        return {.committed = true, .operation_count = plan.size()};
    } catch (const std::exception& error) {
        return {.error = error.what()};
    } catch (...) {
        return {.error = "unknown firewall reconciliation error"};
    }
}

} // namespace keen_pbr3
