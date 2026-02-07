#pragma once

#include <chrono>
#include <map>
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

// --- List definitions ---

struct ListConfig {
    std::optional<std::string> url;
    std::vector<std::string> domains;
    std::vector<std::string> ip_cidrs;
    std::optional<std::string> file;
    uint32_t ttl{0}; // TTL in seconds for dnsmasq-resolved ipset entries (0 = no timeout)
};

// --- Route section ---

struct SkipAction {};

struct RouteRule {
    std::vector<std::string> lists;
    // Single outbound tag, failover chain of tags, or skip action
    std::variant<std::string, std::vector<std::string>, SkipAction> action;
};

struct RouteConfig {
    std::vector<RouteRule> rules;
    std::string fallback;
};

// --- DNS section ---

struct DnsServer {
    std::string tag;
    std::string address; // IP, DoH URL, "system", or "rcode://refused"
    std::optional<std::string> detour;
};

struct DnsRule {
    std::vector<std::string> lists;
    std::string server;
};

struct DnsConfig {
    std::vector<DnsServer> servers;
    std::vector<DnsRule> rules;
    std::string fallback;
};

// --- API section ---

struct ApiConfig {
    bool enabled{false};
    std::string listen{"127.0.0.1:8080"};
};

// --- Top-level Config ---

struct Config {
    DaemonConfig daemon;
    ApiConfig api;
    std::vector<Outbound> outbounds;
    DnsConfig dns;
    RouteConfig route;
    std::map<std::string, ListConfig> lists;
};

// --- JSON deserialization ---

Config parse_config(const std::string& json_str);

} // namespace keen_pbr3
