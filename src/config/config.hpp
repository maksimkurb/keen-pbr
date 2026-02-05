#pragma once

#include <chrono>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace keen_pbr3 {

class ConfigError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// --- Daemon section ---

struct DaemonConfig {
    std::string pid_file;
    std::chrono::seconds list_update_interval{86400}; // 24h
};

// --- Outbound types ---

struct InterfaceOutbound {
    std::string tag;
    std::string interface;
    std::optional<std::string> gateway;
    std::optional<std::string> ping_target;
    std::chrono::seconds ping_interval{30};
    std::chrono::seconds ping_timeout{5};
};

struct TableOutbound {
    std::string tag;
    uint32_t table_id;
};

struct BlackholeOutbound {
    std::string tag;
};

using Outbound = std::variant<InterfaceOutbound, TableOutbound, BlackholeOutbound>;

// --- Top-level Config (partial, extended in US-006) ---

struct Config {
    DaemonConfig daemon;
    std::vector<Outbound> outbounds;
};

// --- JSON deserialization ---

Config parse_config(const std::string& json_str);

} // namespace keen_pbr3
