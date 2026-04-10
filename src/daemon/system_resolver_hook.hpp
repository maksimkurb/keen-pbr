#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "../config/config.hpp"

namespace keen_pbr3 {

using HookCommandExecutor = std::function<int(const std::vector<std::string>& args)>;

const char* system_resolver_hook_path() noexcept;
std::vector<std::string> build_system_resolver_hook_args(const Config& config,
                                                          std::string_view action);
std::vector<std::string> build_system_resolver_reload_args(const Config& config);

bool execute_system_resolver_reload_hook(
    const Config& config,
    const HookCommandExecutor& executor,
    std::string& command,
    int& exit_code);

int default_hook_command_executor(const std::vector<std::string>& args);

} // namespace keen_pbr3
