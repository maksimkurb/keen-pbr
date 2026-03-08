#pragma once

#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "../api/generated/api_types.hpp"

namespace keen_pbr3 {

class ConfigError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Type aliases: map generated QuickType names to conventional keen-pbr3 names.
// All config structs now live in api:: with full from_json/to_json support.
using Config               = api::ConfigObject;
using DaemonConfig         = api::Daemon;
using ApiConfig            = api::ApiConfig;
using Outbound             = api::OutboundElement;
using OutboundType         = api::Type;          // enum: INTERFACE, TABLE, BLACKHOLE, IGNORE, URLTEST
using OutboundGroup        = api::OutboundGroupElement;
using RetryConfig          = api::Retry;
using CircuitBreakerConfig = api::CircuitBreakerConfig;
using ListConfig           = api::ListConfigValue;
using DnsServer            = api::DnsServerElement;
using DnsRule              = api::DnsRuleElement;
using DnsConfig            = api::Dns;
using RouteRule            = api::RouteRuleElement;
using RouteConfig          = api::Route;
using FwmarkConfig         = api::Fwmark;
using IprouteConfig        = api::Iproute;
using ListsAutoupdateConfig = api::ListsAutoupdate;
// Note: DnsRule.list (not .lists) and RouteRule.list (not .lists) match JSON keys.

// --- JSON deserialization with validation ---

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
