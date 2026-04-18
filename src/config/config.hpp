#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "../api/generated/api_types.hpp"
#include "../firewall/firewall.hpp"

namespace keen_pbr3 {

class ConfigError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct ConfigValidationIssue {
    std::string path;
    std::string message;
};

class ConfigValidationError : public ConfigError {
public:
    explicit ConfigValidationError(std::vector<ConfigValidationIssue> issues);

    const std::vector<ConfigValidationIssue>& issues() const noexcept {
        return issues_;
    }

private:
    static std::string build_message(const std::vector<ConfigValidationIssue>& issues);

    std::vector<ConfigValidationIssue> issues_;
};

// Type aliases: map generated QuickType names to conventional keen-pbr names.
// All config structs now live in api:: with full from_json/to_json support.
using Config               = api::ConfigObject;
using DaemonConfig         = api::Daemon;
using ApiConfig            = api::ApiConfig;
using Outbound             = api::OutboundElement;
using OutboundType         = api::OutboundType;  // enum: INTERFACE, TABLE, BLACKHOLE, IGNORE, URLTEST
using OutboundGroup        = api::OutboundGroupElement;
using RetryConfig          = api::Retry;
using CircuitBreakerConfig = api::CircuitBreakerConfig;
using ListConfig           = api::ListConfigValue;
using DnsServer            = api::DnsServerElement;
using DnsTestServer        = api::DnsTestServer;
using DnsRule              = api::DnsRuleElement;
using DnsConfig            = api::Dns;
using RouteRule            = api::RouteRuleElement;
using RouteConfig          = api::Route;
using FwmarkConfig         = api::Fwmark;
using IprouteConfig        = api::Iproute;
using ListsAutoupdateConfig = api::ListsAutoupdate;
// Note: DnsRule.list (not .lists) and RouteRule.list (not .lists) match JSON keys.

constexpr std::size_t kDefaultMaxFileSizeBytes = std::size_t{8} * 1024U * 1024U; // 8 MiB

inline const std::vector<std::string>& route_rule_lists(const RouteRule& rule) {
    static const std::vector<std::string> empty;
    return rule.list ? *rule.list : empty;
}

inline bool route_rule_enabled(const RouteRule& rule) {
    return rule.enabled.value_or(true);
}

inline bool dns_rule_enabled(const DnsRule& rule) {
    return rule.enabled.value_or(true);
}

// --- JSON deserialization and validation ---

Config parse_config(const std::string& json_str);
void validate_config(const Config& config);
Config parse_and_validate_config(const std::string& json_str);
size_t max_file_size_bytes(const Config& config);
FirewallBackendPreference firewall_backend_preference(const Config& config);

// --- Fwmark allocation ---

// Maps outbound tag to its assigned fwmark value
using OutboundMarkMap = std::map<std::string, uint32_t>;

// Validates fwmark.mask and assigns sequential fwmarks to interface and table
// outbounds. Blackhole, ignore, and urltest outbounds do NOT get marks.
// Throws ConfigError if mask is invalid or too many outbounds for the mark space.
OutboundMarkMap allocate_outbound_marks(const FwmarkConfig& fwmark_cfg,
                                         const std::vector<Outbound>& outbounds);

uint32_t fwmark_start_value(const FwmarkConfig& fwmark_cfg);
uint32_t fwmark_mask_value(const FwmarkConfig& fwmark_cfg);

} // namespace keen_pbr3
