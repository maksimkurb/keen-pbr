#pragma once

#include <functional>
#include <string>

namespace keen_pbr3 {

// Durably replace a regular config file. Existing ownership and mode are
// preserved; a newly created config is private to its owner (0600).
void write_config_atomically(const std::string& config_path,
                             const std::string& body);

enum class ConfigWritePhase {
    BeforeTemporaryWrite,
    BeforeTemporaryFsync,
    BeforeRename,
    BeforeDirectoryFsync,
};

// Test-only fault injection for every durable-write boundary. Production code
// never installs a hook.
void set_config_write_phase_hook_for_testing(std::function<void(ConfigWritePhase)> hook);

} // namespace keen_pbr3
