#include "system_resolver_hook.hpp"

#include <cstdlib>
#include <sys/wait.h>

namespace keen_pbr3 {

std::string build_system_resolver_reload_command(const Config& config) {
    if (!config.dns.has_value() || !config.dns->system_resolver.has_value()) {
        return {};
    }

    const std::string& hook = config.dns->system_resolver->hook;
    if (hook.empty()) {
        return {};
    }

    return hook + " reload";
}

bool execute_system_resolver_reload_hook(
    const Config& config,
    const HookCommandExecutor& executor,
    std::string& command,
    int& exit_code) {
    command = build_system_resolver_reload_command(config);
    if (command.empty()) {
        exit_code = 0;
        return true;
    }

    exit_code = executor(command);
    return exit_code == 0;
}

int default_hook_command_executor(const std::string& command) {
    const int status = std::system(command.c_str());
    if (status == -1) {
        return -1;
    }

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }

    return status;
}

} // namespace keen_pbr3
