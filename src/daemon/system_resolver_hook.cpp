#include "system_resolver_hook.hpp"

#include "../util/safe_exec.hpp"

namespace keen_pbr3 {

const char* system_resolver_hook_path() noexcept {
#ifdef KEEN_PBR_SYSTEM_RESOLVER_HOOK
    return KEEN_PBR_SYSTEM_RESOLVER_HOOK;
#else
    return "/usr/lib/keen-pbr/dnsmasq.sh";
#endif
}

std::vector<std::string> build_system_resolver_hook_args(const Config& config,
                                                         std::string_view action) {
    if (!config.dns.has_value() || !config.dns->system_resolver.has_value()) {
        return {};
    }

    if (action.empty()) {
        return {};
    }

    return {system_resolver_hook_path(), std::string(action)};
}

std::vector<std::string> build_system_resolver_reload_args(const Config& config) {
    return build_system_resolver_hook_args(config, "reload");
}

bool execute_system_resolver_reload_hook(
    const Config& config,
    const HookCommandExecutor& executor,
    std::string& command,
    int& exit_code) {
    const auto args = build_system_resolver_reload_args(config);
    if (args.empty()) {
        command.clear();
        exit_code = 0;
        return true;
    }

    // Build command string for logging
    command.clear();
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) command += ' ';
        command += args[i];
    }

    exit_code = executor(args);
    return exit_code == 0;
}

int default_hook_command_executor(const std::vector<std::string>& args) {
    return safe_exec(args);
}

} // namespace keen_pbr3
