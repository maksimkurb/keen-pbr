#pragma once

#include <chrono>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "../config/config.hpp"

namespace keen_pbr3 {

class HealthCheckError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

enum class HealthStatus {
    healthy,
    unhealthy,
    unknown
};

// Result of a single health check
struct HealthResult {
    std::string tag;
    HealthStatus status{HealthStatus::unknown};
    std::string detail;
};

// Monitors interface availability using ICMP ping
class HealthChecker {
public:
    HealthChecker() = default;

    // Register an interface outbound for health checking.
    // Only InterfaceOutbounds with a ping_target are registered.
    void register_outbound(const InterfaceOutbound& outbound);

    // Check health of a single outbound by tag.
    // Returns true if healthy, false if unhealthy or unknown.
    bool check(const std::string& tag);

    // Check health of all registered outbounds.
    // Returns a map of tag -> HealthResult.
    std::map<std::string, HealthResult> check_all();

    // Query the last known health status for a tag (without re-checking).
    HealthStatus last_status(const std::string& tag) const;

    // Get all last known results.
    const std::map<std::string, HealthResult>& results() const;

    // Returns true if the tag is registered for health checking.
    bool has_target(const std::string& tag) const;

private:
    struct PingTarget {
        std::string tag;
        std::string interface;
        std::string target;
        std::chrono::seconds timeout{5};
    };

    bool ping(const PingTarget& target);

    std::map<std::string, PingTarget> targets_;
    std::map<std::string, HealthResult> results_;
};

} // namespace keen_pbr3
