#include "urltest_manager.hpp"

#include "../daemon/scheduler.hpp"
#include "../log/logger.hpp"

#include <algorithm>
#include <chrono>
#include <limits>
#include <utility>
#include <vector>

namespace keen_pbr3 {

namespace {

struct TestCandidate {
    std::string child_tag;
    std::string url;
    uint32_t fwmark{0};
    uint32_t timeout_ms{0};
    RetryConfig retry;
};

std::chrono::seconds normalize_interval_seconds(const Outbound& outbound) {
    auto interval = std::chrono::seconds(outbound.interval_ms.value_or(180000) / 1000);
    if (interval.count() < 1) {
        interval = std::chrono::seconds(1);
    }
    return interval;
}

} // namespace

UrltestManager::UrltestManager(URLTester& tester,
                               const OutboundMarkMap& marks,
                               Scheduler& scheduler,
                               BlockingExecutor& blocking_executor,
                               UrltestChangeCallback on_change,
                               UrltestCommitCallback on_commit)
    : tester_(tester)
    , marks_(marks)
    , scheduler_(scheduler)
    , blocking_executor_(blocking_executor)
    , on_change_(std::move(on_change))
    , on_commit_(std::move(on_commit)) {}

UrltestManager::~UrltestManager() {
    try {
        clear();
    } catch (const std::exception& e) {
        Logger::instance().error("UrltestManager cleanup failed during destruction: {}",
                                 e.what());
    } catch (...) {
        Logger::instance().error(
            "UrltestManager cleanup failed during destruction: unknown error");
    }
}

void UrltestManager::register_urltest(const Outbound& ut) {
    {
        KPBR_SHARED_UNIQUE_LOCK(lock, mutex_);

        UrltestState state;
        state.config = ut;

        for (const auto& group : ut.outbound_groups.value_or(std::vector<OutboundGroup>{})) {
            for (const auto& child_tag : group.outbounds) {
                state.circuit_breakers.emplace(
                    child_tag,
                    CircuitBreaker(ut.circuit_breaker.value_or(CircuitBreakerConfig{})));
            }
        }

        const std::string tag = ut.tag;
        state.scheduler_task_id = scheduler_.schedule_repeating(
            normalize_interval_seconds(ut),
            [this, tag]() {
                run_tests(tag);
            },
            "urltest:" + tag);

        states_.emplace(ut.tag, std::move(state));
    }

    Logger::instance().trace("urltest_register", "tag={}", ut.tag);
    queue_probe_unlocked(ut.tag, "initial");
}

void UrltestManager::trigger_immediate_test(const std::string& urltest_tag) {
    queue_probe_unlocked(urltest_tag, "manual");
}

bool UrltestManager::commit_probe_results(const std::string& urltest_tag,
                                          std::uint64_t generation,
                                          std::map<std::string, URLTestResult> results) {
    std::string new_selected;
    bool selection_changed = false;

    {
        KPBR_SHARED_UNIQUE_LOCK(lock, mutex_);
        auto it = states_.find(urltest_tag);
        if (it == states_.end()) {
            Logger::instance().trace("urltest_commit_skip",
                                     "tag={} generation={} reason=missing_state",
                                     urltest_tag,
                                     generation);
            return false;
        }

        auto& state = it->second;
        if (generation != state.generation) {
            Logger::instance().trace("urltest_commit_skip",
                                     "tag={} generation={} current_generation={} reason=stale",
                                     urltest_tag,
                                     generation,
                                     state.generation);
            return false;
        }

        state.probe_inflight = false;

        for (const auto& [child_tag, result] : results) {
            auto cb_it = state.circuit_breakers.find(child_tag);
            if (cb_it == state.circuit_breakers.end()) {
                continue;
            }

            cb_it->second.end_request(child_tag);
            if (result.success) {
                cb_it->second.record_success(child_tag);
            } else {
                cb_it->second.record_failure(child_tag);
            }
            state.last_results[child_tag] = result;
        }

        const std::string previous_selected = state.selected_outbound;
        new_selected = select_outbound(urltest_tag);
        if (new_selected != previous_selected) {
            state.selected_outbound = new_selected;
            selection_changed = true;
        }
    }

    Logger::instance().trace("urltest_commit",
                             "tag={} generation={} changed={} selected={}",
                             urltest_tag,
                             generation,
                             selection_changed ? "true" : "false",
                             new_selected);

    if (selection_changed && on_change_) {
        on_change_(urltest_tag, new_selected);
    }

    return selection_changed;
}

std::string UrltestManager::get_selected(const std::string& urltest_tag) const {
    KPBR_SHARED_LOCK(lock, mutex_);
    const auto it = states_.find(urltest_tag);
    if (it == states_.end()) {
        return "";
    }
    return it->second.selected_outbound;
}

std::optional<UrltestState> UrltestManager::get_state(const std::string& urltest_tag) const {
    KPBR_SHARED_LOCK(lock, mutex_);
    const auto it = states_.find(urltest_tag);
    if (it == states_.end()) {
        return std::nullopt;
    }
    return it->second;
}

void UrltestManager::clear() {
    KPBR_SHARED_UNIQUE_LOCK(lock, mutex_);
    for (auto& [tag, state] : states_) {
        (void)tag;
        if (state.scheduler_task_id >= 0) {
            scheduler_.cancel(state.scheduler_task_id);
        }
    }
    states_.clear();
    ++generation_;
}

void UrltestManager::run_tests(const std::string& tag) {
    queue_probe_unlocked(tag, "scheduled");
}

bool UrltestManager::queue_probe_unlocked(const std::string& tag,
                                          const std::string& reason) {
    std::vector<TestCandidate> candidates;
    std::uint64_t probe_generation = 0;
    const TraceId trace_id = ensure_trace_id();

    {
        KPBR_SHARED_UNIQUE_LOCK(lock, mutex_);
        auto it = states_.find(tag);
        if (it == states_.end()) {
            Logger::instance().trace("urltest_probe_skip",
                                     "tag={} reason=missing_state trigger={}",
                                     tag,
                                     reason);
            return false;
        }

        auto& state = it->second;
        if (state.probe_inflight) {
            Logger::instance().trace("urltest_probe_skip",
                                     "tag={} reason=inflight trigger={}",
                                     tag,
                                     reason);
            return false;
        }

        state.probe_inflight = true;
        state.generation = generation_++;
        probe_generation = state.generation;

        const auto& cb_cfg = state.config.circuit_breaker.value_or(CircuitBreakerConfig{});
        for (const auto& group : state.config.outbound_groups.value_or(std::vector<OutboundGroup>{})) {
            for (const auto& child_tag : group.outbounds) {
                const auto mark_it = marks_.find(child_tag);
                if (mark_it == marks_.end()) {
                    continue;
                }

                auto cb_it = state.circuit_breakers.find(child_tag);
                if (cb_it == state.circuit_breakers.end()) {
                    continue;
                }

                if (!cb_it->second.is_allowed(child_tag)) {
                    continue;
                }

                cb_it->second.begin_request(child_tag);
                candidates.push_back(TestCandidate{
                    .child_tag = child_tag,
                    .url = state.config.url.value_or(""),
                    .fwmark = mark_it->second,
                    .timeout_ms = static_cast<uint32_t>(cb_cfg.timeout_ms.value_or(5000)),
                    .retry = state.config.retry.value_or(RetryConfig{}),
                });
            }
        }
    }

    Logger::instance().trace("urltest_probe_queued",
                             "tag={} generation={} trigger={} candidates={}",
                             tag,
                             probe_generation,
                             reason,
                             candidates.size());

    const bool enqueued = blocking_executor_.try_post(
        "urltest:" + tag,
        [this,
         tag,
         probe_generation,
         reason,
         candidates_for_probe = candidates,
         trace_id]() mutable {
            ScopedTraceContext trace_scope(trace_id);
            std::map<std::string, URLTestResult> results;
            results.clear();

            for (const auto& candidate : candidates_for_probe) {
                const auto started_at = std::chrono::steady_clock::now();
                Logger::instance().trace("urltest_candidate_start",
                                         "tag={} generation={} child={} fwmark={} trigger={}",
                                         tag,
                                         probe_generation,
                                         candidate.child_tag,
                                         candidate.fwmark,
                                         reason);

                auto result = tester_.test(candidate.url,
                                           candidate.fwmark,
                                           candidate.timeout_ms,
                                           candidate.retry);

                const auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - started_at).count();
                Logger::instance().trace("urltest_candidate_end",
                                         "tag={} generation={} child={} success={} latency_ms={} duration_ms={} error={}",
                                         tag,
                                         probe_generation,
                                         candidate.child_tag,
                                         result.success ? "true" : "false",
                                         result.latency_ms,
                                         duration_ms,
                                         result.error.empty() ? std::string("-") : result.error);

                results.emplace(candidate.child_tag, std::move(result));
            }

            if (on_commit_) {
                on_commit_(tag, probe_generation, std::move(results), trace_id);
            }
        },
        trace_id);

    if (enqueued) {
        return true;
    }

    KPBR_SHARED_UNIQUE_LOCK(lock, mutex_);
    auto it = states_.find(tag);
    if (it != states_.end() && it->second.generation == probe_generation) {
        it->second.probe_inflight = false;
        for (const auto& candidate : candidates) {
            auto cb_it = it->second.circuit_breakers.find(candidate.child_tag);
            if (cb_it != it->second.circuit_breakers.end()) {
                cb_it->second.end_request(candidate.child_tag);
            }
        }
    }

    Logger::instance().trace("urltest_probe_skip",
                             "tag={} generation={} trigger={} reason=executor_unavailable",
                             tag,
                             probe_generation,
                             reason);
    return false;
}

std::string UrltestManager::select_outbound(const std::string& tag) {
    const auto it = states_.find(tag);
    if (it == states_.end()) {
        return "";
    }

    const auto& state = it->second;
    const auto& ut = state.config;
    if (!ut.outbound_groups.has_value()) {
        return "";
    }

    struct GroupRef {
        size_t index;
        uint32_t weight;
    };

    const auto& groups = *ut.outbound_groups;
    std::vector<GroupRef> sorted_groups;
    sorted_groups.reserve(groups.size());
    for (size_t i = 0; i < groups.size(); ++i) {
        sorted_groups.push_back(GroupRef{
            .index = i,
            .weight = static_cast<uint32_t>(groups[i].weight.value_or(1)),
        });
    }
    std::sort(sorted_groups.begin(),
              sorted_groups.end(),
              [](const GroupRef& lhs, const GroupRef& rhs) {
                  return lhs.weight < rhs.weight;
              });

    for (const auto& group_ref : sorted_groups) {
        const auto& group = groups[group_ref.index];
        uint32_t min_latency = std::numeric_limits<uint32_t>::max();

        for (const auto& child_tag : group.outbounds) {
            const auto cb_it = state.circuit_breakers.find(child_tag);
            if (cb_it == state.circuit_breakers.end()) {
                continue;
            }
            if (cb_it->second.state(child_tag) == CircuitState::open) {
                continue;
            }

            const auto result_it = state.last_results.find(child_tag);
            if (result_it == state.last_results.end() || !result_it->second.success) {
                continue;
            }
            min_latency = std::min(min_latency, result_it->second.latency_ms);
        }

        if (min_latency == std::numeric_limits<uint32_t>::max()) {
            continue;
        }

        const uint32_t tolerance = static_cast<uint32_t>(ut.tolerance_ms.value_or(100));

        if (!state.selected_outbound.empty()) {
            const auto existing_it = std::find(group.outbounds.begin(),
                                               group.outbounds.end(),
                                               state.selected_outbound);
            if (existing_it != group.outbounds.end()) {
                const auto cb_it = state.circuit_breakers.find(state.selected_outbound);
                if (cb_it != state.circuit_breakers.end() &&
                    cb_it->second.state(state.selected_outbound) != CircuitState::open) {
                    const auto result_it = state.last_results.find(state.selected_outbound);
                    if (result_it != state.last_results.end() &&
                        result_it->second.success &&
                        result_it->second.latency_ms <= min_latency + tolerance) {
                        return state.selected_outbound;
                    }
                }
            }
        }

        for (const auto& child_tag : group.outbounds) {
            const auto cb_it = state.circuit_breakers.find(child_tag);
            if (cb_it == state.circuit_breakers.end()) {
                continue;
            }
            if (cb_it->second.state(child_tag) == CircuitState::open) {
                continue;
            }

            const auto result_it = state.last_results.find(child_tag);
            if (result_it == state.last_results.end() || !result_it->second.success) {
                continue;
            }
            if (result_it->second.latency_ms <= min_latency + tolerance) {
                return child_tag;
            }
        }
    }

    return "";
}

} // namespace keen_pbr3
