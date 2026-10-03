#pragma once

#include "../config/config.hpp"

#include <nlohmann/json.hpp>

#include <cstddef>

#include <string>

namespace keen_pbr3 {

// Returns 0 if all checks pass, 1 if any check is degraded/missing/error.
int run_status_command(const Config& config, const std::string& config_path);

// Render a status response using the daemon's canonical health report.  The
// control protocol deliberately supplies the report rather than a partial
// RuleState projection, because only the daemon owns the active FirewallPlan.
int run_status_command(const Config& config, const std::string& config_path,
                       const nlohmann::json& routing_health);

// A daemon from before canonical control health was available has no report;
// retain the useful offline route/policy display and mark firewall health
// unavailable instead of rejecting the response.
int run_status_command(const Config& config, const std::string& config_path,
                       std::nullptr_t routing_health);

} // namespace keen_pbr3
