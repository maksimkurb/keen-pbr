#pragma once

#include "../config/config.hpp"
#include "../routing/firewall_state.hpp"

#include <nlohmann/json.hpp>

#include <string>

namespace keen_pbr3 {

// Returns 0 if all checks pass, 1 if any check is degraded/missing/error.
int run_status_command(const Config& config, const std::string& config_path);
int run_status_command(const Config& config, const std::string& config_path,
                       const std::vector<RuleState>& realized_rules);

// Render a status response obtained from the daemon control socket.  The
// response carries the daemon's active config and its live health checks, so
// this command does not inspect or depend on the on-disk config itself.
int run_status_command(const nlohmann::json& response);

} // namespace keen_pbr3
