#include "urltest_manager.hpp"

#include "../config/icmptest_limits.hpp"
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
    std::string target;
    uint32_t count{0};
    uint32_t max_failed{0};
    uint32_t packet_interval_ms{0};
    uint32_t max_rtt_ms{0};
    bool is_icmp{false};
};

std::chrono::milliseconds normalize_interval(const Outbound& outbound) {
    auto interval = std::chrono::milliseconds(outbound.interval_ms.value_or(
        outbound.type == OutboundType::ICMPTEST
            ? icmptest_limits::default_interval_ms
            : 180000));
    if (interval.count() < 1) {
        interval = std::chrono::milliseconds(1);
    }
    return interval;
}

CircuitBreakerConfig effective_breaker_config(const Outbound& outbound) {
    auto config = outbound.circuit_breaker.value_or(CircuitBreakerConfig{});
    if (outbound.type == OutboundType::ICMPTEST && !config.timeout_ms.has_value()) {
        config.timeout_ms = std::max(
            icmptest_limits::default_breaker_timeout_ms,
            outbound.interval_ms.value_or(icmptest_limits::default_interval_ms));
    }
    return config;
}

template <typename Fn>
class ScopeExit {
public:
    explicit ScopeExit(Fn fn) : fn_(std::move(fn)) {}
    ~ScopeExit() { if (active_) fn_(); }
    ScopeExit(const ScopeExit&) = delete;
    ScopeExit& operator=(const ScopeExit&) = delete;
    void dismiss() { active_ = false; }
private:
    Fn fn_;
    bool active_{true};
};

template <typename Fn>
ScopeExit<Fn> make_scope_exit(Fn fn) {
    return ScopeExit<Fn>(std::move(fn));
}

} // namespace

UrltestManager::UrltestManager(URLTester& tester, IcmpTester& icmp_tester,
                               const OutboundMarkMap& marks,
                               Scheduler& scheduler,
                               BlockingExecutor& blocking_executor,
                               UrltestChangeCallback on_change,
                               UrltestCommitCallback on_commit)
    : tester_(tester)
    , icmp_tester_(icmp_tester)
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
            for (const auto& child_tag : outbound_group_tags(group)) {
                state.circuit_breakers.emplace(
                    child_tag,
                    CircuitBreaker(effective_breaker_config(ut)));
            }
        }

        const std::string tag = ut.tag;
        state.scheduler_task_id = scheduler_.schedule_repeating(
            normalize_interval(ut),
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

bool UrltestManager::is_probe_current(const std::string& tag,
                                      std::uint64_t generation) const {
    const auto it = states_.find(tag);
    return it != states_.end() && it->second.generation == generation;
}

void UrltestManager::abandon_probe(const std::string& tag, std::uint64_t generation,
                                   const std::vector<std::string>& child_tags) {
    KPBR_SHARED_UNIQUE_LOCK(lock, mutex_);
    auto it = states_.find(tag);
    if (it == states_.end() || it->second.generation != generation) return;
    it->second.probe_inflight = false;
    for (const auto& child_tag : child_tags) {
        auto breaker = it->second.circuit_breakers.find(child_tag);
        if (breaker != it->second.circuit_breakers.end()) {
            breaker->second.end_request(child_tag);
        }
    }
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

        for (const auto& group : state.config.outbound_groups.value_or(std::vector<OutboundGroup>{})) {
            for (const auto& child_tag : outbound_group_tags(group)) {
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
                    .timeout_ms = static_cast<uint32_t>(
                        state.config.probe_timeout_ms.value_or(
                            state.config.type == OutboundType::ICMPTEST
                                ? icmptest_limits::default_timeout_ms
                                : kDefaultUrltestProbeTimeoutMs)),
                    .retry = state.config.retry.value_or(RetryConfig{}),
                    .target = outbound_group_target(group, child_tag),
                    .count = static_cast<uint32_t>(state.config.count.value_or(3)),
                    .max_failed = static_cast<uint32_t>(state.config.max_failed.value_or(0)),
                    .packet_interval_ms = static_cast<uint32_t>(state.config.packet_interval_ms.value_or(200)),
                    .max_rtt_ms = static_cast<uint32_t>(state.config.max_rtt_ms.value_or(500)),
                    .is_icmp = state.config.type == OutboundType::ICMPTEST,
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

    std::vector<std::string> candidate_tags;
    candidate_tags.reserve(candidates.size());
    for (const auto& candidate : candidates) candidate_tags.push_back(candidate.child_tag);

    const bool enqueued = blocking_executor_.try_post(
        "urltest:" + tag,
        [this,
         tag,
         probe_generation,
         reason,
         candidates_for_probe = candidates,
         candidate_tags_for_probe = candidate_tags,
         trace_id]() mutable {
            ScopedTraceContext trace_scope(trace_id);
            auto completion_guard = make_scope_exit([this, &tag, probe_generation,
                                                     &candidate_tags_for_probe] {
                abandon_probe(tag, probe_generation, candidate_tags_for_probe);
            });
            std::map<std::string, URLTestResult> results;

            for (const auto& candidate : candidates_for_probe) {
                {
                    KPBR_SHARED_LOCK(lock, mutex_);
                    if (!is_probe_current(tag, probe_generation)) {
                        Logger::instance().trace(
                            "urltest_probe_abort",
                            "tag={} generation={} child={} trigger={} reason=stale_probe",
                            tag,
                            probe_generation,
                            candidate.child_tag,
                            reason);
                        return;
                    }
                }

                const auto started_at = std::chrono::steady_clock::now();
                Logger::instance().trace("urltest_candidate_start",
                                         "tag={} generation={} child={} fwmark={} trigger={}",
                                         tag,
                                         probe_generation,
                                         candidate.child_tag,
                                         candidate.fwmark,
                                         reason);

                URLTestResult result;
                try {
                    result = candidate.is_icmp
                        ? icmp_tester_.test(candidate.target, candidate.fwmark, candidate.count,
                                            candidate.max_failed, candidate.packet_interval_ms,
                                            candidate.timeout_ms, candidate.max_rtt_ms)
                        : tester_.test(candidate.url, candidate.fwmark, candidate.timeout_ms,
                                       candidate.retry);
                } catch (const std::exception& error) {
                    result.error = std::string("probe exception: ") + error.what();
                } catch (...) {
                    result.error = "probe exception: unknown error";
                }

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
                if (!result.error.empty()) {
                    Logger::instance().warn(
                        "urltest probe failed: tag={} child={} error={}",
                        tag, candidate.child_tag, result.error);
                }

                results.emplace(candidate.child_tag, std::move(result));
            }

            bool accepted = false;
            try {
                accepted = on_commit_ &&
                    on_commit_(tag, probe_generation, std::move(results), trace_id);
            } catch (...) {
                accepted = false;
            }
            if (!accepted) {
                return;
            }
            completion_guard.dismiss();
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
        const auto group_tags = outbound_group_tags(group);
        uint32_t min_latency = std::numeric_limits<uint32_t>::max();

        for (const auto& child_tag : group_tags) {
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

        const uint32_t tolerance = static_cast<uint32_t>(ut.tolerance_ms.value_or(
            ut.type == OutboundType::ICMPTEST
                ? icmptest_limits::default_tolerance_ms
                : 100));

        if (!state.selected_outbound.empty()) {
            const auto existing_it = std::find(group_tags.begin(),
                                               group_tags.end(),
                                               state.selected_outbound);
            if (existing_it != group_tags.end()) {
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

        for (const auto& child_tag : group_tags) {
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
