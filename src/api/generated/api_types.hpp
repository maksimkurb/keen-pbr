// Generated from docs/openapi.yaml via scripts/generate_api_types.sh
// Run "make generate" to regenerate (requires Node.js).

//  To parse this JSON data, first install
//
//      json.hpp  https://github.com/nlohmann/json
//
//  Then include this file, and then do
//
//     KeenPbr3TypesXLs8I0 data = nlohmann::json::parse(jsonString);

#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <nlohmann/json.hpp>

#ifndef NLOHMANN_OPT_HELPER
#define NLOHMANN_OPT_HELPER
namespace nlohmann {
    template <typename T>
    struct adl_serializer<std::shared_ptr<T>> {
        static void to_json(json & j, const std::shared_ptr<T> & opt) {
            if (!opt) j = nullptr; else j = *opt;
        }

        static std::shared_ptr<T> from_json(const json & j) {
            if (j.is_null()) return std::make_shared<T>(); else return std::make_shared<T>(j.get<T>());
        }
    };
    template <typename T>
    struct adl_serializer<std::optional<T>> {
        static void to_json(json & j, const std::optional<T> & opt) {
            if (!opt) j = nullptr; else j = *opt;
        }

        static std::optional<T> from_json(const json & j) {
            if (j.is_null()) return std::make_optional<T>(); else return std::make_optional<T>(j.get<T>());
        }
    };
}
#endif

namespace keen_pbr3 {
namespace api {
    using nlohmann::json;

    #ifndef NLOHMANN_UNTYPED_keen_pbr3_api_HELPER
    #define NLOHMANN_UNTYPED_keen_pbr3_api_HELPER
    inline json get_untyped(const json & j, const char * property) {
        if (j.find(property) != j.end()) {
            return j.at(property).get<json>();
        }
        return json();
    }

    inline json get_untyped(const json & j, std::string property) {
        return get_untyped(j, property.data());
    }
    #endif

    #ifndef NLOHMANN_OPTIONAL_keen_pbr3_api_HELPER
    #define NLOHMANN_OPTIONAL_keen_pbr3_api_HELPER
    template <typename T>
    inline std::shared_ptr<T> get_heap_optional(const json & j, const char * property) {
        auto it = j.find(property);
        if (it != j.end() && !it->is_null()) {
            return j.at(property).get<std::shared_ptr<T>>();
        }
        return std::shared_ptr<T>();
    }

    template <typename T>
    inline std::shared_ptr<T> get_heap_optional(const json & j, std::string property) {
        return get_heap_optional<T>(j, property.data());
    }
    template <typename T>
    inline std::optional<T> get_stack_optional(const json & j, const char * property) {
        auto it = j.find(property);
        if (it != j.end() && !it->is_null()) {
            return j.at(property).get<std::optional<T>>();
        }
        return std::optional<T>();
    }

    template <typename T>
    inline std::optional<T> get_stack_optional(const json & j, std::string property) {
        return get_stack_optional<T>(j, property.data());
    }
    #endif

    struct ApiConfig {
        std::optional<bool> enabled;
        std::optional<std::string> listen;
    };

    struct CacheMetadata {
        std::optional<int64_t> cidrs;
        std::optional<int64_t> domains;
        std::optional<std::string> download_time;
        std::optional<std::string> etag;
        std::optional<int64_t> ips;
        std::optional<std::string> last_modified;
        std::optional<std::string> url;
    };

    enum class CheckStatus : int { MISMATCH, MISSING, OK };

    struct CircuitBreakerConfig {
        std::optional<int64_t> failure_threshold;
        std::optional<int64_t> half_open_max_requests;
        std::optional<int64_t> success_threshold;
        std::optional<int64_t> timeout_ms;
    };

    struct Daemon {
        std::optional<std::string> cache_dir;
        std::optional<std::string> pid_file;
    };

    struct DnsRuleElement {
        std::vector<std::string> list;
        std::string server;
    };

    struct DnsServerElement {
        std::string address;
        std::optional<std::string> detour;
        std::string tag;
    };

    struct Dns {
        std::optional<std::string> fallback;
        std::optional<std::vector<DnsRuleElement>> rules;
        std::optional<std::vector<DnsServerElement>> servers;
    };

    struct Fwmark {
        std::optional<int64_t> mask;
        std::optional<int64_t> start;
    };

    struct Iproute {
        std::optional<int64_t> table_start;
    };

    struct ListConfigValue {
        std::optional<std::vector<std::string>> domains;
        std::optional<std::string> file;
        std::optional<std::vector<std::string>> ip_cidrs;
        std::optional<int64_t> ttl_ms;
        std::optional<std::string> url;
    };

    struct ListsAutoupdate {
        std::optional<std::string> cron;
        std::optional<bool> enabled;
    };

    struct OutboundGroupElement {
        std::vector<std::string> outbounds;
        std::optional<int64_t> weight;
    };

    struct Retry {
        std::optional<int64_t> attempts;
        std::optional<int64_t> interval_ms;
    };

    enum class Type : int { BLACKHOLE, IGNORE, INTERFACE, TABLE, URLTEST };

    struct OutboundElement {
        std::optional<CircuitBreakerConfig> circuit_breaker;
        std::optional<std::string> gateway;
        std::optional<std::string> interface;
        std::optional<int64_t> interval_ms;
        std::optional<std::vector<OutboundGroupElement>> outbound_groups;
        std::optional<Retry> retry;
        std::optional<int64_t> table;
        std::string tag;
        std::optional<int64_t> tolerance_ms;
        Type type;
        std::optional<std::string> url;
    };

    struct RouteRuleElement {
        std::vector<std::string> list;
        std::string outbound;
    };

    struct Route {
        std::optional<std::string> fallback;
        std::optional<std::vector<RouteRuleElement>> rules;
    };

    struct ConfigObject {
        std::optional<ApiConfig> api;
        std::optional<Daemon> daemon;
        std::optional<Dns> dns;
        std::optional<Fwmark> fwmark;
        std::optional<Iproute> iproute;
        std::optional<std::map<std::string, ListConfigValue>> lists;
        std::optional<ListsAutoupdate> lists_autoupdate;
        std::optional<std::vector<OutboundElement>> outbounds;
        std::optional<Route> route;
    };

    enum class ConfigUpdateResponseStatus : int { OK };

    struct ConfigUpdateResponse {
        std::string message;
        ConfigUpdateResponseStatus status;
    };

    struct ErrorResponse {
        std::string error;
    };

    struct FirewallChain {
        bool chain_present;
        std::optional<std::string> detail;
        bool prerouting_hook_present;
    };

    struct FirewallRuleCheck {
        std::string action;
        std::optional<std::string> actual_fwmark;
        std::optional<std::string> detail;
        std::optional<std::string> expected_fwmark;
        std::string set_name;
        CheckStatus status;
    };

    enum class CircuitBreaker : int { CLOSED, HALF_OPEN, OPEN };

    struct HealthChild {
        std::optional<CircuitBreaker> circuit_breaker;
        std::optional<std::string> error;
        int64_t latency_ms;
        bool success;
        std::string tag;
    };

    enum class HealthEntryStatus : int { DEGRADED, HEALTHY, UNKNOWN };

    struct HealthEntry {
        std::optional<std::vector<HealthChild>> children;
        std::optional<std::string> selected_outbound;
        HealthEntryStatus status;
        std::string tag;
        Type type;
    };

    enum class HealthResponseStatus : int { RUNNING };

    struct HealthResponse {
        std::vector<HealthEntry> outbounds;
        HealthResponseStatus status;
        std::string version;
    };

    struct PolicyRuleCheck {
        std::optional<std::string> detail;
        int64_t expected_table;
        std::string fwmark;
        std::string fwmask;
        int64_t priority;
        bool rule_present_v4;
        bool rule_present_v6;
        CheckStatus status;
    };

    struct ReloadResponse {
        std::string message;
        ConfigUpdateResponseStatus status;
    };

    struct RouteTableCheck {
        bool default_route_present;
        std::optional<std::string> detail;
        std::optional<std::string> expected_gateway;
        std::optional<std::string> expected_interface;
        bool gateway_matches;
        bool interface_matches;
        std::string outbound_tag;
        CheckStatus status;
        bool table_exists;
        int64_t table_id;
    };

    enum class RoutingHealthErrorResponseOverall : int { ERROR };

    struct RoutingHealthErrorResponse {
        std::string error;
        RoutingHealthErrorResponseOverall overall;
    };

    enum class FirewallBackend : int { IPTABLES, NFTABLES };

    enum class RoutingHealthResponseOverall : int { DEGRADED, ERROR, OK };

    struct RoutingHealthResponse {
        FirewallChain firewall;
        FirewallBackend firewall_backend;
        std::vector<FirewallRuleCheck> firewall_rules;
        RoutingHealthResponseOverall overall;
        std::vector<PolicyRuleCheck> policy_rules;
        std::vector<RouteTableCheck> route_tables;
    };

    struct KeenPbr3TypesXLs8I0 {
        std::optional<ApiConfig> api_config;
        std::optional<CacheMetadata> cache_metadata;
        std::optional<CheckStatus> check_status;
        std::optional<CircuitBreakerConfig> circuit_breaker_config;
        std::optional<ConfigObject> config_object;
        std::optional<ConfigUpdateResponse> config_update_response;
        std::optional<Daemon> daemon_config;
        std::optional<Dns> dns_config;
        std::optional<DnsRuleElement> dns_rule;
        std::optional<DnsServerElement> dns_server;
        std::optional<ErrorResponse> error_response;
        std::optional<FirewallChain> firewall_chain;
        std::optional<FirewallRuleCheck> firewall_rule_check;
        std::optional<Fwmark> fwmark_config;
        std::optional<HealthChild> health_child;
        std::optional<HealthEntry> health_entry;
        std::optional<HealthResponse> health_response;
        std::optional<Iproute> iproute_config;
        std::optional<ListConfigValue> list_config;
        std::optional<ListsAutoupdate> lists_autoupdate_config;
        std::optional<OutboundElement> outbound;
        std::optional<OutboundGroupElement> outbound_group;
        std::optional<PolicyRuleCheck> policy_rule_check;
        std::optional<ReloadResponse> reload_response;
        std::optional<Retry> retry_config;
        std::optional<Route> route_config;
        std::optional<RouteRuleElement> route_rule;
        std::optional<RouteTableCheck> route_table_check;
        std::optional<RoutingHealthErrorResponse> routing_health_error_response;
        std::optional<RoutingHealthResponse> routing_health_response;
    };
}
}

namespace keen_pbr3 {
namespace api {
    void from_json(const json & j, ApiConfig & x);
    void to_json(json & j, const ApiConfig & x);

    void from_json(const json & j, CacheMetadata & x);
    void to_json(json & j, const CacheMetadata & x);

    void from_json(const json & j, CircuitBreakerConfig & x);
    void to_json(json & j, const CircuitBreakerConfig & x);

    void from_json(const json & j, Daemon & x);
    void to_json(json & j, const Daemon & x);

    void from_json(const json & j, DnsRuleElement & x);
    void to_json(json & j, const DnsRuleElement & x);

    void from_json(const json & j, DnsServerElement & x);
    void to_json(json & j, const DnsServerElement & x);

    void from_json(const json & j, Dns & x);
    void to_json(json & j, const Dns & x);

    void from_json(const json & j, Fwmark & x);
    void to_json(json & j, const Fwmark & x);

    void from_json(const json & j, Iproute & x);
    void to_json(json & j, const Iproute & x);

    void from_json(const json & j, ListConfigValue & x);
    void to_json(json & j, const ListConfigValue & x);

    void from_json(const json & j, ListsAutoupdate & x);
    void to_json(json & j, const ListsAutoupdate & x);

    void from_json(const json & j, OutboundGroupElement & x);
    void to_json(json & j, const OutboundGroupElement & x);

    void from_json(const json & j, Retry & x);
    void to_json(json & j, const Retry & x);

    void from_json(const json & j, OutboundElement & x);
    void to_json(json & j, const OutboundElement & x);

    void from_json(const json & j, RouteRuleElement & x);
    void to_json(json & j, const RouteRuleElement & x);

    void from_json(const json & j, Route & x);
    void to_json(json & j, const Route & x);

    void from_json(const json & j, ConfigObject & x);
    void to_json(json & j, const ConfigObject & x);

    void from_json(const json & j, ConfigUpdateResponse & x);
    void to_json(json & j, const ConfigUpdateResponse & x);

    void from_json(const json & j, ErrorResponse & x);
    void to_json(json & j, const ErrorResponse & x);

    void from_json(const json & j, FirewallChain & x);
    void to_json(json & j, const FirewallChain & x);

    void from_json(const json & j, FirewallRuleCheck & x);
    void to_json(json & j, const FirewallRuleCheck & x);

    void from_json(const json & j, HealthChild & x);
    void to_json(json & j, const HealthChild & x);

    void from_json(const json & j, HealthEntry & x);
    void to_json(json & j, const HealthEntry & x);

    void from_json(const json & j, HealthResponse & x);
    void to_json(json & j, const HealthResponse & x);

    void from_json(const json & j, PolicyRuleCheck & x);
    void to_json(json & j, const PolicyRuleCheck & x);

    void from_json(const json & j, ReloadResponse & x);
    void to_json(json & j, const ReloadResponse & x);

    void from_json(const json & j, RouteTableCheck & x);
    void to_json(json & j, const RouteTableCheck & x);

    void from_json(const json & j, RoutingHealthErrorResponse & x);
    void to_json(json & j, const RoutingHealthErrorResponse & x);

    void from_json(const json & j, RoutingHealthResponse & x);
    void to_json(json & j, const RoutingHealthResponse & x);

    void from_json(const json & j, KeenPbr3TypesXLs8I0 & x);
    void to_json(json & j, const KeenPbr3TypesXLs8I0 & x);

    void from_json(const json & j, CheckStatus & x);
    void to_json(json & j, const CheckStatus & x);

    void from_json(const json & j, Type & x);
    void to_json(json & j, const Type & x);

    void from_json(const json & j, ConfigUpdateResponseStatus & x);
    void to_json(json & j, const ConfigUpdateResponseStatus & x);

    void from_json(const json & j, CircuitBreaker & x);
    void to_json(json & j, const CircuitBreaker & x);

    void from_json(const json & j, HealthEntryStatus & x);
    void to_json(json & j, const HealthEntryStatus & x);

    void from_json(const json & j, HealthResponseStatus & x);
    void to_json(json & j, const HealthResponseStatus & x);

    void from_json(const json & j, RoutingHealthErrorResponseOverall & x);
    void to_json(json & j, const RoutingHealthErrorResponseOverall & x);

    void from_json(const json & j, FirewallBackend & x);
    void to_json(json & j, const FirewallBackend & x);

    void from_json(const json & j, RoutingHealthResponseOverall & x);
    void to_json(json & j, const RoutingHealthResponseOverall & x);

    inline void from_json(const json & j, ApiConfig& x) {
        x.enabled = get_stack_optional<bool>(j, "enabled");
        x.listen = get_stack_optional<std::string>(j, "listen");
    }

    inline void to_json(json & j, const ApiConfig & x) {
        j = json::object();
        j["enabled"] = x.enabled;
        j["listen"] = x.listen;
    }

    inline void from_json(const json & j, CacheMetadata& x) {
        x.cidrs = get_stack_optional<int64_t>(j, "cidrs");
        x.domains = get_stack_optional<int64_t>(j, "domains");
        x.download_time = get_stack_optional<std::string>(j, "download_time");
        x.etag = get_stack_optional<std::string>(j, "etag");
        x.ips = get_stack_optional<int64_t>(j, "ips");
        x.last_modified = get_stack_optional<std::string>(j, "last_modified");
        x.url = get_stack_optional<std::string>(j, "url");
    }

    inline void to_json(json & j, const CacheMetadata & x) {
        j = json::object();
        j["cidrs"] = x.cidrs;
        j["domains"] = x.domains;
        j["download_time"] = x.download_time;
        j["etag"] = x.etag;
        j["ips"] = x.ips;
        j["last_modified"] = x.last_modified;
        j["url"] = x.url;
    }

    inline void from_json(const json & j, CircuitBreakerConfig& x) {
        x.failure_threshold = get_stack_optional<int64_t>(j, "failure_threshold");
        x.half_open_max_requests = get_stack_optional<int64_t>(j, "half_open_max_requests");
        x.success_threshold = get_stack_optional<int64_t>(j, "success_threshold");
        x.timeout_ms = get_stack_optional<int64_t>(j, "timeout_ms");
    }

    inline void to_json(json & j, const CircuitBreakerConfig & x) {
        j = json::object();
        j["failure_threshold"] = x.failure_threshold;
        j["half_open_max_requests"] = x.half_open_max_requests;
        j["success_threshold"] = x.success_threshold;
        j["timeout_ms"] = x.timeout_ms;
    }

    inline void from_json(const json & j, Daemon& x) {
        x.cache_dir = get_stack_optional<std::string>(j, "cache_dir");
        x.pid_file = get_stack_optional<std::string>(j, "pid_file");
    }

    inline void to_json(json & j, const Daemon & x) {
        j = json::object();
        j["cache_dir"] = x.cache_dir;
        j["pid_file"] = x.pid_file;
    }

    inline void from_json(const json & j, DnsRuleElement& x) {
        x.list = j.at("list").get<std::vector<std::string>>();
        x.server = j.at("server").get<std::string>();
    }

    inline void to_json(json & j, const DnsRuleElement & x) {
        j = json::object();
        j["list"] = x.list;
        j["server"] = x.server;
    }

    inline void from_json(const json & j, DnsServerElement& x) {
        x.address = j.at("address").get<std::string>();
        x.detour = get_stack_optional<std::string>(j, "detour");
        x.tag = j.at("tag").get<std::string>();
    }

    inline void to_json(json & j, const DnsServerElement & x) {
        j = json::object();
        j["address"] = x.address;
        j["detour"] = x.detour;
        j["tag"] = x.tag;
    }

    inline void from_json(const json & j, Dns& x) {
        x.fallback = get_stack_optional<std::string>(j, "fallback");
        x.rules = get_stack_optional<std::vector<DnsRuleElement>>(j, "rules");
        x.servers = get_stack_optional<std::vector<DnsServerElement>>(j, "servers");
    }

    inline void to_json(json & j, const Dns & x) {
        j = json::object();
        j["fallback"] = x.fallback;
        j["rules"] = x.rules;
        j["servers"] = x.servers;
    }

    inline void from_json(const json & j, Fwmark& x) {
        x.mask = get_stack_optional<int64_t>(j, "mask");
        x.start = get_stack_optional<int64_t>(j, "start");
    }

    inline void to_json(json & j, const Fwmark & x) {
        j = json::object();
        j["mask"] = x.mask;
        j["start"] = x.start;
    }

    inline void from_json(const json & j, Iproute& x) {
        x.table_start = get_stack_optional<int64_t>(j, "table_start");
    }

    inline void to_json(json & j, const Iproute & x) {
        j = json::object();
        j["table_start"] = x.table_start;
    }

    inline void from_json(const json & j, ListConfigValue& x) {
        x.domains = get_stack_optional<std::vector<std::string>>(j, "domains");
        x.file = get_stack_optional<std::string>(j, "file");
        x.ip_cidrs = get_stack_optional<std::vector<std::string>>(j, "ip_cidrs");
        x.ttl_ms = get_stack_optional<int64_t>(j, "ttl_ms");
        x.url = get_stack_optional<std::string>(j, "url");
    }

    inline void to_json(json & j, const ListConfigValue & x) {
        j = json::object();
        j["domains"] = x.domains;
        j["file"] = x.file;
        j["ip_cidrs"] = x.ip_cidrs;
        j["ttl_ms"] = x.ttl_ms;
        j["url"] = x.url;
    }

    inline void from_json(const json & j, ListsAutoupdate& x) {
        x.cron = get_stack_optional<std::string>(j, "cron");
        x.enabled = get_stack_optional<bool>(j, "enabled");
    }

    inline void to_json(json & j, const ListsAutoupdate & x) {
        j = json::object();
        j["cron"] = x.cron;
        j["enabled"] = x.enabled;
    }

    inline void from_json(const json & j, OutboundGroupElement& x) {
        x.outbounds = j.at("outbounds").get<std::vector<std::string>>();
        x.weight = get_stack_optional<int64_t>(j, "weight");
    }

    inline void to_json(json & j, const OutboundGroupElement & x) {
        j = json::object();
        j["outbounds"] = x.outbounds;
        j["weight"] = x.weight;
    }

    inline void from_json(const json & j, Retry& x) {
        x.attempts = get_stack_optional<int64_t>(j, "attempts");
        x.interval_ms = get_stack_optional<int64_t>(j, "interval_ms");
    }

    inline void to_json(json & j, const Retry & x) {
        j = json::object();
        j["attempts"] = x.attempts;
        j["interval_ms"] = x.interval_ms;
    }

    inline void from_json(const json & j, OutboundElement& x) {
        x.circuit_breaker = get_stack_optional<CircuitBreakerConfig>(j, "circuit_breaker");
        x.gateway = get_stack_optional<std::string>(j, "gateway");
        x.interface = get_stack_optional<std::string>(j, "interface");
        x.interval_ms = get_stack_optional<int64_t>(j, "interval_ms");
        x.outbound_groups = get_stack_optional<std::vector<OutboundGroupElement>>(j, "outbound_groups");
        x.retry = get_stack_optional<Retry>(j, "retry");
        x.table = get_stack_optional<int64_t>(j, "table");
        x.tag = j.at("tag").get<std::string>();
        x.tolerance_ms = get_stack_optional<int64_t>(j, "tolerance_ms");
        x.type = j.at("type").get<Type>();
        x.url = get_stack_optional<std::string>(j, "url");
    }

    inline void to_json(json & j, const OutboundElement & x) {
        j = json::object();
        j["circuit_breaker"] = x.circuit_breaker;
        j["gateway"] = x.gateway;
        j["interface"] = x.interface;
        j["interval_ms"] = x.interval_ms;
        j["outbound_groups"] = x.outbound_groups;
        j["retry"] = x.retry;
        j["table"] = x.table;
        j["tag"] = x.tag;
        j["tolerance_ms"] = x.tolerance_ms;
        j["type"] = x.type;
        j["url"] = x.url;
    }

    inline void from_json(const json & j, RouteRuleElement& x) {
        x.list = j.at("list").get<std::vector<std::string>>();
        x.outbound = j.at("outbound").get<std::string>();
    }

    inline void to_json(json & j, const RouteRuleElement & x) {
        j = json::object();
        j["list"] = x.list;
        j["outbound"] = x.outbound;
    }

    inline void from_json(const json & j, Route& x) {
        x.fallback = get_stack_optional<std::string>(j, "fallback");
        x.rules = get_stack_optional<std::vector<RouteRuleElement>>(j, "rules");
    }

    inline void to_json(json & j, const Route & x) {
        j = json::object();
        j["fallback"] = x.fallback;
        j["rules"] = x.rules;
    }

    inline void from_json(const json & j, ConfigObject& x) {
        x.api = get_stack_optional<ApiConfig>(j, "api");
        x.daemon = get_stack_optional<Daemon>(j, "daemon");
        x.dns = get_stack_optional<Dns>(j, "dns");
        x.fwmark = get_stack_optional<Fwmark>(j, "fwmark");
        x.iproute = get_stack_optional<Iproute>(j, "iproute");
        x.lists = get_stack_optional<std::map<std::string, ListConfigValue>>(j, "lists");
        x.lists_autoupdate = get_stack_optional<ListsAutoupdate>(j, "lists_autoupdate");
        x.outbounds = get_stack_optional<std::vector<OutboundElement>>(j, "outbounds");
        x.route = get_stack_optional<Route>(j, "route");
    }

    inline void to_json(json & j, const ConfigObject & x) {
        j = json::object();
        j["api"] = x.api;
        j["daemon"] = x.daemon;
        j["dns"] = x.dns;
        j["fwmark"] = x.fwmark;
        j["iproute"] = x.iproute;
        j["lists"] = x.lists;
        j["lists_autoupdate"] = x.lists_autoupdate;
        j["outbounds"] = x.outbounds;
        j["route"] = x.route;
    }

    inline void from_json(const json & j, ConfigUpdateResponse& x) {
        x.message = j.at("message").get<std::string>();
        x.status = j.at("status").get<ConfigUpdateResponseStatus>();
    }

    inline void to_json(json & j, const ConfigUpdateResponse & x) {
        j = json::object();
        j["message"] = x.message;
        j["status"] = x.status;
    }

    inline void from_json(const json & j, ErrorResponse& x) {
        x.error = j.at("error").get<std::string>();
    }

    inline void to_json(json & j, const ErrorResponse & x) {
        j = json::object();
        j["error"] = x.error;
    }

    inline void from_json(const json & j, FirewallChain& x) {
        x.chain_present = j.at("chain_present").get<bool>();
        x.detail = get_stack_optional<std::string>(j, "detail");
        x.prerouting_hook_present = j.at("prerouting_hook_present").get<bool>();
    }

    inline void to_json(json & j, const FirewallChain & x) {
        j = json::object();
        j["chain_present"] = x.chain_present;
        j["detail"] = x.detail;
        j["prerouting_hook_present"] = x.prerouting_hook_present;
    }

    inline void from_json(const json & j, FirewallRuleCheck& x) {
        x.action = j.at("action").get<std::string>();
        x.actual_fwmark = get_stack_optional<std::string>(j, "actual_fwmark");
        x.detail = get_stack_optional<std::string>(j, "detail");
        x.expected_fwmark = get_stack_optional<std::string>(j, "expected_fwmark");
        x.set_name = j.at("set_name").get<std::string>();
        x.status = j.at("status").get<CheckStatus>();
    }

    inline void to_json(json & j, const FirewallRuleCheck & x) {
        j = json::object();
        j["action"] = x.action;
        j["actual_fwmark"] = x.actual_fwmark;
        j["detail"] = x.detail;
        j["expected_fwmark"] = x.expected_fwmark;
        j["set_name"] = x.set_name;
        j["status"] = x.status;
    }

    inline void from_json(const json & j, HealthChild& x) {
        x.circuit_breaker = get_stack_optional<CircuitBreaker>(j, "circuit_breaker");
        x.error = get_stack_optional<std::string>(j, "error");
        x.latency_ms = j.at("latency_ms").get<int64_t>();
        x.success = j.at("success").get<bool>();
        x.tag = j.at("tag").get<std::string>();
    }

    inline void to_json(json & j, const HealthChild & x) {
        j = json::object();
        j["circuit_breaker"] = x.circuit_breaker;
        j["error"] = x.error;
        j["latency_ms"] = x.latency_ms;
        j["success"] = x.success;
        j["tag"] = x.tag;
    }

    inline void from_json(const json & j, HealthEntry& x) {
        x.children = get_stack_optional<std::vector<HealthChild>>(j, "children");
        x.selected_outbound = get_stack_optional<std::string>(j, "selected_outbound");
        x.status = j.at("status").get<HealthEntryStatus>();
        x.tag = j.at("tag").get<std::string>();
        x.type = j.at("type").get<Type>();
    }

    inline void to_json(json & j, const HealthEntry & x) {
        j = json::object();
        j["children"] = x.children;
        j["selected_outbound"] = x.selected_outbound;
        j["status"] = x.status;
        j["tag"] = x.tag;
        j["type"] = x.type;
    }

    inline void from_json(const json & j, HealthResponse& x) {
        x.outbounds = j.at("outbounds").get<std::vector<HealthEntry>>();
        x.status = j.at("status").get<HealthResponseStatus>();
        x.version = j.at("version").get<std::string>();
    }

    inline void to_json(json & j, const HealthResponse & x) {
        j = json::object();
        j["outbounds"] = x.outbounds;
        j["status"] = x.status;
        j["version"] = x.version;
    }

    inline void from_json(const json & j, PolicyRuleCheck& x) {
        x.detail = get_stack_optional<std::string>(j, "detail");
        x.expected_table = j.at("expected_table").get<int64_t>();
        x.fwmark = j.at("fwmark").get<std::string>();
        x.fwmask = j.at("fwmask").get<std::string>();
        x.priority = j.at("priority").get<int64_t>();
        x.rule_present_v4 = j.at("rule_present_v4").get<bool>();
        x.rule_present_v6 = j.at("rule_present_v6").get<bool>();
        x.status = j.at("status").get<CheckStatus>();
    }

    inline void to_json(json & j, const PolicyRuleCheck & x) {
        j = json::object();
        j["detail"] = x.detail;
        j["expected_table"] = x.expected_table;
        j["fwmark"] = x.fwmark;
        j["fwmask"] = x.fwmask;
        j["priority"] = x.priority;
        j["rule_present_v4"] = x.rule_present_v4;
        j["rule_present_v6"] = x.rule_present_v6;
        j["status"] = x.status;
    }

    inline void from_json(const json & j, ReloadResponse& x) {
        x.message = j.at("message").get<std::string>();
        x.status = j.at("status").get<ConfigUpdateResponseStatus>();
    }

    inline void to_json(json & j, const ReloadResponse & x) {
        j = json::object();
        j["message"] = x.message;
        j["status"] = x.status;
    }

    inline void from_json(const json & j, RouteTableCheck& x) {
        x.default_route_present = j.at("default_route_present").get<bool>();
        x.detail = get_stack_optional<std::string>(j, "detail");
        x.expected_gateway = get_stack_optional<std::string>(j, "expected_gateway");
        x.expected_interface = get_stack_optional<std::string>(j, "expected_interface");
        x.gateway_matches = j.at("gateway_matches").get<bool>();
        x.interface_matches = j.at("interface_matches").get<bool>();
        x.outbound_tag = j.at("outbound_tag").get<std::string>();
        x.status = j.at("status").get<CheckStatus>();
        x.table_exists = j.at("table_exists").get<bool>();
        x.table_id = j.at("table_id").get<int64_t>();
    }

    inline void to_json(json & j, const RouteTableCheck & x) {
        j = json::object();
        j["default_route_present"] = x.default_route_present;
        j["detail"] = x.detail;
        j["expected_gateway"] = x.expected_gateway;
        j["expected_interface"] = x.expected_interface;
        j["gateway_matches"] = x.gateway_matches;
        j["interface_matches"] = x.interface_matches;
        j["outbound_tag"] = x.outbound_tag;
        j["status"] = x.status;
        j["table_exists"] = x.table_exists;
        j["table_id"] = x.table_id;
    }

    inline void from_json(const json & j, RoutingHealthErrorResponse& x) {
        x.error = j.at("error").get<std::string>();
        x.overall = j.at("overall").get<RoutingHealthErrorResponseOverall>();
    }

    inline void to_json(json & j, const RoutingHealthErrorResponse & x) {
        j = json::object();
        j["error"] = x.error;
        j["overall"] = x.overall;
    }

    inline void from_json(const json & j, RoutingHealthResponse& x) {
        x.firewall = j.at("firewall").get<FirewallChain>();
        x.firewall_backend = j.at("firewall_backend").get<FirewallBackend>();
        x.firewall_rules = j.at("firewall_rules").get<std::vector<FirewallRuleCheck>>();
        x.overall = j.at("overall").get<RoutingHealthResponseOverall>();
        x.policy_rules = j.at("policy_rules").get<std::vector<PolicyRuleCheck>>();
        x.route_tables = j.at("route_tables").get<std::vector<RouteTableCheck>>();
    }

    inline void to_json(json & j, const RoutingHealthResponse & x) {
        j = json::object();
        j["firewall"] = x.firewall;
        j["firewall_backend"] = x.firewall_backend;
        j["firewall_rules"] = x.firewall_rules;
        j["overall"] = x.overall;
        j["policy_rules"] = x.policy_rules;
        j["route_tables"] = x.route_tables;
    }

    inline void from_json(const json & j, KeenPbr3TypesXLs8I0& x) {
        x.api_config = get_stack_optional<ApiConfig>(j, "ApiConfig");
        x.cache_metadata = get_stack_optional<CacheMetadata>(j, "CacheMetadata");
        x.check_status = get_stack_optional<CheckStatus>(j, "CheckStatus");
        x.circuit_breaker_config = get_stack_optional<CircuitBreakerConfig>(j, "CircuitBreakerConfig");
        x.config_object = get_stack_optional<ConfigObject>(j, "ConfigObject");
        x.config_update_response = get_stack_optional<ConfigUpdateResponse>(j, "ConfigUpdateResponse");
        x.daemon_config = get_stack_optional<Daemon>(j, "DaemonConfig");
        x.dns_config = get_stack_optional<Dns>(j, "DnsConfig");
        x.dns_rule = get_stack_optional<DnsRuleElement>(j, "DnsRule");
        x.dns_server = get_stack_optional<DnsServerElement>(j, "DnsServer");
        x.error_response = get_stack_optional<ErrorResponse>(j, "ErrorResponse");
        x.firewall_chain = get_stack_optional<FirewallChain>(j, "FirewallChain");
        x.firewall_rule_check = get_stack_optional<FirewallRuleCheck>(j, "FirewallRuleCheck");
        x.fwmark_config = get_stack_optional<Fwmark>(j, "FwmarkConfig");
        x.health_child = get_stack_optional<HealthChild>(j, "HealthChild");
        x.health_entry = get_stack_optional<HealthEntry>(j, "HealthEntry");
        x.health_response = get_stack_optional<HealthResponse>(j, "HealthResponse");
        x.iproute_config = get_stack_optional<Iproute>(j, "IprouteConfig");
        x.list_config = get_stack_optional<ListConfigValue>(j, "ListConfig");
        x.lists_autoupdate_config = get_stack_optional<ListsAutoupdate>(j, "ListsAutoupdateConfig");
        x.outbound = get_stack_optional<OutboundElement>(j, "Outbound");
        x.outbound_group = get_stack_optional<OutboundGroupElement>(j, "OutboundGroup");
        x.policy_rule_check = get_stack_optional<PolicyRuleCheck>(j, "PolicyRuleCheck");
        x.reload_response = get_stack_optional<ReloadResponse>(j, "ReloadResponse");
        x.retry_config = get_stack_optional<Retry>(j, "RetryConfig");
        x.route_config = get_stack_optional<Route>(j, "RouteConfig");
        x.route_rule = get_stack_optional<RouteRuleElement>(j, "RouteRule");
        x.route_table_check = get_stack_optional<RouteTableCheck>(j, "RouteTableCheck");
        x.routing_health_error_response = get_stack_optional<RoutingHealthErrorResponse>(j, "RoutingHealthErrorResponse");
        x.routing_health_response = get_stack_optional<RoutingHealthResponse>(j, "RoutingHealthResponse");
    }

    inline void to_json(json & j, const KeenPbr3TypesXLs8I0 & x) {
        j = json::object();
        j["ApiConfig"] = x.api_config;
        j["CacheMetadata"] = x.cache_metadata;
        j["CheckStatus"] = x.check_status;
        j["CircuitBreakerConfig"] = x.circuit_breaker_config;
        j["ConfigObject"] = x.config_object;
        j["ConfigUpdateResponse"] = x.config_update_response;
        j["DaemonConfig"] = x.daemon_config;
        j["DnsConfig"] = x.dns_config;
        j["DnsRule"] = x.dns_rule;
        j["DnsServer"] = x.dns_server;
        j["ErrorResponse"] = x.error_response;
        j["FirewallChain"] = x.firewall_chain;
        j["FirewallRuleCheck"] = x.firewall_rule_check;
        j["FwmarkConfig"] = x.fwmark_config;
        j["HealthChild"] = x.health_child;
        j["HealthEntry"] = x.health_entry;
        j["HealthResponse"] = x.health_response;
        j["IprouteConfig"] = x.iproute_config;
        j["ListConfig"] = x.list_config;
        j["ListsAutoupdateConfig"] = x.lists_autoupdate_config;
        j["Outbound"] = x.outbound;
        j["OutboundGroup"] = x.outbound_group;
        j["PolicyRuleCheck"] = x.policy_rule_check;
        j["ReloadResponse"] = x.reload_response;
        j["RetryConfig"] = x.retry_config;
        j["RouteConfig"] = x.route_config;
        j["RouteRule"] = x.route_rule;
        j["RouteTableCheck"] = x.route_table_check;
        j["RoutingHealthErrorResponse"] = x.routing_health_error_response;
        j["RoutingHealthResponse"] = x.routing_health_response;
    }

    inline void from_json(const json & j, CheckStatus & x) {
        if (j == "mismatch") x = CheckStatus::MISMATCH;
        else if (j == "missing") x = CheckStatus::MISSING;
        else if (j == "ok") x = CheckStatus::OK;
        else { throw std::runtime_error("Input JSON does not conform to schema!"); }
    }

    inline void to_json(json & j, const CheckStatus & x) {
        switch (x) {
            case CheckStatus::MISMATCH: j = "mismatch"; break;
            case CheckStatus::MISSING: j = "missing"; break;
            case CheckStatus::OK: j = "ok"; break;
            default: throw std::runtime_error("Unexpected value in enumeration \"CheckStatus\": " + std::to_string(static_cast<int>(x)));
        }
    }

    inline void from_json(const json & j, Type & x) {
        if (j == "blackhole") x = Type::BLACKHOLE;
        else if (j == "ignore") x = Type::IGNORE;
        else if (j == "interface") x = Type::INTERFACE;
        else if (j == "table") x = Type::TABLE;
        else if (j == "urltest") x = Type::URLTEST;
        else { throw std::runtime_error("Input JSON does not conform to schema!"); }
    }

    inline void to_json(json & j, const Type & x) {
        switch (x) {
            case Type::BLACKHOLE: j = "blackhole"; break;
            case Type::IGNORE: j = "ignore"; break;
            case Type::INTERFACE: j = "interface"; break;
            case Type::TABLE: j = "table"; break;
            case Type::URLTEST: j = "urltest"; break;
            default: throw std::runtime_error("Unexpected value in enumeration \"Type\": " + std::to_string(static_cast<int>(x)));
        }
    }

    inline void from_json(const json & j, ConfigUpdateResponseStatus & x) {
        if (j == "ok") x = ConfigUpdateResponseStatus::OK;
        else { throw std::runtime_error("Input JSON does not conform to schema!"); }
    }

    inline void to_json(json & j, const ConfigUpdateResponseStatus & x) {
        switch (x) {
            case ConfigUpdateResponseStatus::OK: j = "ok"; break;
            default: throw std::runtime_error("Unexpected value in enumeration \"ConfigUpdateResponseStatus\": " + std::to_string(static_cast<int>(x)));
        }
    }

    inline void from_json(const json & j, CircuitBreaker & x) {
        if (j == "closed") x = CircuitBreaker::CLOSED;
        else if (j == "half_open") x = CircuitBreaker::HALF_OPEN;
        else if (j == "open") x = CircuitBreaker::OPEN;
        else { throw std::runtime_error("Input JSON does not conform to schema!"); }
    }

    inline void to_json(json & j, const CircuitBreaker & x) {
        switch (x) {
            case CircuitBreaker::CLOSED: j = "closed"; break;
            case CircuitBreaker::HALF_OPEN: j = "half_open"; break;
            case CircuitBreaker::OPEN: j = "open"; break;
            default: throw std::runtime_error("Unexpected value in enumeration \"CircuitBreaker\": " + std::to_string(static_cast<int>(x)));
        }
    }

    inline void from_json(const json & j, HealthEntryStatus & x) {
        if (j == "degraded") x = HealthEntryStatus::DEGRADED;
        else if (j == "healthy") x = HealthEntryStatus::HEALTHY;
        else if (j == "unknown") x = HealthEntryStatus::UNKNOWN;
        else { throw std::runtime_error("Input JSON does not conform to schema!"); }
    }

    inline void to_json(json & j, const HealthEntryStatus & x) {
        switch (x) {
            case HealthEntryStatus::DEGRADED: j = "degraded"; break;
            case HealthEntryStatus::HEALTHY: j = "healthy"; break;
            case HealthEntryStatus::UNKNOWN: j = "unknown"; break;
            default: throw std::runtime_error("Unexpected value in enumeration \"HealthEntryStatus\": " + std::to_string(static_cast<int>(x)));
        }
    }

    inline void from_json(const json & j, HealthResponseStatus & x) {
        if (j == "running") x = HealthResponseStatus::RUNNING;
        else { throw std::runtime_error("Input JSON does not conform to schema!"); }
    }

    inline void to_json(json & j, const HealthResponseStatus & x) {
        switch (x) {
            case HealthResponseStatus::RUNNING: j = "running"; break;
            default: throw std::runtime_error("Unexpected value in enumeration \"HealthResponseStatus\": " + std::to_string(static_cast<int>(x)));
        }
    }

    inline void from_json(const json & j, RoutingHealthErrorResponseOverall & x) {
        if (j == "error") x = RoutingHealthErrorResponseOverall::ERROR;
        else { throw std::runtime_error("Input JSON does not conform to schema!"); }
    }

    inline void to_json(json & j, const RoutingHealthErrorResponseOverall & x) {
        switch (x) {
            case RoutingHealthErrorResponseOverall::ERROR: j = "error"; break;
            default: throw std::runtime_error("Unexpected value in enumeration \"RoutingHealthErrorResponseOverall\": " + std::to_string(static_cast<int>(x)));
        }
    }

    inline void from_json(const json & j, FirewallBackend & x) {
        if (j == "iptables") x = FirewallBackend::IPTABLES;
        else if (j == "nftables") x = FirewallBackend::NFTABLES;
        else { throw std::runtime_error("Input JSON does not conform to schema!"); }
    }

    inline void to_json(json & j, const FirewallBackend & x) {
        switch (x) {
            case FirewallBackend::IPTABLES: j = "iptables"; break;
            case FirewallBackend::NFTABLES: j = "nftables"; break;
            default: throw std::runtime_error("Unexpected value in enumeration \"FirewallBackend\": " + std::to_string(static_cast<int>(x)));
        }
    }

    inline void from_json(const json & j, RoutingHealthResponseOverall & x) {
        if (j == "degraded") x = RoutingHealthResponseOverall::DEGRADED;
        else if (j == "error") x = RoutingHealthResponseOverall::ERROR;
        else if (j == "ok") x = RoutingHealthResponseOverall::OK;
        else { throw std::runtime_error("Input JSON does not conform to schema!"); }
    }

    inline void to_json(json & j, const RoutingHealthResponseOverall & x) {
        switch (x) {
            case RoutingHealthResponseOverall::DEGRADED: j = "degraded"; break;
            case RoutingHealthResponseOverall::ERROR: j = "error"; break;
            case RoutingHealthResponseOverall::OK: j = "ok"; break;
            default: throw std::runtime_error("Unexpected value in enumeration \"RoutingHealthResponseOverall\": " + std::to_string(static_cast<int>(x)));
        }
    }
}
}
