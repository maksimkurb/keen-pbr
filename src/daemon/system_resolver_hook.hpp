#pragma once

#include <functional>
#include <string>

#include "../config/config.hpp"

namespace keen_pbr3 {

using HookCommandExecutor = std::function<int(const std::string& command)>;

std::string build_system_resolver_reload_command(const Config& config);

bool execute_system_resolver_reload_hook(
    const Config& config,
    const HookCommandExecutor& executor,
    std::string& command,
    int& exit_code);

int default_hook_command_executor(const std::string& command);

} // namespace keen_pbr3
