#pragma once

#include "../config/config.hpp"
#include "../health/circuit_breaker.hpp"
#include "../health/url_tester.hpp"
#include "../health/icmp_tester.hpp"
#include "../util/blocking_executor.hpp"
#include "../util/traced_mutex.hpp"

#include <functional>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace keen_pbr3 {

class Scheduler;

// Per-child probe telemetry, written under the manager lock when a sweep is
// committed (never on a packet path).  Latencies are microseconds; the
// optionals are empty until the first probe and after a failed probe.
struct ProbeMetrics {
    uint64_t attempts{0};
    uint64_t successes{0};
    uint64_t packets_sent{0};      // ICMP only
    uint64_t packets_received{0};  // ICMP only
    std::optional<bool> last_up;                 // result of the last probe
    std::optional<int64_t> last_success_unix_s;  // wall clock of the last success
    std::optional<uint64_t> latency_us;          // last probe, success only
    std::optional<uint64_t> latency_min_us;      // ICMP, last probe, success only
    std::optional<uint64_t> latency_max_us;      // ICMP, last probe, success only
};

// Per-urltest outbound state: test results, circuit breakers, selected child.
struct UrltestState {
    Outbound config;
    std::map<std::string, URLTestResult> last_results;
    std::map<std::string, ProbeMetrics> probe_metrics;
    std::map<std::string, CircuitBreaker> circuit_breakers;
    std::string selected_outbound;
    uint64_t selection_changes{0};
    int scheduler_task_id{-1};
    bool probe_inflight{false};
    std::uint64_t generation{0};
};

// Pure selection policy shared by URLTEST and ICMPTEST.
std::string select_test_group_outbound(const UrltestState& state);

// Usable children in the first (lowest weight) healthy group, in configured
// order. Balance mode distributes new connections across exactly this set.
std::vector<std::string> select_test_group_usable_outbounds(const UrltestState& state);

// Callback invoked after every accepted probe sweep. Emitting unchanged
// selections lets the daemon retry a previously failed routing transaction.
// Parameters: (urltest_tag, desired_child_outbound_tag)
// Guaranteed to be called without any UrltestManager lock held.
using UrltestChangeCallback = std::function<void(const std::string&, const std::string&)>;
using UrltestCommitCallback = std::function<bool(const std::string&,
                                                 std::uint64_t,
                                                 std::map<std::string, URLTestResult>,
                                                 TraceId)>;

// Manages periodic URL testing for urltest outbounds, tracks per-child-outbound
// latencies and circuit breaker states, and selects the best outbound using the
// weighted group algorithm.
//
// All public methods are thread-safe.
class UrltestManager {
public:
    UrltestManager(URLTester& tester, IcmpTester& icmp_tester, const OutboundMarkMap& marks,
                   Scheduler& scheduler,
                   BlockingExecutor& blocking_executor,
                   UrltestChangeCallback on_change,
                   UrltestCommitCallback on_commit);
    ~UrltestManager();

    UrltestManager(const UrltestManager&) = delete;
    UrltestManager& operator=(const UrltestManager&) = delete;

    // Register a test-group outbound, queue the initial probe, and schedule
    // periodic retests. Every accepted sweep publishes its desired selection.
    void register_urltest(const Outbound& ut);

    // Run tests immediately for a specific test-group outbound (e.g. on SIGUSR1).
    void trigger_immediate_test(const std::string& urltest_tag);
    bool commit_probe_results(const std::string& urltest_tag,
                              std::uint64_t generation,
                              std::map<std::string, URLTestResult> results);

    // Return the currently selected child outbound tag, or "" if none.
    std::string get_selected(const std::string& urltest_tag) const;

    // Return a state snapshot for API/status reporting.
    // Returns std::nullopt if the tag is not registered.
    std::optional<UrltestState> get_state(const std::string& urltest_tag) const;

    // Cancel all scheduled tasks and unregister all outbounds.
    void clear();

private:
    // Check whether an async probe still belongs to the currently registered
    // state for the given tag. Caller must hold at least a shared_lock.
    bool is_probe_current(const std::string& tag,
                          std::uint64_t generation) const REQUIRES_SHARED(mutex_);
    void abandon_probe(const std::string& tag, std::uint64_t generation,
                       const std::vector<std::string>& child_tags);

    // Run URL tests for all child outbounds of the given urltest and update
    // the internal selection. Returns the new selection if it changed.
    // Must NOT be called while holding mutex_.
    bool queue_probe_unlocked(const std::string& tag, const std::string& reason);

    // Periodic test entry point (called by the scheduler).
    void run_tests(const std::string& tag);

    // Select the best outbound using the weighted group / tolerance algorithm.
    // Caller must hold at least a shared_lock on mutex_.
    std::string select_outbound(const std::string& tag) REQUIRES_SHARED(mutex_);

    URLTester& tester_;
    IcmpTester& icmp_tester_;
    const OutboundMarkMap& marks_;
    Scheduler& scheduler_;
    BlockingExecutor& blocking_executor_;
    UrltestChangeCallback on_change_;
    UrltestCommitCallback on_commit_;

    mutable TracedSharedMutex mutex_;
    std::map<std::string, UrltestState> states_ GUARDED_BY(mutex_);
    std::uint64_t generation_ GUARDED_BY(mutex_){1};
};

} // namespace keen_pbr3
