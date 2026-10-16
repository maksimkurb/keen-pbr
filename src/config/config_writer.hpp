#pragma once

#include <functional>
#include <string>

namespace keen_pbr3 {

// Durably replace a regular config file. Existing ownership and mode are
// preserved; a newly created config is private to its owner (0600).
void write_config_atomically(const std::string& config_path,
                             const std::string& body);

// Restore a previously opened config inode without materializing its contents
// in memory. The source descriptor is not closed and its file offset is not
// changed.
void write_config_atomically_from_fd(const std::string& config_path,
                                     int source_fd);

// Rewrites a config file whose text used a legacy form migrated at load time
// (see upgraded_config_text()) into the upgraded form, keeping a one-time
// backup of the original in `<config>.bak-pre-members` (never overwritten).
// Call only after the original text parsed AND validated. Comments and key
// order of the original are not preserved (the backup keeps them). Failures
// are logged, never thrown. Returns true when the file was rewritten.
bool upgrade_config_file_if_needed(const std::string& config_path,
                                   const std::string& original_text);

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
