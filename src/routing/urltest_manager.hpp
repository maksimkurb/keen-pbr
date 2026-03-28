#pragma once

#include "../config/config.hpp"
#include "../health/circuit_breaker.hpp"
#include "../health/url_tester.hpp"

#include <functional>
#include <map>
#include <shared_mutex>
#include <string>

namespace keen_pbr3 {

class Scheduler;

// Per-urltest outbound state: test results, circuit breakers, selected child
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
class UrltestManager {
public:
    UrltestManager(URLTester& tester, const OutboundMarkMap& marks,
                   Scheduler& scheduler, UrltestChangeCallback on_change);
    ~UrltestManager();

    UrltestManager(const UrltestManager&) = delete;
    UrltestManager& operator=(const UrltestManager&) = delete;

    // Register a urltest outbound. Schedules periodic testing at interval_ms.
    void register_urltest(const Outbound& ut);

    // Run tests immediately for a specific urltest outbound (e.g., on SIGUSR1).
    void trigger_immediate_test(const std::string& urltest_tag);

    // Get the currently selected child outbound tag for a urltest.
    // Returns empty string if not registered or no outbound selected.
    std::string get_selected(const std::string& urltest_tag) const;

    // Get a snapshot of state for API/status reporting.
    // Throws std::out_of_range if tag not found.
    UrltestState get_state(const std::string& urltest_tag) const;

    // Unregister all urltest outbounds and cancel their scheduled tasks.
    void clear();

private:
    // Run URL tests for all child outbounds of the given urltest and update selection.
    // Acquires unique_lock on mutex_.
    void run_tests(const std::string& tag);

    // Internal implementation of run_tests; caller must already hold unique_lock on mutex_.
    void run_tests_locked(const std::string& tag);

    // Select the best outbound using weighted group algorithm with tolerance.
    // Returns empty string if all outbounds are circuit-broken (blackhole fallback).
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
