#pragma once

#include "../config/config.hpp"
#include "../health/circuit_breaker.hpp"
#include "../health/url_tester.hpp"

#include <functional>
#include <map>
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
// Guaranteed to be called without any UrltestManager lock held.
using UrltestChangeCallback = std::function<void(const std::string&, const std::string&)>;

// Manages periodic URL testing for urltest outbounds, tracks per-child-outbound
// latencies and circuit breaker states, and selects the best outbound using the
// weighted group algorithm.
//
// All public methods are thread-safe.
class UrltestManager {
public:
    UrltestManager(URLTester& tester, const OutboundMarkMap& marks,
                   Scheduler& scheduler, UrltestChangeCallback on_change);
    ~UrltestManager();

    UrltestManager(const UrltestManager&) = delete;
    UrltestManager& operator=(const UrltestManager&) = delete;

    // Register a urltest outbound, run the initial URL test, and schedule
    // periodic retests. on_change_ is NOT called for the initial selection —
    // call get_selected() after this returns to read the initial value.
    void register_urltest(const Outbound& ut);

    // Run tests immediately for a specific urltest outbound (e.g. on SIGUSR1).
    // Invokes on_change_ if the selection changes.
    void trigger_immediate_test(const std::string& urltest_tag);

    // Return the currently selected child outbound tag, or "" if none.
    std::string get_selected(const std::string& urltest_tag) const;

    // Return a state snapshot for API/status reporting.
    // Returns std::nullopt if the tag is not registered.
    std::optional<UrltestState> get_state(const std::string& urltest_tag) const;

    // Cancel all scheduled tasks and unregister all outbounds.
    void clear();

private:
    // Run URL tests for all child outbounds of the given urltest and update
    // the internal selection. Returns the new selection if it changed.
    // Must NOT be called while holding mutex_.
    std::optional<std::string> run_tests_unlocked(const std::string& tag);

    // Periodic test entry point (called by the scheduler).
    // Runs tests and invokes on_change_ if the selection changes.
    void run_tests(const std::string& tag);

    // Select the best outbound using the weighted group / tolerance algorithm.
    // Caller must hold at least a shared_lock on mutex_.
    std::string select_outbound(const std::string& tag);

    URLTester& tester_;
    const OutboundMarkMap& marks_;
    Scheduler& scheduler_;
    UrltestChangeCallback on_change_;

    mutable std::shared_mutex mutex_;
    std::map<std::string, UrltestState> states_;
};

} // namespace keen_pbr3
