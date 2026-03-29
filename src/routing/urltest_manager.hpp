#pragma once

#include "../config/config.hpp"
#include "../health/circuit_breaker.hpp"
#include "../health/url_tester.hpp"

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>

namespace keen_pbr3 {

class Scheduler;

// Per-urltest outbound state: test results, circuit breakers, selected child.
struct UrltestState {
    Outbound config;
    std::map<std::string, URLTestResult> last_results;
    std::map<std::string, CircuitBreaker> circuit_breakers;
    std::string selected_outbound;
    int scheduler_task_id{-1};
};

// Callback invoked when the selected outbound changes for a urltest.
// Parameters: (urltest_tag, new_child_outbound_tag)
using UrltestChangeCallback = std::function<void(const std::string&, const std::string&)>;

// Manages periodic URL testing for urltest outbounds, tracks per-child-outbound
// latencies and circuit breaker states, and selects the best outbound using the
// weighted group algorithm.
//
// Thread-safety model:
//   - All public methods are thread-safe.
//   - Mutation methods (register_urltest, trigger_immediate_test, clear) are
//     intended to be called on the event-loop thread, but are safe if called
//     concurrently because all mutable state is protected by mutex_.
//   - mutex_ is never held during blocking I/O — only for brief snapshots and
//     result commits. This prevents long stalls for any concurrent reader.
//   - Read methods (get_selected, get_state) read from a separately published
//     snapshot map (snapshots_mutex_) and never contend with test execution.
//
// Lock ordering (always acquire in this order, never reverse):
//   Daemon::state_mutex_ → UrltestManager::mutex_ → snapshots_mutex_
//
// on_change_ is always invoked with no UrltestManager lock held. The caller
// must post the callback work to the event loop rather than executing it
// synchronously — this removes any lock-ordering dependency between
// UrltestManager and Daemon::state_mutex_.
class UrltestManager {
public:
    UrltestManager(URLTester& tester, const OutboundMarkMap& marks,
                   Scheduler& scheduler, UrltestChangeCallback on_change);
    ~UrltestManager();

    UrltestManager(const UrltestManager&) = delete;
    UrltestManager& operator=(const UrltestManager&) = delete;

    // Register a urltest outbound and run the initial URL test synchronously.
    // Schedules periodic testing at interval_ms for subsequent runs.
    // Returns the initially selected child outbound (empty string if none selected).
    // on_change_ is NOT called for the initial selection — the caller is responsible
    // for applying it. Subsequent selection changes will invoke on_change_.
    std::string register_urltest(const Outbound& ut);

    // Run tests immediately for a specific urltest outbound (e.g., on SIGUSR1).
    void trigger_immediate_test(const std::string& urltest_tag);

    // Get the currently selected child outbound tag for a urltest.
    // Thread-safe: reads from the published snapshot; never blocks on test I/O.
    std::string get_selected(const std::string& urltest_tag) const;

    // Get a snapshot of state for API/status reporting.
    // Thread-safe: reads from the published snapshot; never blocks on test I/O.
    // Returns std::nullopt if the tag is not registered.
    std::optional<UrltestState> get_state(const std::string& urltest_tag) const;

    // Unregister all urltest outbounds and cancel their scheduled tasks.
    void clear();

private:
    // Run URL tests and notify via on_change_ if the selection changed.
    // Must NOT be called while holding mutex_.
    void run_tests(const std::string& tag);

    // Three-phase test execution:
    //   Phase 1 (brief mutex_ lock)  — snapshot candidates, call begin_request
    //   Phase 2 (no lock)            — blocking HTTP tests
    //   Phase 3 (brief mutex_ lock)  — commit results, recompute selection, publish snapshot
    // Returns the new selection if it changed, std::nullopt otherwise.
    // Must NOT be called while holding mutex_.
    std::optional<std::string> run_tests_unlocked(const std::string& tag);

    // Select the best outbound using weighted group / tolerance algorithm.
    // Caller must hold at least a shared_lock on mutex_.
    std::string select_outbound(const std::string& tag);

    // Copy states_[tag] into the published snapshot map.
    // Caller must hold at least a shared_lock on mutex_.
    // Takes snapshots_mutex_ internally (mutex_ → snapshots_mutex_ ordering).
    void publish_snapshot_locked(const std::string& tag);

    URLTester& tester_;
    const OutboundMarkMap& marks_;
    Scheduler& scheduler_;
    UrltestChangeCallback on_change_;

    // Mutable state: all reads and writes go through mutex_.
    // mutex_ is held only for brief, non-blocking critical sections.
    mutable std::shared_mutex mutex_;
    std::map<std::string, UrltestState> states_;

    // Published snapshots for concurrent reads from API / status threads.
    // Updated by the event-loop thread after every test run or state change.
    // snapshots_mutex_ is a leaf lock — nothing else is acquired while holding it.
    mutable std::shared_mutex snapshots_mutex_;
    std::map<std::string, std::shared_ptr<UrltestState>> snapshots_;
};

} // namespace keen_pbr3
