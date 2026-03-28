#include "urltest_manager.hpp"
#include "../daemon/scheduler.hpp"

#include <algorithm>
#include <limits>

namespace keen_pbr3 {

UrltestManager::UrltestManager(URLTester& tester, const OutboundMarkMap& marks,
                               Scheduler& scheduler, UrltestChangeCallback on_change)
    : tester_(tester), marks_(marks), scheduler_(scheduler),
      on_change_(std::move(on_change)) {}

UrltestManager::~UrltestManager() {
    try {
        clear();
    } catch (...) {
        // Suppress exceptions in destructor
    }
}

void UrltestManager::register_urltest(const Outbound& ut) {
    std::unique_lock<std::shared_mutex> lock(mutex_);

    UrltestState state;
    state.config = ut;

    // Create a circuit breaker per child outbound
    for (const auto& group : ut.outbound_groups.value_or(std::vector<OutboundGroup>{})) {
        for (const auto& child_tag : group.outbounds) {
            state.circuit_breakers.emplace(child_tag, CircuitBreaker(ut.circuit_breaker.value_or(CircuitBreakerConfig{})));
        }
    }

    // Schedule periodic tests. Scheduler uses seconds, urltest uses milliseconds.
    auto interval_sec = std::chrono::seconds(ut.interval_ms.value_or(180000) / 1000);
    if (interval_sec.count() < 1) interval_sec = std::chrono::seconds(1);

    std::string tag = ut.tag;
    state.scheduler_task_id = scheduler_.schedule_repeating(interval_sec, [this, tag]() {
        run_tests(tag);
    });

    states_.emplace(ut.tag, std::move(state));

    // Run initial test immediately (lock already held)
    run_tests_locked(ut.tag);
}

void UrltestManager::trigger_immediate_test(const std::string& urltest_tag) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = states_.find(urltest_tag);
    if (it != states_.end()) {
        run_tests_locked(urltest_tag);
    }
}

std::string UrltestManager::get_selected(const std::string& urltest_tag) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = states_.find(urltest_tag);
    if (it == states_.end()) {
        return "";
    }
    return it->second.selected_outbound;
}

UrltestState UrltestManager::get_state(const std::string& urltest_tag) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return states_.at(urltest_tag);
}

void UrltestManager::clear() {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    for (auto& [tag, state] : states_) {
        if (state.scheduler_task_id >= 0) {
            scheduler_.cancel(state.scheduler_task_id);
        }
    }
    states_.clear();
}

void UrltestManager::run_tests(const std::string& tag) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    run_tests_locked(tag);
}

void UrltestManager::run_tests_locked(const std::string& tag) {
    auto it = states_.find(tag);
    if (it == states_.end()) return;

    auto& state = it->second;
    const auto& ut = state.config;

    // Test each child outbound
    for (const auto& group : ut.outbound_groups.value_or(std::vector<OutboundGroup>{})) {
        for (const auto& child_tag : group.outbounds) {
            auto mark_it = marks_.find(child_tag);
            if (mark_it == marks_.end()) {
                // No fwmark for this outbound (e.g. blackhole) — skip testing
                continue;
            }

            auto& cb = state.circuit_breakers.at(child_tag);

            if (!cb.is_allowed(child_tag)) {
                // Circuit breaker is open, skip testing
                continue;
            }

            cb.begin_request(child_tag);
            const auto& cb_cfg = ut.circuit_breaker.value_or(CircuitBreakerConfig{});
            auto result = tester_.test(ut.url.value_or(""), mark_it->second,
                                       cb_cfg.timeout_ms.value_or(5000), ut.retry.value_or(RetryConfig{}));
            cb.end_request(child_tag);

            if (result.success) {
                cb.record_success(child_tag);
            } else {
                cb.record_failure(child_tag);
            }

            state.last_results[child_tag] = result;
        }
    }

    // Select best outbound after all tests complete
    auto new_selected = select_outbound(tag);
    if (new_selected != state.selected_outbound) {
        std::string old_selected = state.selected_outbound;
        state.selected_outbound = new_selected;
        if (on_change_) {
            on_change_(tag, new_selected);
        }
    }
}

std::string UrltestManager::select_outbound(const std::string& tag) {
    auto it = states_.find(tag);
    if (it == states_.end()) return "";

    const auto& state = it->second;
    const auto& ut = state.config;

    if (!ut.outbound_groups.has_value()) return "";
    const auto& groups = *ut.outbound_groups;

    // Sort groups by weight ascending. We need indices to reference them.
    struct GroupRef {
        size_t index;
        uint32_t weight;
    };
    std::vector<GroupRef> sorted_groups;
    sorted_groups.reserve(groups.size());
    for (size_t i = 0; i < groups.size(); ++i) {
        sorted_groups.push_back({i, static_cast<uint32_t>(groups[i].weight.value_or(1))});
    }
    std::sort(sorted_groups.begin(), sorted_groups.end(),
              [](const GroupRef& a, const GroupRef& b) { return a.weight < b.weight; });

    // For each group (ascending weight), filter by circuit breaker allowed,
    // find fastest within tolerance
    for (const auto& gref : sorted_groups) {
        const auto& group = groups[gref.index];

        // Find min latency among allowed outbounds in this group
        uint32_t min_latency = std::numeric_limits<uint32_t>::max();
        for (const auto& child_tag : group.outbounds) {
            auto cb_it = state.circuit_breakers.find(child_tag);
            if (cb_it == state.circuit_breakers.end()) continue;

            // Use a const-safe check: only consider closed or half_open states
            auto cb_state = cb_it->second.state(child_tag);
            if (cb_state == CircuitState::open) continue;

            auto res_it = state.last_results.find(child_tag);
            if (res_it == state.last_results.end() || !res_it->second.success) continue;

            if (res_it->second.latency_ms < min_latency) {
                min_latency = res_it->second.latency_ms;
            }
        }

        if (min_latency == std::numeric_limits<uint32_t>::max()) {
            // No allowed outbound with successful test in this group, try next group
            continue;
        }

        uint32_t tolerance = static_cast<uint32_t>(ut.tolerance_ms.value_or(100));

        // Prefer the currently selected outbound if it belongs to this group
        // and is still within tolerance — avoids unnecessary switching
        if (!state.selected_outbound.empty()) {
            auto incumbent_in_group = std::find(group.outbounds.begin(), group.outbounds.end(), state.selected_outbound);
            if (incumbent_in_group != group.outbounds.end()) {
                auto cb_it = state.circuit_breakers.find(state.selected_outbound);
                if (cb_it != state.circuit_breakers.end() && cb_it->second.state(state.selected_outbound) != CircuitState::open) {
                    auto res_it = state.last_results.find(state.selected_outbound);
                    if (res_it != state.last_results.end() && res_it->second.success &&
                        res_it->second.latency_ms <= min_latency + tolerance) {
                        return state.selected_outbound;
                    }
                }
            }
        }

        // Find first outbound within tolerance of min_latency
        for (const auto& child_tag : group.outbounds) {
            auto cb_it = state.circuit_breakers.find(child_tag);
            if (cb_it == state.circuit_breakers.end()) continue;

            auto cb_state = cb_it->second.state(child_tag);
            if (cb_state == CircuitState::open) continue;

            auto res_it = state.last_results.find(child_tag);
            if (res_it == state.last_results.end() || !res_it->second.success) continue;

            if (res_it->second.latency_ms <= min_latency + tolerance) {
                return child_tag;
            }
        }
    }

    // All exhausted — return empty string (blackhole fallback)
    return "";
}

} // namespace keen_pbr3
