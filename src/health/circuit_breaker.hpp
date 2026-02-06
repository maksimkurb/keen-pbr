#pragma once

#include <chrono>
#include <map>
#include <string>

namespace keen_pbr3 {

// Circuit breaker states
enum class CircuitState {
    closed,    // Healthy - requests pass through
    open,      // Failed - requests blocked, waiting for cooldown
    half_open  // Testing recovery - allowing one probe request
};

// Per-outbound circuit breaker state
struct CircuitBreakerEntry {
    CircuitState state{CircuitState::closed};
    int failure_count{0};
    std::chrono::steady_clock::time_point last_failure_time{};
    std::chrono::steady_clock::time_point opened_at{};
};

// Prevents rapid failover oscillation between interfaces by tracking
// failure counts and enforcing cooldown periods before recovery attempts.
class CircuitBreaker {
public:
    // failure_threshold: number of consecutive failures before opening circuit
    // cooldown: time to wait in open state before transitioning to half-open
    explicit CircuitBreaker(int failure_threshold = 3,
                            std::chrono::seconds cooldown = std::chrono::seconds{60});

    // Record a successful health check for the given outbound tag.
    // Transitions: half_open -> closed, resets failure count.
    void record_success(const std::string& tag);

    // Record a failed health check for the given outbound tag.
    // Increments failure count; transitions closed -> open when threshold reached.
    // Transitions half_open -> open on failure.
    void record_failure(const std::string& tag);

    // Returns true if the outbound is allowed to receive traffic.
    // closed: allowed
    // open: blocked (unless cooldown expired, in which case transitions to half_open and allows)
    // half_open: allowed (one probe)
    bool is_allowed(const std::string& tag);

    // Get the current state for a tag (closed if not tracked).
    CircuitState state(const std::string& tag) const;

    // Get the failure count for a tag (0 if not tracked).
    int failure_count(const std::string& tag) const;

    // Reset the circuit breaker state for a specific tag.
    void reset(const std::string& tag);

    // Reset all circuit breaker states.
    void reset_all();

private:
    CircuitBreakerEntry& get_or_create(const std::string& tag);

    int failure_threshold_;
    std::chrono::seconds cooldown_;
    std::map<std::string, CircuitBreakerEntry> entries_;
};

} // namespace keen_pbr3
