#pragma once

#include "../config/config.hpp"
#include "../health/circuit_breaker.hpp"
#include "../health/url_tester.hpp"

#include <functional>
#include <map>
#include <string>

namespace keen_pbr3 {

class Scheduler;

// Per-urltest outbound state: test results, circuit breakers, selected child
struct UrltestState {
    UrltestOutbound config;
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
    void register_urltest(const UrltestOutbound& ut);

    // Run tests immediately for a specific urltest outbound (e.g., on SIGUSR1).
    void trigger_immediate_test(const std::string& urltest_tag);

    // Get the currently selected child outbound tag for a urltest.
    // Returns empty string if not registered or no outbound selected.
    std::string get_selected(const std::string& urltest_tag) const;

    // Get const reference to state for API/status reporting.
    // Throws std::out_of_range if tag not found.
    const UrltestState& get_state(const std::string& urltest_tag) const;

    // Unregister all urltest outbounds and cancel their scheduled tasks.
    void clear();

private:
    // Run URL tests for all child outbounds of the given urltest and update selection.
    void run_tests(const std::string& tag);

    // Select the best outbound using weighted group algorithm with tolerance.
    // Returns empty string if all outbounds are circuit-broken (blackhole fallback).
    std::string select_outbound(const std::string& tag);

    URLTester& tester_;
    const OutboundMarkMap& marks_;
    Scheduler& scheduler_;
    UrltestChangeCallback on_change_;
    std::map<std::string, UrltestState> states_;
};

} // namespace keen_pbr3
