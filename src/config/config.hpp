#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <istream>
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
using AuthenticationConfig = api::AuthenticationConfigClass;
using CorsConfig           = api::CorsConfigClass;
using Outbound             = api::OutboundElement;
using OutboundType         = api::OutboundType;  // enum: INTERFACE, TABLE, BLACKHOLE, IGNORE, URLTEST
using OutboundGroup        = api::OutboundGroupElement;
using RetryConfig          = api::Retry;
using CircuitBreakerConfig = api::CircuitBreakerConfig;
using ListConfig           = api::ListConfigValue;
using DnsServer            = api::DnsServerElement;
using DnsTestServer        = api::DnsTestServer;
using DnsRule              = api::DnsRuleElement;
using DnsConfig            = api::DnsConfigClass;
using ResolverIntegrationMode = api::ResolverIntegration;  // enum: NONE, DNSMASQ
using RouteRule            = api::RouteRuleElement;
using RouteConfig          = api::Route;
using FwmarkConfig         = api::Fwmark;
using IprouteConfig        = api::Iproute;
using ListsAutoupdateConfig = api::ListsAutoupdate;
using InterceptConfig      = api::InterceptConfigClass;
using InterceptDnsConfig   = api::InterceptDnsConfigClass;
using InterceptL7Config    = api::L7;
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

inline const std::vector<api::OutboundGroupMemberElement>& outbound_group_members(
    const OutboundGroup& group) {
    static const std::vector<api::OutboundGroupMemberElement> empty;
    return group.members ? *group.members : empty;
}

inline std::vector<std::string> outbound_group_tags(const OutboundGroup& group) {
    std::vector<std::string> tags;
    for (const auto& member : outbound_group_members(group)) {
        tags.push_back(member.outbound);
    }
    return tags;
}

inline std::string outbound_group_target(const OutboundGroup& group,
                                         const std::string& tag) {
    for (const auto& member : outbound_group_members(group)) {
        if (member.outbound == tag) return member.target.value_or(std::string{});
    }
    return {};
}

inline constexpr int64_t kMinBalanceWeight = 1;
inline constexpr int64_t kMaxBalanceWeight = 100;

// Share weight of a group member in balance mode (default 1).
inline uint32_t outbound_group_balance_weight(const OutboundGroup& group,
                                              const std::string& tag) {
    for (const auto& member : outbound_group_members(group)) {
        if (member.outbound == tag) {
            return static_cast<uint32_t>(member.weight.value_or(1));
        }
    }
    return 1U;
}

// What the config asks for, independent of the build.
inline bool outbound_requests_balance(const Outbound& outbound) {
    return outbound.strategy.value_or(api::Strategy::PRIORITY) ==
           api::Strategy::BALANCE;
}

// Whether keen-pbr balances this outbound.  The Keenetic platform build does
// not compile balancing (config validation rejects it), so every balance
// branch behind this predicate is dead there.
inline bool outbound_uses_balance(const Outbound& outbound) {
#ifdef KEEN_PBR_PLATFORM_KEENETIC
    (void)outbound;
    return false;
#else
    return outbound_requests_balance(outbound);
#endif
}

// Effective resolver integration mode of a configuration.  An explicit
// dns.resolver_integration always wins.  When the field is absent (configs
// written before the option existed) a config that defines non-empty dns.rules
// keeps using dnsmasq, everything else runs without a resolver integration.
inline ResolverIntegrationMode effective_resolver_integration(const Config& config) {
    if (!config.dns.has_value()) return ResolverIntegrationMode::NONE;
    if (config.dns->resolver_integration.has_value()) {
        return *config.dns->resolver_integration;
    }
    const bool has_rules = !config.dns->rules.value_or(std::vector<DnsRule>{}).empty();
    return has_rules ? ResolverIntegrationMode::DNSMASQ
                     : ResolverIntegrationMode::NONE;
}

inline const char* resolver_integration_name(ResolverIntegrationMode mode) {
    return mode == ResolverIntegrationMode::DNSMASQ ? "dnsmasq" : "none";
}

// Non-fatal configuration findings (deprecated or ineffective settings).
// validate_config() logs them; they never make validation fail.
std::vector<std::string> config_warnings(const Config& config);

// --- JSON deserialization and validation ---

Config parse_config(const std::string& json_str);
// When the config text uses a legacy form that load-time migration rewrites
// (legacy icmptest probes, group outbounds/candidates/weight), returns the
// upgraded JSON text; nullopt when nothing changes or the text is not JSON.
// Pure: callers decide whether to persist it, after parse+validate succeeded.
std::optional<std::string> upgraded_config_text(const std::string& json_str);
Config parse_config(std::istream& json_stream);
void validate_config(const Config& config);
Config parse_and_validate_config(const std::string& json_str);
size_t max_file_size_bytes(const Config& config);
FirewallBackendPreference firewall_backend_preference(const Config& config);

// --- Fwmark allocation ---

// Maps outbound tag to its assigned fwmark value
using OutboundMarkMap = std::map<std::string, uint32_t>;

// Reserved map key for traffic initiated by keen-pbr itself (DNS/list fetches).
// It is deliberately distinct from the mark used for user packet classification.
std::string internal_detour_mark_key(const std::string& outbound_tag);

// Validates fwmark.mask and assigns deterministic normal and internal-detour
// fwmarks to each routable outbound. Blackhole and ignore outbounds get none.
// Throws ConfigError if mask is invalid or too many outbounds for the mark space.
OutboundMarkMap allocate_outbound_marks(const FwmarkConfig& fwmark_cfg,
                                         const std::vector<Outbound>& outbounds);

uint32_t fwmark_start_value(const FwmarkConfig& fwmark_cfg);
uint32_t fwmark_mask_value(const FwmarkConfig& fwmark_cfg);

} // namespace keen_pbr3
