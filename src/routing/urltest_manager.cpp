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

std::string UrltestManager::register_urltest(const Outbound& ut) {
    {
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

        // Publish the initial (pre-test) snapshot so API reads see the registered
        // config immediately, before the first test run completes.
        publish_snapshot_locked(ut.tag);
    }  // release mutex_ before blocking I/O

    // Run the initial test without holding any lock and return the result.
    // on_change_ is intentionally NOT called here: the caller (apply_config)
    // may hold an outer lock (Daemon::state_mutex_), and on_change_ must only
    // be invoked with no locks held. The caller applies the initial selection
    // directly to firewall_state_ under state_mutex_.
    auto initial_selection = run_tests_unlocked(ut.tag);
    return initial_selection.value_or("");
}

void UrltestManager::trigger_immediate_test(const std::string& urltest_tag) {
    auto changed_selection = run_tests_unlocked(urltest_tag);
    if (changed_selection.has_value() && on_change_) {
        on_change_(urltest_tag, *changed_selection);
    }
}

std::string UrltestManager::get_selected(const std::string& urltest_tag) const {
    std::shared_lock<std::shared_mutex> lock(snapshots_mutex_);
    auto it = snapshots_.find(urltest_tag);
    if (it == snapshots_.end()) return "";
    return it->second->selected_outbound;
}

std::optional<UrltestState> UrltestManager::get_state(const std::string& urltest_tag) const {
    std::shared_lock<std::shared_mutex> lock(snapshots_mutex_);
    auto it = snapshots_.find(urltest_tag);
    if (it == snapshots_.end()) return std::nullopt;
    return *(it->second);
}

void UrltestManager::clear() {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    for (auto& [tag, state] : states_) {
        if (state.scheduler_task_id >= 0) {
            scheduler_.cancel(state.scheduler_task_id);
        }
    }
    states_.clear();

    // Clear published snapshots under the nested lock (mutex_ → snapshots_mutex_).
    std::unique_lock<std::shared_mutex> snap_lock(snapshots_mutex_);
    snapshots_.clear();
}

void UrltestManager::run_tests(const std::string& tag) {
    auto changed_selection = run_tests_unlocked(tag);
    if (changed_selection.has_value() && on_change_) {
        on_change_(tag, *changed_selection);
    }
}

std::optional<std::string> UrltestManager::run_tests_unlocked(const std::string& tag) {
    // Parameters needed to run a single child outbound test.
    struct TestCandidate {
        std::string child_tag;
        std::string url;
        uint32_t fwmark;
        uint32_t timeout_ms;
        RetryConfig retry;
    };

    // Phase 1: snapshot test parameters and mark each candidate as in-flight.
    // Hold mutex_ only for this brief, non-blocking section.
    std::vector<TestCandidate> candidates;
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        auto it = states_.find(tag);
        if (it == states_.end()) return std::nullopt;

        auto& state = it->second;
        const auto& ut = state.config;
        const auto& cb_cfg = ut.circuit_breaker.value_or(CircuitBreakerConfig{});

        for (const auto& group : ut.outbound_groups.value_or(std::vector<OutboundGroup>{})) {
            for (const auto& child_tag : group.outbounds) {
                auto mark_it = marks_.find(child_tag);
                if (mark_it == marks_.end()) continue;  // no fwmark (e.g. blackhole), skip

                auto& cb = state.circuit_breakers.at(child_tag);
                if (!cb.is_allowed(child_tag)) continue;  // circuit open, skip

                cb.begin_request(child_tag);
                candidates.push_back({
                    child_tag,
                    ut.url.value_or(""),
                    mark_it->second,
                    cb_cfg.timeout_ms.value_or(5000),
                    ut.retry.value_or(RetryConfig{})
                });
            }
        }
    }  // mutex_ released — blocking I/O runs below without holding any lock

    // Phase 2: run the blocking HTTP tests with no lock held.
    // Concurrent API reads (get_selected, get_state) see the last published
    // snapshot and are never blocked by this section.
    std::map<std::string, URLTestResult> results;
    for (const auto& c : candidates) {
        results[c.child_tag] = tester_.test(c.url, c.fwmark, c.timeout_ms, c.retry);
    }

    // Phase 3: commit results, update circuit breakers, recompute selection,
    // and publish a new snapshot — all under a brief mutex_ lock.
    {
        std::unique_lock<std::shared_mutex> lock(mutex_);
        auto it = states_.find(tag);
        if (it == states_.end()) {
            // Tag was cleared while tests were running (e.g. apply_config tore down
            // this UrltestManager). Results are stale — discard them.
            return std::nullopt;
        }

        auto& state = it->second;
        for (const auto& [child_tag, result] : results) {
            auto& cb = state.circuit_breakers.at(child_tag);
            cb.end_request(child_tag);
            if (result.success) cb.record_success(child_tag);
            else cb.record_failure(child_tag);
            state.last_results[child_tag] = result;
        }

        auto new_selected = select_outbound(tag);
        bool selection_changed = (new_selected != state.selected_outbound);
        if (selection_changed) {
            state.selected_outbound = new_selected;
        }

        // Always publish the updated snapshot (captures new results + CB states),
        // not just when the selection changes.
        publish_snapshot_locked(tag);

        if (selection_changed) return new_selected;
        return std::nullopt;
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

void UrltestManager::publish_snapshot_locked(const std::string& tag) {
    // Caller must hold at least a shared_lock on mutex_ to ensure a consistent
    // read of states_[tag]. snapshots_mutex_ is taken here as a nested leaf lock.
    auto it = states_.find(tag);
    if (it == states_.end()) return;

    auto snapshot = std::make_shared<UrltestState>(it->second);

    std::unique_lock<std::shared_mutex> snap_lock(snapshots_mutex_);
    snapshots_[tag] = std::move(snapshot);
}

} // namespace keen_pbr3
