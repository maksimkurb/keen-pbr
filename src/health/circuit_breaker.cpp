#include "circuit_breaker.hpp"

namespace keen_pbr3 {

CircuitBreaker::CircuitBreaker(const CircuitBreakerConfig& config)
    : config_(config) {}

void CircuitBreaker::record_success(const std::string& tag) {
    auto& entry = get_or_create(tag);

    switch (entry.state) {
    case CircuitState::half_open:
        entry.success_count_in_half_open++;
        if (entry.success_count_in_half_open >= config_.success_threshold) {
            entry.state = CircuitState::closed;
            entry.failure_count = 0;
            entry.success_count_in_half_open = 0;
            entry.half_open_active_requests = 0;
        }
        break;
    case CircuitState::closed:
        // Normal operation - reset failure count
        entry.failure_count = 0;
        break;
    case CircuitState::open:
        // Shouldn't happen, but ignore
        break;
    }
}

void CircuitBreaker::record_failure(const std::string& tag) {
    auto& entry = get_or_create(tag);
    auto now = std::chrono::steady_clock::now();

    entry.failure_count++;
    entry.last_failure_time = now;

    switch (entry.state) {
    case CircuitState::closed:
        if (entry.failure_count >= static_cast<int>(config_.failure_threshold)) {
            entry.state = CircuitState::open;
            entry.opened_at = now;
        }
        break;
    case CircuitState::half_open:
        // Probe failed - reopen the circuit
        entry.state = CircuitState::open;
        entry.opened_at = now;
        entry.success_count_in_half_open = 0;
        entry.half_open_active_requests = 0;
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
        auto cooldown = std::chrono::milliseconds(config_.timeout_ms);
        if (now - entry.opened_at >= cooldown) {
            // Cooldown expired - transition to half-open
            entry.state = CircuitState::half_open;
            entry.success_count_in_half_open = 0;
            entry.half_open_active_requests = 0;
            return true;
        }
        return false;
    }
    case CircuitState::half_open:
        return entry.half_open_active_requests < config_.half_open_max_requests;
    }

    return false; // unreachable, but satisfies compiler
}

void CircuitBreaker::begin_request(const std::string& tag) {
    auto& entry = get_or_create(tag);
    if (entry.state == CircuitState::half_open) {
        entry.half_open_active_requests++;
    }
}

void CircuitBreaker::end_request(const std::string& tag) {
    auto& entry = get_or_create(tag);
    if (entry.half_open_active_requests > 0) {
        entry.half_open_active_requests--;
    }
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
