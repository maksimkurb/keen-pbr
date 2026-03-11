#pragma once

#include "../config/config.hpp"

#include <string>

namespace keen_pbr3 {

// Returns 0 if all checks pass, 1 if any check is degraded/missing/error.
int run_status_command(const Config& config, const std::string& config_path);

} // namespace keen_pbr3
