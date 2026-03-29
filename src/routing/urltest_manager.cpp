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
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);

        UrltestState state;
        state.config = ut;

        for (const auto& group : ut.outbound_groups.value_or(std::vector<OutboundGroup>{})) {
            for (const auto& child_tag : group.outbounds) {
                state.circuit_breakers.emplace(child_tag,
                    CircuitBreaker(ut.circuit_breaker.value_or(CircuitBreakerConfig{})));
            }
        }

        auto interval_sec = std::chrono::seconds(ut.interval_ms.value_or(180000) / 1000);
        if (interval_sec.count() < 1) interval_sec = std::chrono::seconds(1);

        const std::string tag = ut.tag;
        state.scheduler_task_id = scheduler_.schedule_repeating(interval_sec, [this, tag]() {
            run_tests(tag);
        });

        states_.emplace(ut.tag, std::move(state));
    }

    // Run the initial test after releasing the lock. on_change_ is intentionally
    // not called here — the caller reads the initial selection via get_selected().
    run_tests_unlocked(ut.tag);
}

void UrltestManager::trigger_immediate_test(const std::string& urltest_tag) {
    auto changed = run_tests_unlocked(urltest_tag);
    if (changed && on_change_) {
        on_change_(urltest_tag, *changed);
    }
}

std::string UrltestManager::get_selected(const std::string& urltest_tag) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = states_.find(urltest_tag);
    if (it == states_.end()) return "";
    return it->second.selected_outbound;
}

std::optional<UrltestState> UrltestManager::get_state(const std::string& urltest_tag) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = states_.find(urltest_tag);
    if (it == states_.end()) return std::nullopt;
    return it->second;
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
    auto changed = run_tests_unlocked(tag);
    if (changed && on_change_) {
        on_change_(tag, *changed);
    }
}

std::optional<std::string> UrltestManager::run_tests_unlocked(const std::string& tag) {
    struct TestCandidate {
        std::string child_tag;
        std::string url;
        uint32_t fwmark;
        uint32_t timeout_ms;
        RetryConfig retry;
    };

    std::unique_lock<std::shared_mutex> lock(mutex_);

    auto it = states_.find(tag);
    if (it == states_.end()) return std::nullopt;

    auto& state = it->second;
    const auto& ut = state.config;
    const auto& cb_cfg = ut.circuit_breaker.value_or(CircuitBreakerConfig{});

    // Snapshot test parameters and mark each candidate as in-flight.
    std::vector<TestCandidate> candidates;
    for (const auto& group : ut.outbound_groups.value_or(std::vector<OutboundGroup>{})) {
        for (const auto& child_tag : group.outbounds) {
            auto mark_it = marks_.find(child_tag);
            if (mark_it == marks_.end()) continue;  // no fwmark (e.g. blackhole), skip
            auto& cb = state.circuit_breakers.at(child_tag);
            if (!cb.is_allowed(child_tag)) continue;  // circuit open, skip
            cb.begin_request(child_tag);
            candidates.push_back({child_tag, ut.url.value_or(""), mark_it->second,
                                   cb_cfg.timeout_ms.value_or(5000),
                                   ut.retry.value_or(RetryConfig{})});
        }
    }

    // Release the lock before blocking HTTP tests so concurrent reads and
    // other write operations are not stalled for the test duration.
    lock.unlock();

    std::map<std::string, URLTestResult> results;
    for (const auto& c : candidates) {
        results[c.child_tag] = tester_.test(c.url, c.fwmark, c.timeout_ms, c.retry);
    }

    // Re-acquire the lock to commit results and recompute the selection.
    lock.lock();

    auto it2 = states_.find(tag);
    if (it2 == states_.end()) return std::nullopt;  // cleared while tests were running

    auto& state2 = it2->second;
    for (const auto& [child_tag, result] : results) {
        auto& cb = state2.circuit_breakers.at(child_tag);
        cb.end_request(child_tag);
        if (result.success) cb.record_success(child_tag);
        else cb.record_failure(child_tag);
        state2.last_results[child_tag] = result;
    }

    const auto new_selected = select_outbound(tag);
    if (new_selected != state2.selected_outbound) {
        state2.selected_outbound = new_selected;
        return new_selected;
    }
    return std::nullopt;
}

std::string UrltestManager::select_outbound(const std::string& tag) {
    auto it = states_.find(tag);
    if (it == states_.end()) return "";

    const auto& state = it->second;
    const auto& ut = state.config;

    if (!ut.outbound_groups.has_value()) return "";
    const auto& groups = *ut.outbound_groups;

    struct GroupRef { size_t index; uint32_t weight; };
    std::vector<GroupRef> sorted_groups;
    sorted_groups.reserve(groups.size());
    for (size_t i = 0; i < groups.size(); ++i) {
        sorted_groups.push_back({i, static_cast<uint32_t>(groups[i].weight.value_or(1))});
    }
    std::sort(sorted_groups.begin(), sorted_groups.end(),
              [](const GroupRef& a, const GroupRef& b) { return a.weight < b.weight; });

    for (const auto& gref : sorted_groups) {
        const auto& group = groups[gref.index];

        uint32_t min_latency = std::numeric_limits<uint32_t>::max();
        for (const auto& child_tag : group.outbounds) {
            auto cb_it = state.circuit_breakers.find(child_tag);
            if (cb_it == state.circuit_breakers.end()) continue;
            if (cb_it->second.state(child_tag) == CircuitState::open) continue;
            auto res_it = state.last_results.find(child_tag);
            if (res_it == state.last_results.end() || !res_it->second.success) continue;
            if (res_it->second.latency_ms < min_latency) min_latency = res_it->second.latency_ms;
        }

        if (min_latency == std::numeric_limits<uint32_t>::max()) continue;

        const uint32_t tolerance = static_cast<uint32_t>(ut.tolerance_ms.value_or(100));

        // Prefer the current selection if still within tolerance (avoids churn).
        if (!state.selected_outbound.empty()) {
            auto it_inc = std::find(group.outbounds.begin(), group.outbounds.end(),
                                    state.selected_outbound);
            if (it_inc != group.outbounds.end()) {
                auto cb_it = state.circuit_breakers.find(state.selected_outbound);
                if (cb_it != state.circuit_breakers.end() &&
                    cb_it->second.state(state.selected_outbound) != CircuitState::open) {
                    auto res_it = state.last_results.find(state.selected_outbound);
                    if (res_it != state.last_results.end() && res_it->second.success &&
                        res_it->second.latency_ms <= min_latency + tolerance) {
                        return state.selected_outbound;
                    }
                }
            }
        }

        for (const auto& child_tag : group.outbounds) {
            auto cb_it = state.circuit_breakers.find(child_tag);
            if (cb_it == state.circuit_breakers.end()) continue;
            if (cb_it->second.state(child_tag) == CircuitState::open) continue;
            auto res_it = state.last_results.find(child_tag);
            if (res_it == state.last_results.end() || !res_it->second.success) continue;
            if (res_it->second.latency_ms <= min_latency + tolerance) return child_tag;
        }
    }

    return "";  // all exhausted — blackhole fallback
}

} // namespace keen_pbr3
