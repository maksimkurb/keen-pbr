#pragma once

#include <cstdint>
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
    std::string cache_dir{"/var/cache/keen-pbr3"};
};

// --- Outbound types ---

struct InterfaceOutbound {
    std::string tag;
    std::string interface;
    std::optional<std::string> gateway;
};

struct TableOutbound {
    std::string tag;
    uint32_t table_id;
};

struct BlackholeOutbound {
    std::string tag;
};

struct IgnoreOutbound {
    std::string tag;
};

struct OutboundGroup {
    uint32_t weight{1};
    std::vector<std::string> outbounds;
};

struct RetryConfig {
    uint32_t attempts{3};
    uint32_t interval_ms{1000};
};

struct CircuitBreakerConfig {
    uint32_t failure_threshold{5};
    uint32_t success_threshold{2};
    uint32_t timeout_ms{30000};
    uint32_t half_open_max_requests{1};
};

struct UrltestOutbound {
    std::string tag;
    std::vector<OutboundGroup> outbound_groups;
    std::string url;
    uint32_t interval_ms{180000};
    uint32_t tolerance_ms{100};
    RetryConfig retry;
    CircuitBreakerConfig circuit_breaker;
};

using Outbound = std::variant<InterfaceOutbound, TableOutbound, BlackholeOutbound,
                              IgnoreOutbound, UrltestOutbound>;

// --- List definitions ---

struct ListConfig {
    std::optional<std::string> url;
    std::vector<std::string> domains;
    std::vector<std::string> ip_cidrs;
    std::optional<std::string> file;
    uint32_t ttl{0}; // TTL in seconds for dnsmasq-resolved ipset entries (0 = no timeout)
};

// --- Route section ---

struct RouteRule {
    std::vector<std::string> lists;
    std::string outbound; // Single outbound tag
};

struct RouteConfig {
    std::vector<RouteRule> rules;
    std::string fallback;
};

// --- DNS section ---

struct DnsServer {
    std::string tag;
    std::string address; // IPv4 or IPv6 address
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

// --- Fwmark section ---

struct FwmarkConfig {
    uint32_t start{0x00010000};
    uint32_t mask{0x00FF0000};
};

// --- Iproute section ---

struct IprouteConfig {
    uint32_t table_start{150};
};

// --- Lists autoupdate section ---

struct ListsAutoupdateConfig {
    bool enabled{false};
    std::string cron;  // 5-field standard cron expression
};

// --- Top-level Config ---

struct Config {
    DaemonConfig daemon;
    ApiConfig api;
    std::vector<Outbound> outbounds;
    DnsConfig dns;
    RouteConfig route;
    std::map<std::string, ListConfig> lists;
    FwmarkConfig fwmark;
    IprouteConfig iproute;
    ListsAutoupdateConfig lists_autoupdate;
};

// --- JSON deserialization ---

Config parse_config(const std::string& json_str);

// --- Fwmark allocation ---

// Maps outbound tag to its assigned fwmark value
using OutboundMarkMap = std::map<std::string, uint32_t>;

// Validates fwmark.mask and assigns sequential fwmarks to interface and table
// outbounds. Blackhole, ignore, and urltest outbounds do NOT get marks.
// Throws ConfigError if mask is invalid or too many outbounds for the mark space.
OutboundMarkMap allocate_outbound_marks(const FwmarkConfig& fwmark_cfg,
                                         const std::vector<Outbound>& outbounds);

} // namespace keen_pbr3
