#include "circuit_breaker.hpp"

namespace keen_pbr3 {

CircuitBreaker::CircuitBreaker(int failure_threshold, std::chrono::seconds cooldown)
    : failure_threshold_(failure_threshold), cooldown_(cooldown) {}

void CircuitBreaker::record_success(const std::string& tag) {
    auto& entry = get_or_create(tag);
    // Success in half-open means the outbound has recovered
    // Success in closed is normal operation
    entry.state = CircuitState::closed;
    entry.failure_count = 0;
}

void CircuitBreaker::record_failure(const std::string& tag) {
    auto& entry = get_or_create(tag);
    auto now = std::chrono::steady_clock::now();

    entry.failure_count++;
    entry.last_failure_time = now;

    switch (entry.state) {
    case CircuitState::closed:
        if (entry.failure_count >= failure_threshold_) {
            entry.state = CircuitState::open;
            entry.opened_at = now;
        }
        break;
    case CircuitState::half_open:
        // Probe failed - reopen the circuit
        entry.state = CircuitState::open;
        entry.opened_at = now;
        break;
    case CircuitState::open:
        // Already open, just update timestamps
        break;
    }
}

bool CircuitBreaker::is_allowed(const std::string& tag) {
    auto& entry = get_or_create(tag);

    switch (entry.state) {
    case CircuitState::closed:
        return true;
    case CircuitState::open: {
        auto now = std::chrono::steady_clock::now();
        if (now - entry.opened_at >= cooldown_) {
            // Cooldown expired - transition to half-open, allow one probe
            entry.state = CircuitState::half_open;
            return true;
        }
        return false;
    }
    case CircuitState::half_open:
        return true;
    }

    return false; // unreachable, but satisfies compiler
}

CircuitState CircuitBreaker::state(const std::string& tag) const {
    auto it = entries_.find(tag);
    if (it == entries_.end()) {
        return CircuitState::closed;
    }
    return it->second.state;
}

int CircuitBreaker::failure_count(const std::string& tag) const {
    auto it = entries_.find(tag);
    if (it == entries_.end()) {
        return 0;
    }
    return it->second.failure_count;
}

void CircuitBreaker::reset(const std::string& tag) {
    entries_.erase(tag);
}

void CircuitBreaker::reset_all() {
    entries_.clear();
}

CircuitBreakerEntry& CircuitBreaker::get_or_create(const std::string& tag) {
    return entries_[tag];
}

} // namespace keen_pbr3
