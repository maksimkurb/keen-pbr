// Generated from docs/openapi.yaml via build_scripts/generate_api_types.sh
// Run "make generate" to regenerate (requires Node.js).

//  To parse this JSON data, first install
//
//      json.hpp  https://github.com/nlohmann/json
//
//  Then include this file, and then do
//
//     KeenPbrTypesHwuqQh data = nlohmann::json::parse(jsonString);

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
            if (j.is_null()) return std::shared_ptr<T>(); else return std::make_shared<T>(j.get<T>());
        }
    };
    template <typename T>
    struct adl_serializer<std::optional<T>> {
        static void to_json(json & j, const std::optional<T> & opt) {
            if (!opt) j = nullptr; else j = *opt;
        }

        static std::optional<T> from_json(const json & j) {
            if (j.is_null()) return std::optional<T>(); else return std::make_optional<T>(j.get<T>());
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

    struct AuthenticationConfigClass {
        std::optional<bool> enabled;
        std::optional<std::string> password_hash;
    };

    struct CorsConfigClass {
        std::optional<std::vector<std::string>> allowed_origins;
    };

    struct ApiConfig {
        std::optional<AuthenticationConfigClass> authentication;
        std::optional<CorsConfigClass> cors;
        std::optional<bool> enabled;
        std::optional<int64_t> keep_alive_timeout_seconds;
        std::optional<std::string> listen;
        std::optional<int64_t> max_request_body_bytes;
        std::optional<int64_t> read_timeout_seconds;
        std::optional<int64_t> write_timeout_seconds;
    };

    struct AuthLoginRequest {
        std::string password;
    };

    struct AuthLoginResponse {
        int64_t expires_at;
        std::string token;
    };

    struct AuthPasswordRequest {
        std::string password;
    };

    struct AuthPasswordStatus {
        bool password_set;
    };

    struct AuthSettingsRequestAuthentication {
        bool enabled;
    };

    struct AuthSettingsRequestCors {
        std::vector<std::string> allowed_origins;
    };

    struct AuthSettingsRequest {
        AuthSettingsRequestAuthentication authentication;
        AuthSettingsRequestCors cors;
        std::optional<std::string> password;
    };

    struct AuthSettingsResponse {
        AuthenticationConfigClass authentication;
        CorsConfigClass cors;
        bool password_set;
    };

    struct AuthStatusResponse {
        bool authenticated;
        std::string device_name;
        bool enabled;
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

    enum class DaemonConfigFirewallBackend : int { AUTO, IPTABLES, NFTABLES };

    enum class StrictEnforcementAction : int { BLACKHOLE, UNREACHABLE };

    struct Daemon {
        std::optional<std::string> cache_dir;
        std::optional<bool> clear_dynamic_sets_on_apply;
        std::optional<int64_t> exec_kill_grace_seconds;
        std::optional<int64_t> exec_timeout_seconds;
        std::optional<DaemonConfigFirewallBackend> firewall_backend;
        std::optional<int64_t> firewall_verify_max_bytes;
        std::optional<int64_t> ipset_hashsize;
        std::optional<int64_t> ipset_maxelem;
        std::optional<bool> ipv6_enabled;
        std::optional<int64_t> max_file_size_bytes;
        std::optional<std::string> pid_file;
        std::optional<int64_t> resolver_ready_timeout_seconds;
        std::optional<bool> reuse_static_sets_on_runtime_refresh;
        std::optional<bool> skip_marked_packets;
        std::optional<bool> strict_enforcement;
        std::optional<StrictEnforcementAction> strict_enforcement_action;
    };

    struct DnsTestServer {
        std::optional<std::string> answer_ipv4;
        std::string listen;
    };

    enum class ResolverIntegration : int { DNSMASQ, NONE };

    struct DnsRuleElement {
        std::optional<bool> allow_domain_rebinding;
        std::optional<bool> enabled;
        std::vector<std::string> list;
        std::string server;
    };

    enum class DnsServerType : int { KEENETIC, STATIC };

    struct DnsServerElement {
        std::optional<std::string> address;
        std::optional<std::string> detour;
        std::string tag;
        std::optional<DnsServerType> type;
    };

    struct SystemResolver {
        std::string address;
    };

    struct DnsConfigClass {
        std::optional<DnsTestServer> dns_test_server;
        std::optional<std::vector<std::string>> fallback;
        std::optional<ResolverIntegration> resolver_integration;
        std::optional<std::vector<DnsRuleElement>> rules;
        std::optional<std::vector<DnsServerElement>> servers;
        std::optional<SystemResolver> system_resolver;
    };

    struct Fwmark {
        std::optional<std::string> mask;
        std::optional<std::string> start;
    };

    struct Marker {
        std::optional<std::string> answer_ipv4;
        std::optional<std::string> domain;
    };

    struct InterceptDnsConfigClass {
        std::optional<bool> enabled;
        std::optional<int64_t> hold_timeout_ms;
        std::optional<Marker> marker;
        std::optional<int64_t> queue_num;
    };

    struct L7 {
        std::optional<bool> enabled;
        std::optional<bool> http;
        std::optional<int64_t> nflog_group;
        std::optional<bool> quic;
        std::optional<bool> tls;
    };

    struct InterceptConfigClass {
        std::optional<InterceptDnsConfigClass> dns;
        std::optional<bool> enabled;
        std::optional<L7> l7;
        std::optional<int64_t> max_ttl_s;
        std::optional<int64_t> min_ttl_s;
    };

    struct Iproute {
        std::optional<bool> process_router_traffic;
        std::optional<int64_t> rule_priority_start;
        std::optional<int64_t> table_start;
    };

    struct ListConfigValue {
        std::optional<std::string> detour;
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

    enum class ConntrackOnSwitch : int { DELETE, PRESERVE };

    struct IcmpCandidateElement {
        std::string outbound;
        std::string target;
    };

    struct OutboundGroupMemberElement {
        std::string outbound;
        std::optional<std::string> target;
        std::optional<int64_t> weight;
    };

    struct OutboundGroupElement {
        std::optional<std::vector<IcmpCandidateElement>> candidates;
        std::optional<std::vector<OutboundGroupMemberElement>> members;
        std::optional<std::vector<std::string>> outbounds;
        std::optional<int64_t> weight;
    };

    struct Retry {
        std::optional<int64_t> attempts;
        std::optional<int64_t> interval_ms;
    };

    enum class Strategy : int { BALANCE, PRIORITY };

    enum class OutboundType : int { BLACKHOLE, ICMPTEST, IGNORE, INTERFACE, TABLE, URLTEST };

    struct OutboundElement {
        std::optional<CircuitBreakerConfig> circuit_breaker;
        std::optional<ConntrackOnSwitch> conntrack_on_switch;
        std::optional<int64_t> count;
        std::optional<std::string> gateway;
        std::optional<std::string> gateway6;
        std::optional<std::string> interface;
        std::optional<int64_t> interval_ms;
        std::optional<int64_t> max_failed;
        std::optional<int64_t> max_rtt_ms;
        std::optional<std::vector<OutboundGroupElement>> outbound_groups;
        std::optional<int64_t> packet_interval_ms;
        std::optional<int64_t> probe_timeout_ms;
        std::optional<Retry> retry;
        std::optional<Strategy> strategy;
        std::optional<bool> strict_enforcement;
        std::optional<StrictEnforcementAction> strict_enforcement_action;
        std::optional<int64_t> table;
        std::string tag;
        std::optional<int64_t> tolerance_ms;
        OutboundType type;
        std::optional<std::string> url;
    };

    enum class DefaultGateway : int { IPV4, IPV6 };

    struct RouteRuleElement {
        std::optional<DefaultGateway> default_gateway;
        std::optional<std::string> dest_addr;
        std::optional<std::string> dest_port;
        std::optional<int64_t> dscp;
        std::optional<bool> enabled;
        std::optional<std::vector<std::string>> list;
        std::string outbound;
        std::optional<std::string> proto;
        std::optional<std::string> src_addr;
        std::optional<std::string> src_port;
    };

    struct Route {
        std::optional<std::vector<std::string>> inbound_interfaces;
        std::optional<std::vector<RouteRuleElement>> rules;
    };

    struct ConfigObject {
        std::optional<ApiConfig> api;
        std::optional<Daemon> daemon;
        std::optional<std::string> device_name;
        std::optional<DnsConfigClass> dns;
        std::optional<Fwmark> fwmark;
        std::optional<InterceptConfigClass> intercept;
        std::optional<Iproute> iproute;
        std::optional<std::map<std::string, ListConfigValue>> lists;
        std::optional<ListsAutoupdate> lists_autoupdate;
        std::optional<std::vector<OutboundElement>> outbounds;
        std::optional<Route> route;
    };

    struct Config {
        std::optional<Daemon> daemon;
        std::optional<std::string> device_name;
        std::optional<DnsConfigClass> dns;
        std::optional<Fwmark> fwmark;
        std::optional<InterceptConfigClass> intercept;
        std::optional<Iproute> iproute;
        std::optional<std::map<std::string, ListConfigValue>> lists;
        std::optional<ListsAutoupdate> lists_autoupdate;
        std::optional<std::vector<OutboundElement>> outbounds;
        std::optional<Route> route;
    };

    struct ListRefreshStateValue {
        std::optional<std::string> last_updated;
    };

    struct ConfigStateResponse {
        Config config;
        bool is_draft;
        std::optional<std::map<std::string, ListRefreshStateValue>> list_refresh_state;
    };

    enum class ConfigUpdateResponseStatus : int { OK };

    struct ConfigUpdateResponse {
        std::optional<int64_t> apply_started_ts;
        std::string message;
        ConfigUpdateResponseStatus status;
    };

    enum class DnsTestGapEventType : int { GAP };

    struct DnsTestGapEvent {
        int64_t from_seq;
        int64_t to_seq;
        DnsTestGapEventType type;
    };

    enum class Source : int { DNS, HTTP, MARKER, QUIC, SNI };

    enum class TimeoutCause : int { ADMISSION_BLOCKED, BUDGET_SPENT_BY_BATCH, LATE_BATCH_FULL, OTHER, OWN_WRITE_SLOW };

    enum class DnsTestInterceptEventType : int { INTERCEPT };

    struct DnsTestInterceptEvent {
        int64_t added;
        std::optional<int64_t> admission_wait_us;
        std::optional<int64_t> batch_pos;
        std::optional<int64_t> batch_size;
        std::optional<int64_t> budget_left_us;
        std::optional<int64_t> cache_hits;
        std::optional<std::string> client_ip;
        std::optional<int64_t> deferred_refresh;
        std::string domain;
        int64_t errors;
        int64_t hold_us;
        std::vector<std::string> ips;
        std::optional<int64_t> late_batch_elements;
        std::optional<bool> late_write;
        std::vector<std::string> lists;
        std::optional<int64_t> not_learned;
        std::optional<int64_t> parse_us;
        std::optional<int64_t> qtype;
        std::optional<int64_t> queue_wait_us;
        std::optional<int64_t> rcode;
        int64_t refreshed;
        int64_t seq;
        std::optional<int64_t> set_write_us;
        Source source;
        bool timed_out;
        std::optional<TimeoutCause> timeout_cause;
        int64_t ts_ms;
        DnsTestInterceptEventType type;
        std::optional<int64_t> write_elements;
        std::optional<int64_t> write_errno;
    };

    enum class DnsmasqAlive : int { ALIVE, DEAD, UNKNOWN };

    enum class ProbeStatus : int { INVALID, MISSING, NOT_CHECKED, OK, QUERY_FAILED };

    enum class State : int { APPLYING, DISABLED, ERROR, OK, RECONCILING };

    struct DnsmasqHealth {
        std::optional<std::string> config_hash;
        std::optional<DnsmasqAlive> dnsmasq_alive;
        int64_t domains;
        std::optional<int64_t> last_apply_ts;
        std::optional<int64_t> last_check_ts;
        std::optional<std::string> last_error;
        std::optional<int64_t> last_external_reload_ts;
        std::optional<int64_t> loaded_boottime_ms;
        std::optional<std::string> loaded_hash;
        std::optional<int64_t> loaded_ts;
        ResolverIntegration mode;
        std::optional<int64_t> next_repair_ts;
        std::optional<ProbeStatus> probe_status;
        std::optional<int64_t> repair_attempt;
        std::optional<int64_t> repair_max_attempts;
        std::optional<bool> repair_paused;
        std::optional<std::string> repair_reason;
        int64_t rules;
        State state;
    };

    struct ValidationErrorElement {
        std::string message;
        std::optional<std::string> path;
    };

    struct ErrorResponse {
        std::string error;
        std::optional<std::vector<ValidationErrorElement>> validation_errors;
    };

    enum class VerificationState : int { FAILED, UNAVAILABLE, VERIFIED };

    struct FirewallChain {
        bool chain_present;
        std::optional<std::string> detail;
        bool prerouting_hook_present;
        VerificationState verification_state;
    };

    struct FirewallRuleCheck {
        std::string action;
        std::optional<std::string> actual_fwmark;
        std::optional<std::string> detail;
        std::optional<std::string> expected_fwmark;
        std::string set_name;
        CheckStatus status;
    };

    enum class PayloadReplacement : int { SUPPORTED, UNKNOWN, UNSUPPORTED };

    struct Capabilities {
        bool addrtype;
        bool connbytes;
        std::optional<bool> conntrack_cleanup;
        std::optional<bool> fail_open;
        bool nflog;
        bool nfqueue;
        std::optional<PayloadReplacement> payload_replacement;
    };

    struct DnsWriteLatency {
        std::optional<int64_t> ge_100_ms;
        std::optional<int64_t> lt_100_ms;
        std::optional<int64_t> lt_10_ms;
        std::optional<int64_t> lt_1_ms;
        std::optional<int64_t> lt_30_ms;
        std::optional<int64_t> lt_5_ms;
        std::optional<int64_t> max_elements;
        std::optional<int64_t> max_us;
    };

    struct Counters {
        std::optional<int64_t> conntrack_deleted;
        std::optional<int64_t> conntrack_errors;
        std::optional<int64_t> conntrack_requests;
        std::optional<int64_t> dns_aaaa_ignored;
        std::optional<int64_t> dns_hold_timeouts;
        std::optional<int64_t> dns_late_write_errors;
        std::optional<int64_t> dns_late_writes;
        std::optional<int64_t> dns_matched;
        std::optional<int64_t> dns_packets;
        std::optional<int64_t> dns_parse_errors;
        std::optional<int64_t> dns_refresh_deferred;
        std::optional<int64_t> dns_tcp_partial;
        std::optional<int64_t> dns_timeout_admission_blocked;
        std::optional<int64_t> dns_timeout_budget_spent_by_batch;
        std::optional<int64_t> dns_timeout_late_batch_full;
        std::optional<int64_t> dns_timeout_other;
        std::optional<int64_t> dns_timeout_own_write_slow;
        std::optional<DnsWriteLatency> dns_write_latency;
        std::optional<int64_t> l7_matched;
        std::optional<int64_t> l7_packets;
        std::optional<DnsWriteLatency> l7_write_latency;
        std::optional<DnsWriteLatency> late_write_latency;
        std::optional<int64_t> log_overruns;
        std::optional<int64_t> marker_hits;
        std::optional<int64_t> queue_overruns;
        std::optional<int64_t> refresh_dropped;
        std::optional<int64_t> refresh_skipped;
        std::optional<int64_t> set_added;
        std::optional<int64_t> set_cache_entries;
        std::optional<int64_t> set_cache_hits;
        std::optional<int64_t> set_cache_misses;
        std::optional<int64_t> set_errors;
        std::optional<int64_t> set_refreshed;
        std::optional<int64_t> set_write_slow;
    };

    struct KernelQueue {
        int64_t id_sequence;
        int64_t queue_dropped;
        int64_t queue_total;
        int64_t user_dropped;
    };

    enum class InterceptProbeFeatureStatus : int { ERROR, NOT_RUN, OK, SKIPPED, UNSUPPORTED };

    struct InterceptProbeFeatureElement {
        std::string feature;
        std::optional<std::string> reason;
        InterceptProbeFeatureStatus status;
    };

    struct InterceptHealthClass {
        Capabilities capabilities;
        std::optional<Counters> counters;
        bool dns_hold_active;
        bool enabled;
        std::optional<int64_t> events_seq;
        std::optional<int64_t> ipset_protocol;
        std::optional<KernelQueue> kernel_queue;
        std::optional<std::string> kernel_release;
        bool l7_active;
        std::optional<int64_t> nflog_group;
        std::optional<std::vector<InterceptProbeFeatureElement>> probes;
        std::optional<int64_t> queue_num;
        std::vector<std::string> reasons;
        bool running;
        std::optional<std::vector<std::string>> warnings;
    };

    enum class LifecycleOperationStageStatus : int { FAILED, PENDING, RUNNING, SKIPPED, SUCCEEDED };

    struct LifecycleOperationStageElement {
        std::optional<std::string> detail;
        std::string id;
        LifecycleOperationStageStatus status;
        std::string title;
    };

    enum class LifecycleOperationStatus : int { FAILED, RUNNING, SUCCEEDED };

    enum class LifecycleOperationType : int { APPLY_CONFIG, RESTART, ROLLBACK_CONFIG, START, STOP };

    struct LifecycleOperation {
        std::optional<std::string> error;
        std::optional<int64_t> finished_at;
        std::string id;
        std::vector<LifecycleOperationStageElement> stages;
        int64_t started_at;
        LifecycleOperationStatus status;
        LifecycleOperationType type;
    };

    enum class RuntimeState : int { APPLYING, BROKEN, RESTART_REQUIRED, RUNNING, SHUTTING_DOWN, STARTING, STOPPED };

    enum class HealthResponseStatus : int { DEGRADED, RUNNING, STOPPED };

    struct HealthResponse {
        std::optional<int64_t> apply_started_ts;
        std::string build;
        std::string build_variant;
        bool config_is_draft;
        std::optional<DnsmasqHealth> dnsmasq;
        std::optional<InterceptHealthClass> intercept;
        std::optional<LifecycleOperation> lifecycle_operation;
        std::string os_type;
        std::string os_version;
        bool rollback_available;
        std::optional<RuntimeState> runtime_state;
        std::optional<std::string> runtime_state_reason;
        HealthResponseStatus status;
        std::string version;
    };

    enum class LifecycleOperationAcceptedResponseStatus : int { ACCEPTED };

    struct LifecycleOperationAcceptedResponse {
        std::string operation_id;
        LifecycleOperationAcceptedResponseStatus status;
    };

    struct ListRefreshRequest {
        std::optional<std::string> name;
    };

    struct ListRefreshResponse {
        std::vector<std::string> changed_lists;
        std::vector<std::string> failed_lists;
        std::string message;
        std::vector<std::string> refreshed_lists;
        bool reloaded;
        ConfigUpdateResponseStatus status;
    };

    enum class ExpectedAction : int { BLACKHOLE, LOOKUP, UNREACHABLE };

    struct PolicyRuleCheck {
        std::optional<std::string> detail;
        std::optional<ExpectedAction> expected_action;
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
        std::optional<std::string> expected_destination;
        std::optional<std::string> expected_gateway;
        std::optional<std::string> expected_interface;
        std::optional<int64_t> expected_metric;
        std::optional<std::string> expected_route_type;
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

    enum class RoutingHealthResponseFirewallBackend : int { IPTABLES, NFTABLES };

    enum class RoutingHealthResponseOverall : int { DEGRADED, ERROR, OK };

    struct RoutingHealthResponse {
        FirewallChain firewall;
        RoutingHealthResponseFirewallBackend firewall_backend;
        std::vector<FirewallRuleCheck> firewall_rules;
        RoutingHealthResponseOverall overall;
        std::vector<PolicyRuleCheck> policy_rules;
        std::vector<RouteTableCheck> route_tables;
    };

    struct ListMatch {
        std::string list;
        std::string via;
    };

    struct RoutingTestEntry {
        std::string actual_outbound;
        std::optional<bool> criteria_match;
        std::string expected_outbound;
        std::string ip;
        std::optional<ListMatch> list_match;
        std::optional<int64_t> matched_rule_index;
        bool ok;
    };

    enum class Proto : int { OTHER, TCP, UDP };

    struct RoutingTestRequest {
        std::optional<int64_t> dest_port;
        std::optional<int64_t> dscp;
        std::optional<Proto> proto;
        std::optional<std::string> src_addr;
        std::optional<int64_t> src_port;
        std::string target;
    };

    enum class RoutingTestSetWriteEvidenceStatus : int { NOT_TRACKED, NO_RECORD, RECORDED };

    struct SetWriteEvidence {
        std::optional<int64_t> age_seconds;
        RoutingTestSetWriteEvidenceStatus status;
    };

    struct RoutingTestRuleIpDiagnosticElement {
        std::optional<bool> criteria_match;
        std::optional<bool> in_ipset;
        bool in_lists;
        std::string ip;
        std::optional<ListMatch> list_match;
        std::optional<SetWriteEvidence> set_write_evidence;
    };

    struct RoutingTestRuleDiagnosticElement {
        std::string interface_name;
        std::vector<RoutingTestRuleIpDiagnosticElement> ip_rows;
        std::string outbound;
        RouteRuleElement rule;
        int64_t rule_index;
        bool target_in_lists;
        std::optional<ListMatch> target_match;
    };

    struct RoutingTestResponse {
        std::optional<std::string> dns_error;
        bool is_domain;
        bool no_matching_rule;
        std::vector<std::string> resolved_ips;
        std::vector<RoutingTestEntry> results;
        std::vector<RoutingTestRuleDiagnosticElement> rule_diagnostics;
        std::string target;
        std::vector<std::string> warnings;
    };

    enum class RuntimeInterfaceInventoryStatusEnum : int { DOWN, UP };

    struct RuntimeInterfaceInventoryEntry {
        std::optional<bool> admin_up;
        std::optional<bool> carrier;
        std::optional<std::string> description;
        std::optional<std::vector<std::string>> ipv4_addresses;
        std::optional<std::vector<std::string>> ipv6_addresses;
        std::string name;
        std::optional<std::string> oper_state;
        RuntimeInterfaceInventoryStatusEnum status;
    };

    struct RuntimeInterfaceInventoryResponse {
        std::vector<RuntimeInterfaceInventoryEntry> interfaces;
    };

    enum class RuntimeInterfaceStatusEnum : int { ACTIVE, BACKUP, DEGRADED, UNAVAILABLE, UNKNOWN };

    struct RuntimeInterfaceState {
        std::optional<std::string> detail;
        std::optional<std::string> interface_name;
        std::optional<int64_t> latency_ms;
        std::string outbound_tag;
        std::optional<int64_t> packets_attempted;
        std::optional<int64_t> packets_failed;
        std::optional<int64_t> packets_received;
        std::optional<int64_t> packets_sent;
        std::optional<std::string> probe_target;
        RuntimeInterfaceStatusEnum status;
    };

    enum class RuntimeOutboundStatusEnum : int { DEGRADED, HEALTHY, UNAVAILABLE, UNKNOWN };

    struct RuntimeOutboundStateElement {
        std::optional<std::string> detail;
        std::vector<RuntimeInterfaceState> interfaces;
        RuntimeOutboundStatusEnum status;
        std::string tag;
        OutboundType type;
    };

    struct RuntimeOutboundsResponse {
        std::vector<RuntimeOutboundStateElement> outbounds;
    };

    enum class StatusEventInterfacesType : int { INTERFACES };

    struct StatusEventInterfaces {
        RuntimeInterfaceInventoryResponse data;
        StatusEventInterfacesType type;
    };

    enum class StatusEventOutboundsType : int { OUTBOUNDS };

    struct StatusEventOutbounds {
        RuntimeOutboundsResponse data;
        StatusEventOutboundsType type;
    };

    enum class StatusEventServiceType : int { SERVICE };

    struct StatusEventService {
        HealthResponse data;
        StatusEventServiceType type;
    };

    struct Data {
        RuntimeInterfaceInventoryResponse interfaces;
        RuntimeOutboundsResponse outbounds;
        HealthResponse service;
    };

    enum class StatusEventSnapshotType : int { SNAPSHOT };

    struct StatusEventSnapshot {
        Data data;
        StatusEventSnapshotType type;
    };

    struct KeenPbrTypesHwuqQh {
        std::optional<ApiConfig> api_config;
        std::optional<AuthenticationConfigClass> authentication_config;
        std::optional<AuthLoginRequest> auth_login_request;
        std::optional<AuthLoginResponse> auth_login_response;
        std::optional<AuthPasswordRequest> auth_password_request;
        std::optional<AuthPasswordStatus> auth_password_status;
        std::optional<AuthSettingsRequest> auth_settings_request;
        std::optional<AuthSettingsResponse> auth_settings_response;
        std::optional<AuthStatusResponse> auth_status_response;
        std::optional<CacheMetadata> cache_metadata;
        std::optional<CheckStatus> check_status;
        std::optional<CircuitBreakerConfig> circuit_breaker_config;
        std::optional<ConfigObject> config_object;
        std::optional<ConfigStateResponse> config_state_response;
        std::optional<ConfigUpdateResponse> config_update_response;
        std::optional<ConntrackOnSwitch> conntrack_on_switch;
        std::optional<CorsConfigClass> cors_config;
        std::optional<Daemon> daemon_config;
        std::optional<DefaultGateway> default_gateway;
        std::optional<DnsConfigClass> dns_config;
        std::optional<DnsmasqHealth> dnsmasq_health;
        std::optional<DnsRuleElement> dns_rule;
        std::optional<DnsServerElement> dns_server;
        std::optional<SystemResolver> dns_system_resolver;
        std::optional<DnsTestGapEvent> dns_test_gap_event;
        std::optional<DnsTestInterceptEvent> dns_test_intercept_event;
        std::optional<DnsTestServer> dns_test_server;
        std::optional<Config> draft_config;
        std::optional<ErrorResponse> error_response;
        std::optional<FirewallChain> firewall_chain;
        std::optional<FirewallRuleCheck> firewall_rule_check;
        std::optional<Fwmark> fwmark_config;
        std::optional<HealthResponse> health_response;
        std::optional<IcmpCandidateElement> icmp_candidate;
        std::optional<Capabilities> intercept_capabilities_health;
        std::optional<InterceptConfigClass> intercept_config;
        std::optional<Counters> intercept_counters_health;
        std::optional<InterceptDnsConfigClass> intercept_dns_config;
        std::optional<InterceptHealthClass> intercept_health;
        std::optional<KernelQueue> intercept_kernel_queue;
        std::optional<L7> intercept_l7_config;
        std::optional<Marker> intercept_marker_config;
        std::optional<InterceptProbeFeatureElement> intercept_probe_feature;
        std::optional<DnsWriteLatency> intercept_write_latency;
        std::optional<Iproute> iproute_config;
        std::optional<LifecycleOperation> lifecycle_operation;
        std::optional<LifecycleOperationAcceptedResponse> lifecycle_operation_accepted_response;
        std::optional<LifecycleOperationStageElement> lifecycle_operation_stage;
        std::optional<ListConfigValue> list_config;
        std::optional<ListRefreshRequest> list_refresh_request;
        std::optional<ListRefreshResponse> list_refresh_response;
        std::optional<ListRefreshStateValue> list_refresh_state;
        std::optional<ListsAutoupdate> lists_autoupdate_config;
        std::optional<OutboundElement> outbound;
        std::optional<OutboundGroupElement> outbound_group;
        std::optional<OutboundGroupMemberElement> outbound_group_member;
        std::optional<PolicyRuleCheck> policy_rule_check;
        std::optional<ReloadResponse> reload_response;
        std::optional<ResolverIntegration> resolver_integration_mode;
        std::optional<Retry> retry_config;
        std::optional<Route> route_config;
        std::optional<RouteRuleElement> route_rule;
        std::optional<RouteTableCheck> route_table_check;
        std::optional<RoutingHealthErrorResponse> routing_health_error_response;
        std::optional<RoutingHealthResponse> routing_health_response;
        std::optional<RoutingTestEntry> routing_test_entry;
        std::optional<ListMatch> routing_test_list_match;
        std::optional<RoutingTestRequest> routing_test_request;
        std::optional<RoutingTestResponse> routing_test_response;
        std::optional<RoutingTestRuleDiagnosticElement> routing_test_rule_diagnostic;
        std::optional<RoutingTestRuleIpDiagnosticElement> routing_test_rule_ip_diagnostic;
        std::optional<SetWriteEvidence> routing_test_set_write_evidence;
        std::optional<RuntimeInterfaceInventoryEntry> runtime_interface_inventory_entry;
        std::optional<RuntimeInterfaceInventoryResponse> runtime_interface_inventory_response;
        std::optional<RuntimeInterfaceInventoryStatusEnum> runtime_interface_inventory_status;
        std::optional<RuntimeInterfaceState> runtime_interface_state;
        std::optional<RuntimeInterfaceStatusEnum> runtime_interface_status;
        std::optional<RuntimeOutboundsResponse> runtime_outbounds_response;
        std::optional<RuntimeOutboundStateElement> runtime_outbound_state;
        std::optional<RuntimeOutboundStatusEnum> runtime_outbound_status;
        std::optional<StatusEventInterfaces> status_event_interfaces;
        std::optional<StatusEventOutbounds> status_event_outbounds;
        std::optional<StatusEventService> status_event_service;
        std::optional<StatusEventSnapshot> status_event_snapshot;
        std::optional<Strategy> test_group_strategy;
        std::optional<ValidationErrorElement> validation_error;
    };
}
}

namespace keen_pbr3 {
namespace api {
    void from_json(const json & j, AuthenticationConfigClass & x);
    void to_json(json & j, const AuthenticationConfigClass & x);

    void from_json(const json & j, CorsConfigClass & x);
    void to_json(json & j, const CorsConfigClass & x);

    void from_json(const json & j, ApiConfig & x);
    void to_json(json & j, const ApiConfig & x);

    void from_json(const json & j, AuthLoginRequest & x);
    void to_json(json & j, const AuthLoginRequest & x);

    void from_json(const json & j, AuthLoginResponse & x);
    void to_json(json & j, const AuthLoginResponse & x);

    void from_json(const json & j, AuthPasswordRequest & x);
    void to_json(json & j, const AuthPasswordRequest & x);

    void from_json(const json & j, AuthPasswordStatus & x);
    void to_json(json & j, const AuthPasswordStatus & x);

    void from_json(const json & j, AuthSettingsRequestAuthentication & x);
    void to_json(json & j, const AuthSettingsRequestAuthentication & x);

    void from_json(const json & j, AuthSettingsRequestCors & x);
    void to_json(json & j, const AuthSettingsRequestCors & x);

    void from_json(const json & j, AuthSettingsRequest & x);
    void to_json(json & j, const AuthSettingsRequest & x);

    void from_json(const json & j, AuthSettingsResponse & x);
    void to_json(json & j, const AuthSettingsResponse & x);

    void from_json(const json & j, AuthStatusResponse & x);
    void to_json(json & j, const AuthStatusResponse & x);

    void from_json(const json & j, CacheMetadata & x);
    void to_json(json & j, const CacheMetadata & x);

    void from_json(const json & j, CircuitBreakerConfig & x);
    void to_json(json & j, const CircuitBreakerConfig & x);

    void from_json(const json & j, Daemon & x);
    void to_json(json & j, const Daemon & x);

    void from_json(const json & j, DnsTestServer & x);
    void to_json(json & j, const DnsTestServer & x);

    void from_json(const json & j, DnsRuleElement & x);
    void to_json(json & j, const DnsRuleElement & x);

    void from_json(const json & j, DnsServerElement & x);
    void to_json(json & j, const DnsServerElement & x);

    void from_json(const json & j, SystemResolver & x);
    void to_json(json & j, const SystemResolver & x);

    void from_json(const json & j, DnsConfigClass & x);
    void to_json(json & j, const DnsConfigClass & x);

    void from_json(const json & j, Fwmark & x);
    void to_json(json & j, const Fwmark & x);

    void from_json(const json & j, Marker & x);
    void to_json(json & j, const Marker & x);

    void from_json(const json & j, InterceptDnsConfigClass & x);
    void to_json(json & j, const InterceptDnsConfigClass & x);

    void from_json(const json & j, L7 & x);
    void to_json(json & j, const L7 & x);

    void from_json(const json & j, InterceptConfigClass & x);
    void to_json(json & j, const InterceptConfigClass & x);

    void from_json(const json & j, Iproute & x);
    void to_json(json & j, const Iproute & x);

    void from_json(const json & j, ListConfigValue & x);
    void to_json(json & j, const ListConfigValue & x);

    void from_json(const json & j, ListsAutoupdate & x);
    void to_json(json & j, const ListsAutoupdate & x);

    void from_json(const json & j, IcmpCandidateElement & x);
    void to_json(json & j, const IcmpCandidateElement & x);

    void from_json(const json & j, OutboundGroupMemberElement & x);
    void to_json(json & j, const OutboundGroupMemberElement & x);

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

    void from_json(const json & j, Config & x);
    void to_json(json & j, const Config & x);

    void from_json(const json & j, ListRefreshStateValue & x);
    void to_json(json & j, const ListRefreshStateValue & x);

    void from_json(const json & j, ConfigStateResponse & x);
    void to_json(json & j, const ConfigStateResponse & x);

    void from_json(const json & j, ConfigUpdateResponse & x);
    void to_json(json & j, const ConfigUpdateResponse & x);

    void from_json(const json & j, DnsTestGapEvent & x);
    void to_json(json & j, const DnsTestGapEvent & x);

    void from_json(const json & j, DnsTestInterceptEvent & x);
    void to_json(json & j, const DnsTestInterceptEvent & x);

    void from_json(const json & j, DnsmasqHealth & x);
    void to_json(json & j, const DnsmasqHealth & x);

    void from_json(const json & j, ValidationErrorElement & x);
    void to_json(json & j, const ValidationErrorElement & x);

    void from_json(const json & j, ErrorResponse & x);
    void to_json(json & j, const ErrorResponse & x);

    void from_json(const json & j, FirewallChain & x);
    void to_json(json & j, const FirewallChain & x);

    void from_json(const json & j, FirewallRuleCheck & x);
    void to_json(json & j, const FirewallRuleCheck & x);

    void from_json(const json & j, Capabilities & x);
    void to_json(json & j, const Capabilities & x);

    void from_json(const json & j, DnsWriteLatency & x);
    void to_json(json & j, const DnsWriteLatency & x);

    void from_json(const json & j, Counters & x);
    void to_json(json & j, const Counters & x);

    void from_json(const json & j, KernelQueue & x);
    void to_json(json & j, const KernelQueue & x);

    void from_json(const json & j, InterceptProbeFeatureElement & x);
    void to_json(json & j, const InterceptProbeFeatureElement & x);

    void from_json(const json & j, InterceptHealthClass & x);
    void to_json(json & j, const InterceptHealthClass & x);

    void from_json(const json & j, LifecycleOperationStageElement & x);
    void to_json(json & j, const LifecycleOperationStageElement & x);

    void from_json(const json & j, LifecycleOperation & x);
    void to_json(json & j, const LifecycleOperation & x);

    void from_json(const json & j, HealthResponse & x);
    void to_json(json & j, const HealthResponse & x);

    void from_json(const json & j, LifecycleOperationAcceptedResponse & x);
    void to_json(json & j, const LifecycleOperationAcceptedResponse & x);

    void from_json(const json & j, ListRefreshRequest & x);
    void to_json(json & j, const ListRefreshRequest & x);

    void from_json(const json & j, ListRefreshResponse & x);
    void to_json(json & j, const ListRefreshResponse & x);

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

    void from_json(const json & j, ListMatch & x);
    void to_json(json & j, const ListMatch & x);

    void from_json(const json & j, RoutingTestEntry & x);
    void to_json(json & j, const RoutingTestEntry & x);

    void from_json(const json & j, RoutingTestRequest & x);
    void to_json(json & j, const RoutingTestRequest & x);

    void from_json(const json & j, SetWriteEvidence & x);
    void to_json(json & j, const SetWriteEvidence & x);

    void from_json(const json & j, RoutingTestRuleIpDiagnosticElement & x);
    void to_json(json & j, const RoutingTestRuleIpDiagnosticElement & x);

    void from_json(const json & j, RoutingTestRuleDiagnosticElement & x);
    void to_json(json & j, const RoutingTestRuleDiagnosticElement & x);

    void from_json(const json & j, RoutingTestResponse & x);
    void to_json(json & j, const RoutingTestResponse & x);

    void from_json(const json & j, RuntimeInterfaceInventoryEntry & x);
    void to_json(json & j, const RuntimeInterfaceInventoryEntry & x);

    void from_json(const json & j, RuntimeInterfaceInventoryResponse & x);
    void to_json(json & j, const RuntimeInterfaceInventoryResponse & x);

    void from_json(const json & j, RuntimeInterfaceState & x);
    void to_json(json & j, const RuntimeInterfaceState & x);

    void from_json(const json & j, RuntimeOutboundStateElement & x);
    void to_json(json & j, const RuntimeOutboundStateElement & x);

    void from_json(const json & j, RuntimeOutboundsResponse & x);
    void to_json(json & j, const RuntimeOutboundsResponse & x);

    void from_json(const json & j, StatusEventInterfaces & x);
    void to_json(json & j, const StatusEventInterfaces & x);

    void from_json(const json & j, StatusEventOutbounds & x);
    void to_json(json & j, const StatusEventOutbounds & x);

    void from_json(const json & j, StatusEventService & x);
    void to_json(json & j, const StatusEventService & x);

    void from_json(const json & j, Data & x);
    void to_json(json & j, const Data & x);

    void from_json(const json & j, StatusEventSnapshot & x);
    void to_json(json & j, const StatusEventSnapshot & x);

    void from_json(const json & j, KeenPbrTypesHwuqQh & x);
    void to_json(json & j, const KeenPbrTypesHwuqQh & x);

    void from_json(const json & j, CheckStatus & x);
    void to_json(json & j, const CheckStatus & x);

    void from_json(const json & j, DaemonConfigFirewallBackend & x);
    void to_json(json & j, const DaemonConfigFirewallBackend & x);

    void from_json(const json & j, StrictEnforcementAction & x);
    void to_json(json & j, const StrictEnforcementAction & x);

    void from_json(const json & j, ResolverIntegration & x);
    void to_json(json & j, const ResolverIntegration & x);

    void from_json(const json & j, DnsServerType & x);
    void to_json(json & j, const DnsServerType & x);

    void from_json(const json & j, ConntrackOnSwitch & x);
    void to_json(json & j, const ConntrackOnSwitch & x);

    void from_json(const json & j, Strategy & x);
    void to_json(json & j, const Strategy & x);

    void from_json(const json & j, OutboundType & x);
    void to_json(json & j, const OutboundType & x);

    void from_json(const json & j, DefaultGateway & x);
    void to_json(json & j, const DefaultGateway & x);

    void from_json(const json & j, ConfigUpdateResponseStatus & x);
    void to_json(json & j, const ConfigUpdateResponseStatus & x);

    void from_json(const json & j, DnsTestGapEventType & x);
    void to_json(json & j, const DnsTestGapEventType & x);

    void from_json(const json & j, Source & x);
    void to_json(json & j, const Source & x);

    void from_json(const json & j, TimeoutCause & x);
    void to_json(json & j, const TimeoutCause & x);

    void from_json(const json & j, DnsTestInterceptEventType & x);
    void to_json(json & j, const DnsTestInterceptEventType & x);

    void from_json(const json & j, DnsmasqAlive & x);
    void to_json(json & j, const DnsmasqAlive & x);

    void from_json(const json & j, ProbeStatus & x);
    void to_json(json & j, const ProbeStatus & x);

    void from_json(const json & j, State & x);
    void to_json(json & j, const State & x);

    void from_json(const json & j, VerificationState & x);
    void to_json(json & j, const VerificationState & x);

    void from_json(const json & j, PayloadReplacement & x);
    void to_json(json & j, const PayloadReplacement & x);

    void from_json(const json & j, InterceptProbeFeatureStatus & x);
    void to_json(json & j, const InterceptProbeFeatureStatus & x);

    void from_json(const json & j, LifecycleOperationStageStatus & x);
    void to_json(json & j, const LifecycleOperationStageStatus & x);

    void from_json(const json & j, LifecycleOperationStatus & x);
    void to_json(json & j, const LifecycleOperationStatus & x);

    void from_json(const json & j, LifecycleOperationType & x);
    void to_json(json & j, const LifecycleOperationType & x);

    void from_json(const json & j, RuntimeState & x);
    void to_json(json & j, const RuntimeState & x);

    void from_json(const json & j, HealthResponseStatus & x);
    void to_json(json & j, const HealthResponseStatus & x);

    void from_json(const json & j, LifecycleOperationAcceptedResponseStatus & x);
    void to_json(json & j, const LifecycleOperationAcceptedResponseStatus & x);

    void from_json(const json & j, ExpectedAction & x);
    void to_json(json & j, const ExpectedAction & x);

    void from_json(const json & j, RoutingHealthErrorResponseOverall & x);
    void to_json(json & j, const RoutingHealthErrorResponseOverall & x);

    void from_json(const json & j, RoutingHealthResponseFirewallBackend & x);
    void to_json(json & j, const RoutingHealthResponseFirewallBackend & x);

    void from_json(const json & j, RoutingHealthResponseOverall & x);
    void to_json(json & j, const RoutingHealthResponseOverall & x);

    void from_json(const json & j, Proto & x);
    void to_json(json & j, const Proto & x);

    void from_json(const json & j, RoutingTestSetWriteEvidenceStatus & x);
    void to_json(json & j, const RoutingTestSetWriteEvidenceStatus & x);

    void from_json(const json & j, RuntimeInterfaceInventoryStatusEnum & x);
    void to_json(json & j, const RuntimeInterfaceInventoryStatusEnum & x);

    void from_json(const json & j, RuntimeInterfaceStatusEnum & x);
    void to_json(json & j, const RuntimeInterfaceStatusEnum & x);

    void from_json(const json & j, RuntimeOutboundStatusEnum & x);
    void to_json(json & j, const RuntimeOutboundStatusEnum & x);

    void from_json(const json & j, StatusEventInterfacesType & x);
    void to_json(json & j, const StatusEventInterfacesType & x);

    void from_json(const json & j, StatusEventOutboundsType & x);
    void to_json(json & j, const StatusEventOutboundsType & x);

    void from_json(const json & j, StatusEventServiceType & x);
    void to_json(json & j, const StatusEventServiceType & x);

    void from_json(const json & j, StatusEventSnapshotType & x);
    void to_json(json & j, const StatusEventSnapshotType & x);

    inline void from_json(const json & j, AuthenticationConfigClass& x) {
        x.enabled = get_stack_optional<bool>(j, "enabled");
        x.password_hash = get_stack_optional<std::string>(j, "password_hash");
    }

    inline void to_json(json & j, const AuthenticationConfigClass & x) {
        j = json::object();
        j["enabled"] = x.enabled;
        j["password_hash"] = x.password_hash;
    }

    inline void from_json(const json & j, CorsConfigClass& x) {
        x.allowed_origins = get_stack_optional<std::vector<std::string>>(j, "allowed_origins");
    }

    inline void to_json(json & j, const CorsConfigClass & x) {
        j = json::object();
        j["allowed_origins"] = x.allowed_origins;
    }

    inline void from_json(const json & j, ApiConfig& x) {
        x.authentication = get_stack_optional<AuthenticationConfigClass>(j, "authentication");
        x.cors = get_stack_optional<CorsConfigClass>(j, "cors");
        x.enabled = get_stack_optional<bool>(j, "enabled");
        x.keep_alive_timeout_seconds = get_stack_optional<int64_t>(j, "keep_alive_timeout_seconds");
        x.listen = get_stack_optional<std::string>(j, "listen");
        x.max_request_body_bytes = get_stack_optional<int64_t>(j, "max_request_body_bytes");
        x.read_timeout_seconds = get_stack_optional<int64_t>(j, "read_timeout_seconds");
        x.write_timeout_seconds = get_stack_optional<int64_t>(j, "write_timeout_seconds");
    }

    inline void to_json(json & j, const ApiConfig & x) {
        j = json::object();
        j["authentication"] = x.authentication;
        j["cors"] = x.cors;
        j["enabled"] = x.enabled;
        j["keep_alive_timeout_seconds"] = x.keep_alive_timeout_seconds;
        j["listen"] = x.listen;
        j["max_request_body_bytes"] = x.max_request_body_bytes;
        j["read_timeout_seconds"] = x.read_timeout_seconds;
        j["write_timeout_seconds"] = x.write_timeout_seconds;
    }

    inline void from_json(const json & j, AuthLoginRequest& x) {
        x.password = j.at("password").get<std::string>();
    }

    inline void to_json(json & j, const AuthLoginRequest & x) {
        j = json::object();
        j["password"] = x.password;
    }

    inline void from_json(const json & j, AuthLoginResponse& x) {
        x.expires_at = j.at("expires_at").get<int64_t>();
        x.token = j.at("token").get<std::string>();
    }

    inline void to_json(json & j, const AuthLoginResponse & x) {
        j = json::object();
        j["expires_at"] = x.expires_at;
        j["token"] = x.token;
    }

    inline void from_json(const json & j, AuthPasswordRequest& x) {
        x.password = j.at("password").get<std::string>();
    }

    inline void to_json(json & j, const AuthPasswordRequest & x) {
        j = json::object();
        j["password"] = x.password;
    }

    inline void from_json(const json & j, AuthPasswordStatus& x) {
        x.password_set = j.at("password_set").get<bool>();
    }

    inline void to_json(json & j, const AuthPasswordStatus & x) {
        j = json::object();
        j["password_set"] = x.password_set;
    }

    inline void from_json(const json & j, AuthSettingsRequestAuthentication& x) {
        x.enabled = j.at("enabled").get<bool>();
    }

    inline void to_json(json & j, const AuthSettingsRequestAuthentication & x) {
        j = json::object();
        j["enabled"] = x.enabled;
    }

    inline void from_json(const json & j, AuthSettingsRequestCors& x) {
        x.allowed_origins = j.at("allowed_origins").get<std::vector<std::string>>();
    }

    inline void to_json(json & j, const AuthSettingsRequestCors & x) {
        j = json::object();
        j["allowed_origins"] = x.allowed_origins;
    }

    inline void from_json(const json & j, AuthSettingsRequest& x) {
        x.authentication = j.at("authentication").get<AuthSettingsRequestAuthentication>();
        x.cors = j.at("cors").get<AuthSettingsRequestCors>();
        x.password = get_stack_optional<std::string>(j, "password");
    }

    inline void to_json(json & j, const AuthSettingsRequest & x) {
        j = json::object();
        j["authentication"] = x.authentication;
        j["cors"] = x.cors;
        j["password"] = x.password;
    }

    inline void from_json(const json & j, AuthSettingsResponse& x) {
        x.authentication = j.at("authentication").get<AuthenticationConfigClass>();
        x.cors = j.at("cors").get<CorsConfigClass>();
        x.password_set = j.at("password_set").get<bool>();
    }

    inline void to_json(json & j, const AuthSettingsResponse & x) {
        j = json::object();
        j["authentication"] = x.authentication;
        j["cors"] = x.cors;
        j["password_set"] = x.password_set;
    }

    inline void from_json(const json & j, AuthStatusResponse& x) {
        x.authenticated = j.at("authenticated").get<bool>();
        x.device_name = j.at("device_name").get<std::string>();
        x.enabled = j.at("enabled").get<bool>();
    }

    inline void to_json(json & j, const AuthStatusResponse & x) {
        j = json::object();
        j["authenticated"] = x.authenticated;
        j["device_name"] = x.device_name;
        j["enabled"] = x.enabled;
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
        x.clear_dynamic_sets_on_apply = get_stack_optional<bool>(j, "clear_dynamic_sets_on_apply");
        x.exec_kill_grace_seconds = get_stack_optional<int64_t>(j, "exec_kill_grace_seconds");
        x.exec_timeout_seconds = get_stack_optional<int64_t>(j, "exec_timeout_seconds");
        x.firewall_backend = get_stack_optional<DaemonConfigFirewallBackend>(j, "firewall_backend");
        x.firewall_verify_max_bytes = get_stack_optional<int64_t>(j, "firewall_verify_max_bytes");
        x.ipset_hashsize = get_stack_optional<int64_t>(j, "ipset_hashsize");
        x.ipset_maxelem = get_stack_optional<int64_t>(j, "ipset_maxelem");
        x.ipv6_enabled = get_stack_optional<bool>(j, "ipv6_enabled");
        x.max_file_size_bytes = get_stack_optional<int64_t>(j, "max_file_size_bytes");
        x.pid_file = get_stack_optional<std::string>(j, "pid_file");
        x.resolver_ready_timeout_seconds = get_stack_optional<int64_t>(j, "resolver_ready_timeout_seconds");
        x.reuse_static_sets_on_runtime_refresh = get_stack_optional<bool>(j, "reuse_static_sets_on_runtime_refresh");
        x.skip_marked_packets = get_stack_optional<bool>(j, "skip_marked_packets");
        x.strict_enforcement = get_stack_optional<bool>(j, "strict_enforcement");
        x.strict_enforcement_action = get_stack_optional<StrictEnforcementAction>(j, "strict_enforcement_action");
    }

    inline void to_json(json & j, const Daemon & x) {
        j = json::object();
        j["cache_dir"] = x.cache_dir;
        j["clear_dynamic_sets_on_apply"] = x.clear_dynamic_sets_on_apply;
        j["exec_kill_grace_seconds"] = x.exec_kill_grace_seconds;
        j["exec_timeout_seconds"] = x.exec_timeout_seconds;
        j["firewall_backend"] = x.firewall_backend;
        j["firewall_verify_max_bytes"] = x.firewall_verify_max_bytes;
        j["ipset_hashsize"] = x.ipset_hashsize;
        j["ipset_maxelem"] = x.ipset_maxelem;
        j["ipv6_enabled"] = x.ipv6_enabled;
        j["max_file_size_bytes"] = x.max_file_size_bytes;
        j["pid_file"] = x.pid_file;
        j["resolver_ready_timeout_seconds"] = x.resolver_ready_timeout_seconds;
        j["reuse_static_sets_on_runtime_refresh"] = x.reuse_static_sets_on_runtime_refresh;
        j["skip_marked_packets"] = x.skip_marked_packets;
        j["strict_enforcement"] = x.strict_enforcement;
        j["strict_enforcement_action"] = x.strict_enforcement_action;
    }

    inline void from_json(const json & j, DnsTestServer& x) {
        x.answer_ipv4 = get_stack_optional<std::string>(j, "answer_ipv4");
        x.listen = j.at("listen").get<std::string>();
    }

    inline void to_json(json & j, const DnsTestServer & x) {
        j = json::object();
        j["answer_ipv4"] = x.answer_ipv4;
        j["listen"] = x.listen;
    }

    inline void from_json(const json & j, DnsRuleElement& x) {
        x.allow_domain_rebinding = get_stack_optional<bool>(j, "allow_domain_rebinding");
        x.enabled = get_stack_optional<bool>(j, "enabled");
        x.list = j.at("list").get<std::vector<std::string>>();
        x.server = j.at("server").get<std::string>();
    }

    inline void to_json(json & j, const DnsRuleElement & x) {
        j = json::object();
        j["allow_domain_rebinding"] = x.allow_domain_rebinding;
        j["enabled"] = x.enabled;
        j["list"] = x.list;
        j["server"] = x.server;
    }

    inline void from_json(const json & j, DnsServerElement& x) {
        x.address = get_stack_optional<std::string>(j, "address");
        x.detour = get_stack_optional<std::string>(j, "detour");
        x.tag = j.at("tag").get<std::string>();
        x.type = get_stack_optional<DnsServerType>(j, "type");
    }

    inline void to_json(json & j, const DnsServerElement & x) {
        j = json::object();
        j["address"] = x.address;
        j["detour"] = x.detour;
        j["tag"] = x.tag;
        j["type"] = x.type;
    }

    inline void from_json(const json & j, SystemResolver& x) {
        x.address = j.at("address").get<std::string>();
    }

    inline void to_json(json & j, const SystemResolver & x) {
        j = json::object();
        j["address"] = x.address;
    }

    inline void from_json(const json & j, DnsConfigClass& x) {
        x.dns_test_server = get_stack_optional<DnsTestServer>(j, "dns_test_server");
        x.fallback = get_stack_optional<std::vector<std::string>>(j, "fallback");
        x.resolver_integration = get_stack_optional<ResolverIntegration>(j, "resolver_integration");
        x.rules = get_stack_optional<std::vector<DnsRuleElement>>(j, "rules");
        x.servers = get_stack_optional<std::vector<DnsServerElement>>(j, "servers");
        x.system_resolver = get_stack_optional<SystemResolver>(j, "system_resolver");
    }

    inline void to_json(json & j, const DnsConfigClass & x) {
        j = json::object();
        j["dns_test_server"] = x.dns_test_server;
        j["fallback"] = x.fallback;
        j["resolver_integration"] = x.resolver_integration;
        j["rules"] = x.rules;
        j["servers"] = x.servers;
        j["system_resolver"] = x.system_resolver;
    }

    inline void from_json(const json & j, Fwmark& x) {
        x.mask = get_stack_optional<std::string>(j, "mask");
        x.start = get_stack_optional<std::string>(j, "start");
    }

    inline void to_json(json & j, const Fwmark & x) {
        j = json::object();
        j["mask"] = x.mask;
        j["start"] = x.start;
    }

    inline void from_json(const json & j, Marker& x) {
        x.answer_ipv4 = get_stack_optional<std::string>(j, "answer_ipv4");
        x.domain = get_stack_optional<std::string>(j, "domain");
    }

    inline void to_json(json & j, const Marker & x) {
        j = json::object();
        j["answer_ipv4"] = x.answer_ipv4;
        j["domain"] = x.domain;
    }

    inline void from_json(const json & j, InterceptDnsConfigClass& x) {
        x.enabled = get_stack_optional<bool>(j, "enabled");
        x.hold_timeout_ms = get_stack_optional<int64_t>(j, "hold_timeout_ms");
        x.marker = get_stack_optional<Marker>(j, "marker");
        x.queue_num = get_stack_optional<int64_t>(j, "queue_num");
    }

    inline void to_json(json & j, const InterceptDnsConfigClass & x) {
        j = json::object();
        j["enabled"] = x.enabled;
        j["hold_timeout_ms"] = x.hold_timeout_ms;
        j["marker"] = x.marker;
        j["queue_num"] = x.queue_num;
    }

    inline void from_json(const json & j, L7& x) {
        x.enabled = get_stack_optional<bool>(j, "enabled");
        x.http = get_stack_optional<bool>(j, "http");
        x.nflog_group = get_stack_optional<int64_t>(j, "nflog_group");
        x.quic = get_stack_optional<bool>(j, "quic");
        x.tls = get_stack_optional<bool>(j, "tls");
    }

    inline void to_json(json & j, const L7 & x) {
        j = json::object();
        j["enabled"] = x.enabled;
        j["http"] = x.http;
        j["nflog_group"] = x.nflog_group;
        j["quic"] = x.quic;
        j["tls"] = x.tls;
    }

    inline void from_json(const json & j, InterceptConfigClass& x) {
        x.dns = get_stack_optional<InterceptDnsConfigClass>(j, "dns");
        x.enabled = get_stack_optional<bool>(j, "enabled");
        x.l7 = get_stack_optional<L7>(j, "l7");
        x.max_ttl_s = get_stack_optional<int64_t>(j, "max_ttl_s");
        x.min_ttl_s = get_stack_optional<int64_t>(j, "min_ttl_s");
    }

    inline void to_json(json & j, const InterceptConfigClass & x) {
        j = json::object();
        j["dns"] = x.dns;
        j["enabled"] = x.enabled;
        j["l7"] = x.l7;
        j["max_ttl_s"] = x.max_ttl_s;
        j["min_ttl_s"] = x.min_ttl_s;
    }

    inline void from_json(const json & j, Iproute& x) {
        x.process_router_traffic = get_stack_optional<bool>(j, "process_router_traffic");
        x.rule_priority_start = get_stack_optional<int64_t>(j, "rule_priority_start");
        x.table_start = get_stack_optional<int64_t>(j, "table_start");
    }

    inline void to_json(json & j, const Iproute & x) {
        j = json::object();
        j["process_router_traffic"] = x.process_router_traffic;
        j["rule_priority_start"] = x.rule_priority_start;
        j["table_start"] = x.table_start;
    }

    inline void from_json(const json & j, ListConfigValue& x) {
        x.detour = get_stack_optional<std::string>(j, "detour");
        x.domains = get_stack_optional<std::vector<std::string>>(j, "domains");
        x.file = get_stack_optional<std::string>(j, "file");
        x.ip_cidrs = get_stack_optional<std::vector<std::string>>(j, "ip_cidrs");
        x.ttl_ms = get_stack_optional<int64_t>(j, "ttl_ms");
        x.url = get_stack_optional<std::string>(j, "url");
    }

    inline void to_json(json & j, const ListConfigValue & x) {
        j = json::object();
        j["detour"] = x.detour;
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

    inline void from_json(const json & j, IcmpCandidateElement& x) {
        x.outbound = j.at("outbound").get<std::string>();
        x.target = j.at("target").get<std::string>();
    }

    inline void to_json(json & j, const IcmpCandidateElement & x) {
        j = json::object();
        j["outbound"] = x.outbound;
        j["target"] = x.target;
    }

    inline void from_json(const json & j, OutboundGroupMemberElement& x) {
        x.outbound = j.at("outbound").get<std::string>();
        x.target = get_stack_optional<std::string>(j, "target");
        x.weight = get_stack_optional<int64_t>(j, "weight");
    }

    inline void to_json(json & j, const OutboundGroupMemberElement & x) {
        j = json::object();
        j["outbound"] = x.outbound;
        j["target"] = x.target;
        j["weight"] = x.weight;
    }

    inline void from_json(const json & j, OutboundGroupElement& x) {
        x.candidates = get_stack_optional<std::vector<IcmpCandidateElement>>(j, "candidates");
        x.members = get_stack_optional<std::vector<OutboundGroupMemberElement>>(j, "members");
        x.outbounds = get_stack_optional<std::vector<std::string>>(j, "outbounds");
        x.weight = get_stack_optional<int64_t>(j, "weight");
    }

    inline void to_json(json & j, const OutboundGroupElement & x) {
        j = json::object();
        j["candidates"] = x.candidates;
        j["members"] = x.members;
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
        x.conntrack_on_switch = get_stack_optional<ConntrackOnSwitch>(j, "conntrack_on_switch");
        x.count = get_stack_optional<int64_t>(j, "count");
        x.gateway = get_stack_optional<std::string>(j, "gateway");
        x.gateway6 = get_stack_optional<std::string>(j, "gateway6");
        x.interface = get_stack_optional<std::string>(j, "interface");
        x.interval_ms = get_stack_optional<int64_t>(j, "interval_ms");
        x.max_failed = get_stack_optional<int64_t>(j, "max_failed");
        x.max_rtt_ms = get_stack_optional<int64_t>(j, "max_rtt_ms");
        x.outbound_groups = get_stack_optional<std::vector<OutboundGroupElement>>(j, "outbound_groups");
        x.packet_interval_ms = get_stack_optional<int64_t>(j, "packet_interval_ms");
        x.probe_timeout_ms = get_stack_optional<int64_t>(j, "probe_timeout_ms");
        x.retry = get_stack_optional<Retry>(j, "retry");
        x.strategy = get_stack_optional<Strategy>(j, "strategy");
        x.strict_enforcement = get_stack_optional<bool>(j, "strict_enforcement");
        x.strict_enforcement_action = get_stack_optional<StrictEnforcementAction>(j, "strict_enforcement_action");
        x.table = get_stack_optional<int64_t>(j, "table");
        x.tag = j.at("tag").get<std::string>();
        x.tolerance_ms = get_stack_optional<int64_t>(j, "tolerance_ms");
        x.type = j.at("type").get<OutboundType>();
        x.url = get_stack_optional<std::string>(j, "url");
    }

    inline void to_json(json & j, const OutboundElement & x) {
        j = json::object();
        j["circuit_breaker"] = x.circuit_breaker;
        j["conntrack_on_switch"] = x.conntrack_on_switch;
        j["count"] = x.count;
        j["gateway"] = x.gateway;
        j["gateway6"] = x.gateway6;
        j["interface"] = x.interface;
        j["interval_ms"] = x.interval_ms;
        j["max_failed"] = x.max_failed;
        j["max_rtt_ms"] = x.max_rtt_ms;
        j["outbound_groups"] = x.outbound_groups;
        j["packet_interval_ms"] = x.packet_interval_ms;
        j["probe_timeout_ms"] = x.probe_timeout_ms;
        j["retry"] = x.retry;
        j["strategy"] = x.strategy;
        j["strict_enforcement"] = x.strict_enforcement;
        j["strict_enforcement_action"] = x.strict_enforcement_action;
        j["table"] = x.table;
        j["tag"] = x.tag;
        j["tolerance_ms"] = x.tolerance_ms;
        j["type"] = x.type;
        j["url"] = x.url;
    }

    inline void from_json(const json & j, RouteRuleElement& x) {
        x.default_gateway = get_stack_optional<DefaultGateway>(j, "default_gateway");
        x.dest_addr = get_stack_optional<std::string>(j, "dest_addr");
        x.dest_port = get_stack_optional<std::string>(j, "dest_port");
        x.dscp = get_stack_optional<int64_t>(j, "dscp");
        x.enabled = get_stack_optional<bool>(j, "enabled");
        x.list = get_stack_optional<std::vector<std::string>>(j, "list");
        x.outbound = j.at("outbound").get<std::string>();
        x.proto = get_stack_optional<std::string>(j, "proto");
        x.src_addr = get_stack_optional<std::string>(j, "src_addr");
        x.src_port = get_stack_optional<std::string>(j, "src_port");
    }

    inline void to_json(json & j, const RouteRuleElement & x) {
        j = json::object();
        j["default_gateway"] = x.default_gateway;
        j["dest_addr"] = x.dest_addr;
        j["dest_port"] = x.dest_port;
        j["dscp"] = x.dscp;
        j["enabled"] = x.enabled;
        j["list"] = x.list;
        j["outbound"] = x.outbound;
        j["proto"] = x.proto;
        j["src_addr"] = x.src_addr;
        j["src_port"] = x.src_port;
    }

    inline void from_json(const json & j, Route& x) {
        x.inbound_interfaces = get_stack_optional<std::vector<std::string>>(j, "inbound_interfaces");
        x.rules = get_stack_optional<std::vector<RouteRuleElement>>(j, "rules");
    }

    inline void to_json(json & j, const Route & x) {
        j = json::object();
        j["inbound_interfaces"] = x.inbound_interfaces;
        j["rules"] = x.rules;
    }

    inline void from_json(const json & j, ConfigObject& x) {
        x.api = get_stack_optional<ApiConfig>(j, "api");
        x.daemon = get_stack_optional<Daemon>(j, "daemon");
        x.device_name = get_stack_optional<std::string>(j, "device_name");
        x.dns = get_stack_optional<DnsConfigClass>(j, "dns");
        x.fwmark = get_stack_optional<Fwmark>(j, "fwmark");
        x.intercept = get_stack_optional<InterceptConfigClass>(j, "intercept");
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
        j["device_name"] = x.device_name;
        j["dns"] = x.dns;
        j["fwmark"] = x.fwmark;
        j["intercept"] = x.intercept;
        j["iproute"] = x.iproute;
        j["lists"] = x.lists;
        j["lists_autoupdate"] = x.lists_autoupdate;
        j["outbounds"] = x.outbounds;
        j["route"] = x.route;
    }

    inline void from_json(const json & j, Config& x) {
        x.daemon = get_stack_optional<Daemon>(j, "daemon");
        x.device_name = get_stack_optional<std::string>(j, "device_name");
        x.dns = get_stack_optional<DnsConfigClass>(j, "dns");
        x.fwmark = get_stack_optional<Fwmark>(j, "fwmark");
        x.intercept = get_stack_optional<InterceptConfigClass>(j, "intercept");
        x.iproute = get_stack_optional<Iproute>(j, "iproute");
        x.lists = get_stack_optional<std::map<std::string, ListConfigValue>>(j, "lists");
        x.lists_autoupdate = get_stack_optional<ListsAutoupdate>(j, "lists_autoupdate");
        x.outbounds = get_stack_optional<std::vector<OutboundElement>>(j, "outbounds");
        x.route = get_stack_optional<Route>(j, "route");
    }

    inline void to_json(json & j, const Config & x) {
        j = json::object();
        j["daemon"] = x.daemon;
        j["device_name"] = x.device_name;
        j["dns"] = x.dns;
        j["fwmark"] = x.fwmark;
        j["intercept"] = x.intercept;
        j["iproute"] = x.iproute;
        j["lists"] = x.lists;
        j["lists_autoupdate"] = x.lists_autoupdate;
        j["outbounds"] = x.outbounds;
        j["route"] = x.route;
    }

    inline void from_json(const json & j, ListRefreshStateValue& x) {
        x.last_updated = get_stack_optional<std::string>(j, "last_updated");
    }

    inline void to_json(json & j, const ListRefreshStateValue & x) {
        j = json::object();
        j["last_updated"] = x.last_updated;
    }

    inline void from_json(const json & j, ConfigStateResponse& x) {
        x.config = j.at("config").get<Config>();
        x.is_draft = j.at("is_draft").get<bool>();
        x.list_refresh_state = get_stack_optional<std::map<std::string, ListRefreshStateValue>>(j, "list_refresh_state");
    }

    inline void to_json(json & j, const ConfigStateResponse & x) {
        j = json::object();
        j["config"] = x.config;
        j["is_draft"] = x.is_draft;
        j["list_refresh_state"] = x.list_refresh_state;
    }

    inline void from_json(const json & j, ConfigUpdateResponse& x) {
        x.apply_started_ts = get_stack_optional<int64_t>(j, "apply_started_ts");
        x.message = j.at("message").get<std::string>();
        x.status = j.at("status").get<ConfigUpdateResponseStatus>();
    }

    inline void to_json(json & j, const ConfigUpdateResponse & x) {
        j = json::object();
        j["apply_started_ts"] = x.apply_started_ts;
        j["message"] = x.message;
        j["status"] = x.status;
    }

    inline void from_json(const json & j, DnsTestGapEvent& x) {
        x.from_seq = j.at("from_seq").get<int64_t>();
        x.to_seq = j.at("to_seq").get<int64_t>();
        x.type = j.at("type").get<DnsTestGapEventType>();
    }

    inline void to_json(json & j, const DnsTestGapEvent & x) {
        j = json::object();
        j["from_seq"] = x.from_seq;
        j["to_seq"] = x.to_seq;
        j["type"] = x.type;
    }

    inline void from_json(const json & j, DnsTestInterceptEvent& x) {
        x.added = j.at("added").get<int64_t>();
        x.admission_wait_us = get_stack_optional<int64_t>(j, "admission_wait_us");
        x.batch_pos = get_stack_optional<int64_t>(j, "batch_pos");
        x.batch_size = get_stack_optional<int64_t>(j, "batch_size");
        x.budget_left_us = get_stack_optional<int64_t>(j, "budget_left_us");
        x.cache_hits = get_stack_optional<int64_t>(j, "cache_hits");
        x.client_ip = get_stack_optional<std::string>(j, "client_ip");
        x.deferred_refresh = get_stack_optional<int64_t>(j, "deferred_refresh");
        x.domain = j.at("domain").get<std::string>();
        x.errors = j.at("errors").get<int64_t>();
        x.hold_us = j.at("hold_us").get<int64_t>();
        x.ips = j.at("ips").get<std::vector<std::string>>();
        x.late_batch_elements = get_stack_optional<int64_t>(j, "late_batch_elements");
        x.late_write = get_stack_optional<bool>(j, "late_write");
        x.lists = j.at("lists").get<std::vector<std::string>>();
        x.not_learned = get_stack_optional<int64_t>(j, "not_learned");
        x.parse_us = get_stack_optional<int64_t>(j, "parse_us");
        x.qtype = get_stack_optional<int64_t>(j, "qtype");
        x.queue_wait_us = get_stack_optional<int64_t>(j, "queue_wait_us");
        x.rcode = get_stack_optional<int64_t>(j, "rcode");
        x.refreshed = j.at("refreshed").get<int64_t>();
        x.seq = j.at("seq").get<int64_t>();
        x.set_write_us = get_stack_optional<int64_t>(j, "set_write_us");
        x.source = j.at("source").get<Source>();
        x.timed_out = j.at("timed_out").get<bool>();
        x.timeout_cause = get_stack_optional<TimeoutCause>(j, "timeout_cause");
        x.ts_ms = j.at("ts_ms").get<int64_t>();
        x.type = j.at("type").get<DnsTestInterceptEventType>();
        x.write_elements = get_stack_optional<int64_t>(j, "write_elements");
        x.write_errno = get_stack_optional<int64_t>(j, "write_errno");
    }

    inline void to_json(json & j, const DnsTestInterceptEvent & x) {
        j = json::object();
        j["added"] = x.added;
        j["admission_wait_us"] = x.admission_wait_us;
        j["batch_pos"] = x.batch_pos;
        j["batch_size"] = x.batch_size;
        j["budget_left_us"] = x.budget_left_us;
        j["cache_hits"] = x.cache_hits;
        j["client_ip"] = x.client_ip;
        j["deferred_refresh"] = x.deferred_refresh;
        j["domain"] = x.domain;
        j["errors"] = x.errors;
        j["hold_us"] = x.hold_us;
        j["ips"] = x.ips;
        j["late_batch_elements"] = x.late_batch_elements;
        j["late_write"] = x.late_write;
        j["lists"] = x.lists;
        j["not_learned"] = x.not_learned;
        j["parse_us"] = x.parse_us;
        j["qtype"] = x.qtype;
        j["queue_wait_us"] = x.queue_wait_us;
        j["rcode"] = x.rcode;
        j["refreshed"] = x.refreshed;
        j["seq"] = x.seq;
        j["set_write_us"] = x.set_write_us;
        j["source"] = x.source;
        j["timed_out"] = x.timed_out;
        j["timeout_cause"] = x.timeout_cause;
        j["ts_ms"] = x.ts_ms;
        j["type"] = x.type;
        j["write_elements"] = x.write_elements;
        j["write_errno"] = x.write_errno;
    }

    inline void from_json(const json & j, DnsmasqHealth& x) {
        x.config_hash = get_stack_optional<std::string>(j, "config_hash");
        x.dnsmasq_alive = get_stack_optional<DnsmasqAlive>(j, "dnsmasq_alive");
        x.domains = j.at("domains").get<int64_t>();
        x.last_apply_ts = get_stack_optional<int64_t>(j, "last_apply_ts");
        x.last_check_ts = get_stack_optional<int64_t>(j, "last_check_ts");
        x.last_error = get_stack_optional<std::string>(j, "last_error");
        x.last_external_reload_ts = get_stack_optional<int64_t>(j, "last_external_reload_ts");
        x.loaded_boottime_ms = get_stack_optional<int64_t>(j, "loaded_boottime_ms");
        x.loaded_hash = get_stack_optional<std::string>(j, "loaded_hash");
        x.loaded_ts = get_stack_optional<int64_t>(j, "loaded_ts");
        x.mode = j.at("mode").get<ResolverIntegration>();
        x.next_repair_ts = get_stack_optional<int64_t>(j, "next_repair_ts");
        x.probe_status = get_stack_optional<ProbeStatus>(j, "probe_status");
        x.repair_attempt = get_stack_optional<int64_t>(j, "repair_attempt");
        x.repair_max_attempts = get_stack_optional<int64_t>(j, "repair_max_attempts");
        x.repair_paused = get_stack_optional<bool>(j, "repair_paused");
        x.repair_reason = get_stack_optional<std::string>(j, "repair_reason");
        x.rules = j.at("rules").get<int64_t>();
        x.state = j.at("state").get<State>();
    }

    inline void to_json(json & j, const DnsmasqHealth & x) {
        j = json::object();
        j["config_hash"] = x.config_hash;
        j["dnsmasq_alive"] = x.dnsmasq_alive;
        j["domains"] = x.domains;
        j["last_apply_ts"] = x.last_apply_ts;
        j["last_check_ts"] = x.last_check_ts;
        j["last_error"] = x.last_error;
        j["last_external_reload_ts"] = x.last_external_reload_ts;
        j["loaded_boottime_ms"] = x.loaded_boottime_ms;
        j["loaded_hash"] = x.loaded_hash;
        j["loaded_ts"] = x.loaded_ts;
        j["mode"] = x.mode;
        j["next_repair_ts"] = x.next_repair_ts;
        j["probe_status"] = x.probe_status;
        j["repair_attempt"] = x.repair_attempt;
        j["repair_max_attempts"] = x.repair_max_attempts;
        j["repair_paused"] = x.repair_paused;
        j["repair_reason"] = x.repair_reason;
        j["rules"] = x.rules;
        j["state"] = x.state;
    }

    inline void from_json(const json & j, ValidationErrorElement& x) {
        x.message = j.at("message").get<std::string>();
        x.path = get_stack_optional<std::string>(j, "path");
    }

    inline void to_json(json & j, const ValidationErrorElement & x) {
        j = json::object();
        j["message"] = x.message;
        j["path"] = x.path;
    }

    inline void from_json(const json & j, ErrorResponse& x) {
        x.error = j.at("error").get<std::string>();
        x.validation_errors = get_stack_optional<std::vector<ValidationErrorElement>>(j, "validation_errors");
    }

    inline void to_json(json & j, const ErrorResponse & x) {
        j = json::object();
        j["error"] = x.error;
        j["validation_errors"] = x.validation_errors;
    }

    inline void from_json(const json & j, FirewallChain& x) {
        x.chain_present = j.at("chain_present").get<bool>();
        x.detail = get_stack_optional<std::string>(j, "detail");
        x.prerouting_hook_present = j.at("prerouting_hook_present").get<bool>();
        x.verification_state = j.at("verification_state").get<VerificationState>();
    }

    inline void to_json(json & j, const FirewallChain & x) {
        j = json::object();
        j["chain_present"] = x.chain_present;
        j["detail"] = x.detail;
        j["prerouting_hook_present"] = x.prerouting_hook_present;
        j["verification_state"] = x.verification_state;
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

    inline void from_json(const json & j, Capabilities& x) {
        x.addrtype = j.at("addrtype").get<bool>();
        x.connbytes = j.at("connbytes").get<bool>();
        x.conntrack_cleanup = get_stack_optional<bool>(j, "conntrack_cleanup");
        x.fail_open = get_stack_optional<bool>(j, "fail_open");
        x.nflog = j.at("nflog").get<bool>();
        x.nfqueue = j.at("nfqueue").get<bool>();
        x.payload_replacement = get_stack_optional<PayloadReplacement>(j, "payload_replacement");
    }

    inline void to_json(json & j, const Capabilities & x) {
        j = json::object();
        j["addrtype"] = x.addrtype;
        j["connbytes"] = x.connbytes;
        j["conntrack_cleanup"] = x.conntrack_cleanup;
        j["fail_open"] = x.fail_open;
        j["nflog"] = x.nflog;
        j["nfqueue"] = x.nfqueue;
        j["payload_replacement"] = x.payload_replacement;
    }

    inline void from_json(const json & j, DnsWriteLatency& x) {
        x.ge_100_ms = get_stack_optional<int64_t>(j, "ge_100ms");
        x.lt_100_ms = get_stack_optional<int64_t>(j, "lt_100ms");
        x.lt_10_ms = get_stack_optional<int64_t>(j, "lt_10ms");
        x.lt_1_ms = get_stack_optional<int64_t>(j, "lt_1ms");
        x.lt_30_ms = get_stack_optional<int64_t>(j, "lt_30ms");
        x.lt_5_ms = get_stack_optional<int64_t>(j, "lt_5ms");
        x.max_elements = get_stack_optional<int64_t>(j, "max_elements");
        x.max_us = get_stack_optional<int64_t>(j, "max_us");
    }

    inline void to_json(json & j, const DnsWriteLatency & x) {
        j = json::object();
        j["ge_100ms"] = x.ge_100_ms;
        j["lt_100ms"] = x.lt_100_ms;
        j["lt_10ms"] = x.lt_10_ms;
        j["lt_1ms"] = x.lt_1_ms;
        j["lt_30ms"] = x.lt_30_ms;
        j["lt_5ms"] = x.lt_5_ms;
        j["max_elements"] = x.max_elements;
        j["max_us"] = x.max_us;
    }

    inline void from_json(const json & j, Counters& x) {
        x.conntrack_deleted = get_stack_optional<int64_t>(j, "conntrack_deleted");
        x.conntrack_errors = get_stack_optional<int64_t>(j, "conntrack_errors");
        x.conntrack_requests = get_stack_optional<int64_t>(j, "conntrack_requests");
        x.dns_aaaa_ignored = get_stack_optional<int64_t>(j, "dns_aaaa_ignored");
        x.dns_hold_timeouts = get_stack_optional<int64_t>(j, "dns_hold_timeouts");
        x.dns_late_write_errors = get_stack_optional<int64_t>(j, "dns_late_write_errors");
        x.dns_late_writes = get_stack_optional<int64_t>(j, "dns_late_writes");
        x.dns_matched = get_stack_optional<int64_t>(j, "dns_matched");
        x.dns_packets = get_stack_optional<int64_t>(j, "dns_packets");
        x.dns_parse_errors = get_stack_optional<int64_t>(j, "dns_parse_errors");
        x.dns_refresh_deferred = get_stack_optional<int64_t>(j, "dns_refresh_deferred");
        x.dns_tcp_partial = get_stack_optional<int64_t>(j, "dns_tcp_partial");
        x.dns_timeout_admission_blocked = get_stack_optional<int64_t>(j, "dns_timeout_admission_blocked");
        x.dns_timeout_budget_spent_by_batch = get_stack_optional<int64_t>(j, "dns_timeout_budget_spent_by_batch");
        x.dns_timeout_late_batch_full = get_stack_optional<int64_t>(j, "dns_timeout_late_batch_full");
        x.dns_timeout_other = get_stack_optional<int64_t>(j, "dns_timeout_other");
        x.dns_timeout_own_write_slow = get_stack_optional<int64_t>(j, "dns_timeout_own_write_slow");
        x.dns_write_latency = get_stack_optional<DnsWriteLatency>(j, "dns_write_latency");
        x.l7_matched = get_stack_optional<int64_t>(j, "l7_matched");
        x.l7_packets = get_stack_optional<int64_t>(j, "l7_packets");
        x.l7_write_latency = get_stack_optional<DnsWriteLatency>(j, "l7_write_latency");
        x.late_write_latency = get_stack_optional<DnsWriteLatency>(j, "late_write_latency");
        x.log_overruns = get_stack_optional<int64_t>(j, "log_overruns");
        x.marker_hits = get_stack_optional<int64_t>(j, "marker_hits");
        x.queue_overruns = get_stack_optional<int64_t>(j, "queue_overruns");
        x.refresh_dropped = get_stack_optional<int64_t>(j, "refresh_dropped");
        x.refresh_skipped = get_stack_optional<int64_t>(j, "refresh_skipped");
        x.set_added = get_stack_optional<int64_t>(j, "set_added");
        x.set_cache_entries = get_stack_optional<int64_t>(j, "set_cache_entries");
        x.set_cache_hits = get_stack_optional<int64_t>(j, "set_cache_hits");
        x.set_cache_misses = get_stack_optional<int64_t>(j, "set_cache_misses");
        x.set_errors = get_stack_optional<int64_t>(j, "set_errors");
        x.set_refreshed = get_stack_optional<int64_t>(j, "set_refreshed");
        x.set_write_slow = get_stack_optional<int64_t>(j, "set_write_slow");
    }

    inline void to_json(json & j, const Counters & x) {
        j = json::object();
        j["conntrack_deleted"] = x.conntrack_deleted;
        j["conntrack_errors"] = x.conntrack_errors;
        j["conntrack_requests"] = x.conntrack_requests;
        j["dns_aaaa_ignored"] = x.dns_aaaa_ignored;
        j["dns_hold_timeouts"] = x.dns_hold_timeouts;
        j["dns_late_write_errors"] = x.dns_late_write_errors;
        j["dns_late_writes"] = x.dns_late_writes;
        j["dns_matched"] = x.dns_matched;
        j["dns_packets"] = x.dns_packets;
        j["dns_parse_errors"] = x.dns_parse_errors;
        j["dns_refresh_deferred"] = x.dns_refresh_deferred;
        j["dns_tcp_partial"] = x.dns_tcp_partial;
        j["dns_timeout_admission_blocked"] = x.dns_timeout_admission_blocked;
        j["dns_timeout_budget_spent_by_batch"] = x.dns_timeout_budget_spent_by_batch;
        j["dns_timeout_late_batch_full"] = x.dns_timeout_late_batch_full;
        j["dns_timeout_other"] = x.dns_timeout_other;
        j["dns_timeout_own_write_slow"] = x.dns_timeout_own_write_slow;
        j["dns_write_latency"] = x.dns_write_latency;
        j["l7_matched"] = x.l7_matched;
        j["l7_packets"] = x.l7_packets;
        j["l7_write_latency"] = x.l7_write_latency;
        j["late_write_latency"] = x.late_write_latency;
        j["log_overruns"] = x.log_overruns;
        j["marker_hits"] = x.marker_hits;
        j["queue_overruns"] = x.queue_overruns;
        j["refresh_dropped"] = x.refresh_dropped;
        j["refresh_skipped"] = x.refresh_skipped;
        j["set_added"] = x.set_added;
        j["set_cache_entries"] = x.set_cache_entries;
        j["set_cache_hits"] = x.set_cache_hits;
        j["set_cache_misses"] = x.set_cache_misses;
        j["set_errors"] = x.set_errors;
        j["set_refreshed"] = x.set_refreshed;
        j["set_write_slow"] = x.set_write_slow;
    }

    inline void from_json(const json & j, KernelQueue& x) {
        x.id_sequence = j.at("id_sequence").get<int64_t>();
        x.queue_dropped = j.at("queue_dropped").get<int64_t>();
        x.queue_total = j.at("queue_total").get<int64_t>();
        x.user_dropped = j.at("user_dropped").get<int64_t>();
    }

    inline void to_json(json & j, const KernelQueue & x) {
        j = json::object();
        j["id_sequence"] = x.id_sequence;
        j["queue_dropped"] = x.queue_dropped;
        j["queue_total"] = x.queue_total;
        j["user_dropped"] = x.user_dropped;
    }

    inline void from_json(const json & j, InterceptProbeFeatureElement& x) {
        x.feature = j.at("feature").get<std::string>();
        x.reason = get_stack_optional<std::string>(j, "reason");
        x.status = j.at("status").get<InterceptProbeFeatureStatus>();
    }

    inline void to_json(json & j, const InterceptProbeFeatureElement & x) {
        j = json::object();
        j["feature"] = x.feature;
        j["reason"] = x.reason;
        j["status"] = x.status;
    }

    inline void from_json(const json & j, InterceptHealthClass& x) {
        x.capabilities = j.at("capabilities").get<Capabilities>();
        x.counters = get_stack_optional<Counters>(j, "counters");
        x.dns_hold_active = j.at("dns_hold_active").get<bool>();
        x.enabled = j.at("enabled").get<bool>();
        x.events_seq = get_stack_optional<int64_t>(j, "events_seq");
        x.ipset_protocol = get_stack_optional<int64_t>(j, "ipset_protocol");
        x.kernel_queue = get_stack_optional<KernelQueue>(j, "kernel_queue");
        x.kernel_release = get_stack_optional<std::string>(j, "kernel_release");
        x.l7_active = j.at("l7_active").get<bool>();
        x.nflog_group = get_stack_optional<int64_t>(j, "nflog_group");
        x.probes = get_stack_optional<std::vector<InterceptProbeFeatureElement>>(j, "probes");
        x.queue_num = get_stack_optional<int64_t>(j, "queue_num");
        x.reasons = j.at("reasons").get<std::vector<std::string>>();
        x.running = j.at("running").get<bool>();
        x.warnings = get_stack_optional<std::vector<std::string>>(j, "warnings");
    }

    inline void to_json(json & j, const InterceptHealthClass & x) {
        j = json::object();
        j["capabilities"] = x.capabilities;
        j["counters"] = x.counters;
        j["dns_hold_active"] = x.dns_hold_active;
        j["enabled"] = x.enabled;
        j["events_seq"] = x.events_seq;
        j["ipset_protocol"] = x.ipset_protocol;
        j["kernel_queue"] = x.kernel_queue;
        j["kernel_release"] = x.kernel_release;
        j["l7_active"] = x.l7_active;
        j["nflog_group"] = x.nflog_group;
        j["probes"] = x.probes;
        j["queue_num"] = x.queue_num;
        j["reasons"] = x.reasons;
        j["running"] = x.running;
        j["warnings"] = x.warnings;
    }

    inline void from_json(const json & j, LifecycleOperationStageElement& x) {
        x.detail = get_stack_optional<std::string>(j, "detail");
        x.id = j.at("id").get<std::string>();
        x.status = j.at("status").get<LifecycleOperationStageStatus>();
        x.title = j.at("title").get<std::string>();
    }

    inline void to_json(json & j, const LifecycleOperationStageElement & x) {
        j = json::object();
        j["detail"] = x.detail;
        j["id"] = x.id;
        j["status"] = x.status;
        j["title"] = x.title;
    }

    inline void from_json(const json & j, LifecycleOperation& x) {
        x.error = get_stack_optional<std::string>(j, "error");
        x.finished_at = get_stack_optional<int64_t>(j, "finished_at");
        x.id = j.at("id").get<std::string>();
        x.stages = j.at("stages").get<std::vector<LifecycleOperationStageElement>>();
        x.started_at = j.at("started_at").get<int64_t>();
        x.status = j.at("status").get<LifecycleOperationStatus>();
        x.type = j.at("type").get<LifecycleOperationType>();
    }

    inline void to_json(json & j, const LifecycleOperation & x) {
        j = json::object();
        j["error"] = x.error;
        j["finished_at"] = x.finished_at;
        j["id"] = x.id;
        j["stages"] = x.stages;
        j["started_at"] = x.started_at;
        j["status"] = x.status;
        j["type"] = x.type;
    }

    inline void from_json(const json & j, HealthResponse& x) {
        x.apply_started_ts = get_stack_optional<int64_t>(j, "apply_started_ts");
        x.build = j.at("build").get<std::string>();
        x.build_variant = j.at("build_variant").get<std::string>();
        x.config_is_draft = j.at("config_is_draft").get<bool>();
        x.dnsmasq = get_stack_optional<DnsmasqHealth>(j, "dnsmasq");
        x.intercept = get_stack_optional<InterceptHealthClass>(j, "intercept");
        x.lifecycle_operation = get_stack_optional<LifecycleOperation>(j, "lifecycle_operation");
        x.os_type = j.at("os_type").get<std::string>();
        x.os_version = j.at("os_version").get<std::string>();
        x.rollback_available = j.at("rollback_available").get<bool>();
        x.runtime_state = get_stack_optional<RuntimeState>(j, "runtime_state");
        x.runtime_state_reason = get_stack_optional<std::string>(j, "runtime_state_reason");
        x.status = j.at("status").get<HealthResponseStatus>();
        x.version = j.at("version").get<std::string>();
    }

    inline void to_json(json & j, const HealthResponse & x) {
        j = json::object();
        j["apply_started_ts"] = x.apply_started_ts;
        j["build"] = x.build;
        j["build_variant"] = x.build_variant;
        j["config_is_draft"] = x.config_is_draft;
        j["dnsmasq"] = x.dnsmasq;
        j["intercept"] = x.intercept;
        j["lifecycle_operation"] = x.lifecycle_operation;
        j["os_type"] = x.os_type;
        j["os_version"] = x.os_version;
        j["rollback_available"] = x.rollback_available;
        j["runtime_state"] = x.runtime_state;
        j["runtime_state_reason"] = x.runtime_state_reason;
        j["status"] = x.status;
        j["version"] = x.version;
    }

    inline void from_json(const json & j, LifecycleOperationAcceptedResponse& x) {
        x.operation_id = j.at("operation_id").get<std::string>();
        x.status = j.at("status").get<LifecycleOperationAcceptedResponseStatus>();
    }

    inline void to_json(json & j, const LifecycleOperationAcceptedResponse & x) {
        j = json::object();
        j["operation_id"] = x.operation_id;
        j["status"] = x.status;
    }

    inline void from_json(const json & j, ListRefreshRequest& x) {
        x.name = get_stack_optional<std::string>(j, "name");
    }

    inline void to_json(json & j, const ListRefreshRequest & x) {
        j = json::object();
        j["name"] = x.name;
    }

    inline void from_json(const json & j, ListRefreshResponse& x) {
        x.changed_lists = j.at("changed_lists").get<std::vector<std::string>>();
        x.failed_lists = j.at("failed_lists").get<std::vector<std::string>>();
        x.message = j.at("message").get<std::string>();
        x.refreshed_lists = j.at("refreshed_lists").get<std::vector<std::string>>();
        x.reloaded = j.at("reloaded").get<bool>();
        x.status = j.at("status").get<ConfigUpdateResponseStatus>();
    }

    inline void to_json(json & j, const ListRefreshResponse & x) {
        j = json::object();
        j["changed_lists"] = x.changed_lists;
        j["failed_lists"] = x.failed_lists;
        j["message"] = x.message;
        j["refreshed_lists"] = x.refreshed_lists;
        j["reloaded"] = x.reloaded;
        j["status"] = x.status;
    }

    inline void from_json(const json & j, PolicyRuleCheck& x) {
        x.detail = get_stack_optional<std::string>(j, "detail");
        x.expected_action = get_stack_optional<ExpectedAction>(j, "expected_action");
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
        j["expected_action"] = x.expected_action;
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
        x.expected_destination = get_stack_optional<std::string>(j, "expected_destination");
        x.expected_gateway = get_stack_optional<std::string>(j, "expected_gateway");
        x.expected_interface = get_stack_optional<std::string>(j, "expected_interface");
        x.expected_metric = get_stack_optional<int64_t>(j, "expected_metric");
        x.expected_route_type = get_stack_optional<std::string>(j, "expected_route_type");
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
        j["expected_destination"] = x.expected_destination;
        j["expected_gateway"] = x.expected_gateway;
        j["expected_interface"] = x.expected_interface;
        j["expected_metric"] = x.expected_metric;
        j["expected_route_type"] = x.expected_route_type;
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
        x.firewall_backend = j.at("firewall_backend").get<RoutingHealthResponseFirewallBackend>();
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

    inline void from_json(const json & j, ListMatch& x) {
        x.list = j.at("list").get<std::string>();
        x.via = j.at("via").get<std::string>();
    }

    inline void to_json(json & j, const ListMatch & x) {
        j = json::object();
        j["list"] = x.list;
        j["via"] = x.via;
    }

    inline void from_json(const json & j, RoutingTestEntry& x) {
        x.actual_outbound = j.at("actual_outbound").get<std::string>();
        x.criteria_match = get_stack_optional<bool>(j, "criteria_match");
        x.expected_outbound = j.at("expected_outbound").get<std::string>();
        x.ip = j.at("ip").get<std::string>();
        x.list_match = get_stack_optional<ListMatch>(j, "list_match");
        x.matched_rule_index = get_stack_optional<int64_t>(j, "matched_rule_index");
        x.ok = j.at("ok").get<bool>();
    }

    inline void to_json(json & j, const RoutingTestEntry & x) {
        j = json::object();
        j["actual_outbound"] = x.actual_outbound;
        j["criteria_match"] = x.criteria_match;
        j["expected_outbound"] = x.expected_outbound;
        j["ip"] = x.ip;
        j["list_match"] = x.list_match;
        j["matched_rule_index"] = x.matched_rule_index;
        j["ok"] = x.ok;
    }

    inline void from_json(const json & j, RoutingTestRequest& x) {
        x.dest_port = get_stack_optional<int64_t>(j, "dest_port");
        x.dscp = get_stack_optional<int64_t>(j, "dscp");
        x.proto = get_stack_optional<Proto>(j, "proto");
        x.src_addr = get_stack_optional<std::string>(j, "src_addr");
        x.src_port = get_stack_optional<int64_t>(j, "src_port");
        x.target = j.at("target").get<std::string>();
    }

    inline void to_json(json & j, const RoutingTestRequest & x) {
        j = json::object();
        j["dest_port"] = x.dest_port;
        j["dscp"] = x.dscp;
        j["proto"] = x.proto;
        j["src_addr"] = x.src_addr;
        j["src_port"] = x.src_port;
        j["target"] = x.target;
    }

    inline void from_json(const json & j, SetWriteEvidence& x) {
        x.age_seconds = get_stack_optional<int64_t>(j, "age_seconds");
        x.status = j.at("status").get<RoutingTestSetWriteEvidenceStatus>();
    }

    inline void to_json(json & j, const SetWriteEvidence & x) {
        j = json::object();
        j["age_seconds"] = x.age_seconds;
        j["status"] = x.status;
    }

    inline void from_json(const json & j, RoutingTestRuleIpDiagnosticElement& x) {
        x.criteria_match = get_stack_optional<bool>(j, "criteria_match");
        x.in_ipset = get_stack_optional<bool>(j, "in_ipset");
        x.in_lists = j.at("in_lists").get<bool>();
        x.ip = j.at("ip").get<std::string>();
        x.list_match = get_stack_optional<ListMatch>(j, "list_match");
        x.set_write_evidence = get_stack_optional<SetWriteEvidence>(j, "set_write_evidence");
    }

    inline void to_json(json & j, const RoutingTestRuleIpDiagnosticElement & x) {
        j = json::object();
        j["criteria_match"] = x.criteria_match;
        j["in_ipset"] = x.in_ipset;
        j["in_lists"] = x.in_lists;
        j["ip"] = x.ip;
        j["list_match"] = x.list_match;
        j["set_write_evidence"] = x.set_write_evidence;
    }

    inline void from_json(const json & j, RoutingTestRuleDiagnosticElement& x) {
        x.interface_name = j.at("interface_name").get<std::string>();
        x.ip_rows = j.at("ip_rows").get<std::vector<RoutingTestRuleIpDiagnosticElement>>();
        x.outbound = j.at("outbound").get<std::string>();
        x.rule = j.at("rule").get<RouteRuleElement>();
        x.rule_index = j.at("rule_index").get<int64_t>();
        x.target_in_lists = j.at("target_in_lists").get<bool>();
        x.target_match = get_stack_optional<ListMatch>(j, "target_match");
    }

    inline void to_json(json & j, const RoutingTestRuleDiagnosticElement & x) {
        j = json::object();
        j["interface_name"] = x.interface_name;
        j["ip_rows"] = x.ip_rows;
        j["outbound"] = x.outbound;
        j["rule"] = x.rule;
        j["rule_index"] = x.rule_index;
        j["target_in_lists"] = x.target_in_lists;
        j["target_match"] = x.target_match;
    }

    inline void from_json(const json & j, RoutingTestResponse& x) {
        x.dns_error = get_stack_optional<std::string>(j, "dns_error");
        x.is_domain = j.at("is_domain").get<bool>();
        x.no_matching_rule = j.at("no_matching_rule").get<bool>();
        x.resolved_ips = j.at("resolved_ips").get<std::vector<std::string>>();
        x.results = j.at("results").get<std::vector<RoutingTestEntry>>();
        x.rule_diagnostics = j.at("rule_diagnostics").get<std::vector<RoutingTestRuleDiagnosticElement>>();
        x.target = j.at("target").get<std::string>();
        x.warnings = j.at("warnings").get<std::vector<std::string>>();
    }

    inline void to_json(json & j, const RoutingTestResponse & x) {
        j = json::object();
        j["dns_error"] = x.dns_error;
        j["is_domain"] = x.is_domain;
        j["no_matching_rule"] = x.no_matching_rule;
        j["resolved_ips"] = x.resolved_ips;
        j["results"] = x.results;
        j["rule_diagnostics"] = x.rule_diagnostics;
        j["target"] = x.target;
        j["warnings"] = x.warnings;
    }

    inline void from_json(const json & j, RuntimeInterfaceInventoryEntry& x) {
        x.admin_up = get_stack_optional<bool>(j, "admin_up");
        x.carrier = get_stack_optional<bool>(j, "carrier");
        x.description = get_stack_optional<std::string>(j, "description");
        x.ipv4_addresses = get_stack_optional<std::vector<std::string>>(j, "ipv4_addresses");
        x.ipv6_addresses = get_stack_optional<std::vector<std::string>>(j, "ipv6_addresses");
        x.name = j.at("name").get<std::string>();
        x.oper_state = get_stack_optional<std::string>(j, "oper_state");
        x.status = j.at("status").get<RuntimeInterfaceInventoryStatusEnum>();
    }

    inline void to_json(json & j, const RuntimeInterfaceInventoryEntry & x) {
        j = json::object();
        j["admin_up"] = x.admin_up;
        j["carrier"] = x.carrier;
        j["description"] = x.description;
        j["ipv4_addresses"] = x.ipv4_addresses;
        j["ipv6_addresses"] = x.ipv6_addresses;
        j["name"] = x.name;
        j["oper_state"] = x.oper_state;
        j["status"] = x.status;
    }

    inline void from_json(const json & j, RuntimeInterfaceInventoryResponse& x) {
        x.interfaces = j.at("interfaces").get<std::vector<RuntimeInterfaceInventoryEntry>>();
    }

    inline void to_json(json & j, const RuntimeInterfaceInventoryResponse & x) {
        j = json::object();
        j["interfaces"] = x.interfaces;
    }

    inline void from_json(const json & j, RuntimeInterfaceState& x) {
        x.detail = get_stack_optional<std::string>(j, "detail");
        x.interface_name = get_stack_optional<std::string>(j, "interface_name");
        x.latency_ms = get_stack_optional<int64_t>(j, "latency_ms");
        x.outbound_tag = j.at("outbound_tag").get<std::string>();
        x.packets_attempted = get_stack_optional<int64_t>(j, "packets_attempted");
        x.packets_failed = get_stack_optional<int64_t>(j, "packets_failed");
        x.packets_received = get_stack_optional<int64_t>(j, "packets_received");
        x.packets_sent = get_stack_optional<int64_t>(j, "packets_sent");
        x.probe_target = get_stack_optional<std::string>(j, "probe_target");
        x.status = j.at("status").get<RuntimeInterfaceStatusEnum>();
    }

    inline void to_json(json & j, const RuntimeInterfaceState & x) {
        j = json::object();
        j["detail"] = x.detail;
        if (x.interface_name.has_value()) j["interface_name"] = *x.interface_name;
        j["latency_ms"] = x.latency_ms;
        j["outbound_tag"] = x.outbound_tag;
        j["packets_attempted"] = x.packets_attempted;
        j["packets_failed"] = x.packets_failed;
        j["packets_received"] = x.packets_received;
        j["packets_sent"] = x.packets_sent;
        j["probe_target"] = x.probe_target;
        j["status"] = x.status;
    }

    inline void from_json(const json & j, RuntimeOutboundStateElement& x) {
        x.detail = get_stack_optional<std::string>(j, "detail");
        x.interfaces = j.at("interfaces").get<std::vector<RuntimeInterfaceState>>();
        x.status = j.at("status").get<RuntimeOutboundStatusEnum>();
        x.tag = j.at("tag").get<std::string>();
        x.type = j.at("type").get<OutboundType>();
    }

    inline void to_json(json & j, const RuntimeOutboundStateElement & x) {
        j = json::object();
        j["detail"] = x.detail;
        j["interfaces"] = x.interfaces;
        j["status"] = x.status;
        j["tag"] = x.tag;
        j["type"] = x.type;
    }

    inline void from_json(const json & j, RuntimeOutboundsResponse& x) {
        x.outbounds = j.at("outbounds").get<std::vector<RuntimeOutboundStateElement>>();
    }

    inline void to_json(json & j, const RuntimeOutboundsResponse & x) {
        j = json::object();
        j["outbounds"] = x.outbounds;
    }

    inline void from_json(const json & j, StatusEventInterfaces& x) {
        x.data = j.at("data").get<RuntimeInterfaceInventoryResponse>();
        x.type = j.at("type").get<StatusEventInterfacesType>();
    }

    inline void to_json(json & j, const StatusEventInterfaces & x) {
        j = json::object();
        j["data"] = x.data;
        j["type"] = x.type;
    }

    inline void from_json(const json & j, StatusEventOutbounds& x) {
        x.data = j.at("data").get<RuntimeOutboundsResponse>();
        x.type = j.at("type").get<StatusEventOutboundsType>();
    }

    inline void to_json(json & j, const StatusEventOutbounds & x) {
        j = json::object();
        j["data"] = x.data;
        j["type"] = x.type;
    }

    inline void from_json(const json & j, StatusEventService& x) {
        x.data = j.at("data").get<HealthResponse>();
        x.type = j.at("type").get<StatusEventServiceType>();
    }

    inline void to_json(json & j, const StatusEventService & x) {
        j = json::object();
        j["data"] = x.data;
        j["type"] = x.type;
    }

    inline void from_json(const json & j, Data& x) {
        x.interfaces = j.at("interfaces").get<RuntimeInterfaceInventoryResponse>();
        x.outbounds = j.at("outbounds").get<RuntimeOutboundsResponse>();
        x.service = j.at("service").get<HealthResponse>();
    }

    inline void to_json(json & j, const Data & x) {
        j = json::object();
        j["interfaces"] = x.interfaces;
        j["outbounds"] = x.outbounds;
        j["service"] = x.service;
    }

    inline void from_json(const json & j, StatusEventSnapshot& x) {
        x.data = j.at("data").get<Data>();
        x.type = j.at("type").get<StatusEventSnapshotType>();
    }

    inline void to_json(json & j, const StatusEventSnapshot & x) {
        j = json::object();
        j["data"] = x.data;
        j["type"] = x.type;
    }

    inline void from_json(const json & j, KeenPbrTypesHwuqQh& x) {
        x.api_config = get_stack_optional<ApiConfig>(j, "ApiConfig");
        x.authentication_config = get_stack_optional<AuthenticationConfigClass>(j, "AuthenticationConfig");
        x.auth_login_request = get_stack_optional<AuthLoginRequest>(j, "AuthLoginRequest");
        x.auth_login_response = get_stack_optional<AuthLoginResponse>(j, "AuthLoginResponse");
        x.auth_password_request = get_stack_optional<AuthPasswordRequest>(j, "AuthPasswordRequest");
        x.auth_password_status = get_stack_optional<AuthPasswordStatus>(j, "AuthPasswordStatus");
        x.auth_settings_request = get_stack_optional<AuthSettingsRequest>(j, "AuthSettingsRequest");
        x.auth_settings_response = get_stack_optional<AuthSettingsResponse>(j, "AuthSettingsResponse");
        x.auth_status_response = get_stack_optional<AuthStatusResponse>(j, "AuthStatusResponse");
        x.cache_metadata = get_stack_optional<CacheMetadata>(j, "CacheMetadata");
        x.check_status = get_stack_optional<CheckStatus>(j, "CheckStatus");
        x.circuit_breaker_config = get_stack_optional<CircuitBreakerConfig>(j, "CircuitBreakerConfig");
        x.config_object = get_stack_optional<ConfigObject>(j, "ConfigObject");
        x.config_state_response = get_stack_optional<ConfigStateResponse>(j, "ConfigStateResponse");
        x.config_update_response = get_stack_optional<ConfigUpdateResponse>(j, "ConfigUpdateResponse");
        x.conntrack_on_switch = get_stack_optional<ConntrackOnSwitch>(j, "ConntrackOnSwitch");
        x.cors_config = get_stack_optional<CorsConfigClass>(j, "CorsConfig");
        x.daemon_config = get_stack_optional<Daemon>(j, "DaemonConfig");
        x.default_gateway = get_stack_optional<DefaultGateway>(j, "DefaultGateway");
        x.dns_config = get_stack_optional<DnsConfigClass>(j, "DnsConfig");
        x.dnsmasq_health = get_stack_optional<DnsmasqHealth>(j, "DnsmasqHealth");
        x.dns_rule = get_stack_optional<DnsRuleElement>(j, "DnsRule");
        x.dns_server = get_stack_optional<DnsServerElement>(j, "DnsServer");
        x.dns_system_resolver = get_stack_optional<SystemResolver>(j, "DnsSystemResolver");
        x.dns_test_gap_event = get_stack_optional<DnsTestGapEvent>(j, "DnsTestGapEvent");
        x.dns_test_intercept_event = get_stack_optional<DnsTestInterceptEvent>(j, "DnsTestInterceptEvent");
        x.dns_test_server = get_stack_optional<DnsTestServer>(j, "DnsTestServer");
        x.draft_config = get_stack_optional<Config>(j, "DraftConfig");
        x.error_response = get_stack_optional<ErrorResponse>(j, "ErrorResponse");
        x.firewall_chain = get_stack_optional<FirewallChain>(j, "FirewallChain");
        x.firewall_rule_check = get_stack_optional<FirewallRuleCheck>(j, "FirewallRuleCheck");
        x.fwmark_config = get_stack_optional<Fwmark>(j, "FwmarkConfig");
        x.health_response = get_stack_optional<HealthResponse>(j, "HealthResponse");
        x.icmp_candidate = get_stack_optional<IcmpCandidateElement>(j, "IcmpCandidate");
        x.intercept_capabilities_health = get_stack_optional<Capabilities>(j, "InterceptCapabilitiesHealth");
        x.intercept_config = get_stack_optional<InterceptConfigClass>(j, "InterceptConfig");
        x.intercept_counters_health = get_stack_optional<Counters>(j, "InterceptCountersHealth");
        x.intercept_dns_config = get_stack_optional<InterceptDnsConfigClass>(j, "InterceptDnsConfig");
        x.intercept_health = get_stack_optional<InterceptHealthClass>(j, "InterceptHealth");
        x.intercept_kernel_queue = get_stack_optional<KernelQueue>(j, "InterceptKernelQueue");
        x.intercept_l7_config = get_stack_optional<L7>(j, "InterceptL7Config");
        x.intercept_marker_config = get_stack_optional<Marker>(j, "InterceptMarkerConfig");
        x.intercept_probe_feature = get_stack_optional<InterceptProbeFeatureElement>(j, "InterceptProbeFeature");
        x.intercept_write_latency = get_stack_optional<DnsWriteLatency>(j, "InterceptWriteLatency");
        x.iproute_config = get_stack_optional<Iproute>(j, "IprouteConfig");
        x.lifecycle_operation = get_stack_optional<LifecycleOperation>(j, "LifecycleOperation");
        x.lifecycle_operation_accepted_response = get_stack_optional<LifecycleOperationAcceptedResponse>(j, "LifecycleOperationAcceptedResponse");
        x.lifecycle_operation_stage = get_stack_optional<LifecycleOperationStageElement>(j, "LifecycleOperationStage");
        x.list_config = get_stack_optional<ListConfigValue>(j, "ListConfig");
        x.list_refresh_request = get_stack_optional<ListRefreshRequest>(j, "ListRefreshRequest");
        x.list_refresh_response = get_stack_optional<ListRefreshResponse>(j, "ListRefreshResponse");
        x.list_refresh_state = get_stack_optional<ListRefreshStateValue>(j, "ListRefreshState");
        x.lists_autoupdate_config = get_stack_optional<ListsAutoupdate>(j, "ListsAutoupdateConfig");
        x.outbound = get_stack_optional<OutboundElement>(j, "Outbound");
        x.outbound_group = get_stack_optional<OutboundGroupElement>(j, "OutboundGroup");
        x.outbound_group_member = get_stack_optional<OutboundGroupMemberElement>(j, "OutboundGroupMember");
        x.policy_rule_check = get_stack_optional<PolicyRuleCheck>(j, "PolicyRuleCheck");
        x.reload_response = get_stack_optional<ReloadResponse>(j, "ReloadResponse");
        x.resolver_integration_mode = get_stack_optional<ResolverIntegration>(j, "ResolverIntegrationMode");
        x.retry_config = get_stack_optional<Retry>(j, "RetryConfig");
        x.route_config = get_stack_optional<Route>(j, "RouteConfig");
        x.route_rule = get_stack_optional<RouteRuleElement>(j, "RouteRule");
        x.route_table_check = get_stack_optional<RouteTableCheck>(j, "RouteTableCheck");
        x.routing_health_error_response = get_stack_optional<RoutingHealthErrorResponse>(j, "RoutingHealthErrorResponse");
        x.routing_health_response = get_stack_optional<RoutingHealthResponse>(j, "RoutingHealthResponse");
        x.routing_test_entry = get_stack_optional<RoutingTestEntry>(j, "RoutingTestEntry");
        x.routing_test_list_match = get_stack_optional<ListMatch>(j, "RoutingTestListMatch");
        x.routing_test_request = get_stack_optional<RoutingTestRequest>(j, "RoutingTestRequest");
        x.routing_test_response = get_stack_optional<RoutingTestResponse>(j, "RoutingTestResponse");
        x.routing_test_rule_diagnostic = get_stack_optional<RoutingTestRuleDiagnosticElement>(j, "RoutingTestRuleDiagnostic");
        x.routing_test_rule_ip_diagnostic = get_stack_optional<RoutingTestRuleIpDiagnosticElement>(j, "RoutingTestRuleIpDiagnostic");
        x.routing_test_set_write_evidence = get_stack_optional<SetWriteEvidence>(j, "RoutingTestSetWriteEvidence");
        x.runtime_interface_inventory_entry = get_stack_optional<RuntimeInterfaceInventoryEntry>(j, "RuntimeInterfaceInventoryEntry");
        x.runtime_interface_inventory_response = get_stack_optional<RuntimeInterfaceInventoryResponse>(j, "RuntimeInterfaceInventoryResponse");
        x.runtime_interface_inventory_status = get_stack_optional<RuntimeInterfaceInventoryStatusEnum>(j, "RuntimeInterfaceInventoryStatus");
        x.runtime_interface_state = get_stack_optional<RuntimeInterfaceState>(j, "RuntimeInterfaceState");
        x.runtime_interface_status = get_stack_optional<RuntimeInterfaceStatusEnum>(j, "RuntimeInterfaceStatus");
        x.runtime_outbounds_response = get_stack_optional<RuntimeOutboundsResponse>(j, "RuntimeOutboundsResponse");
        x.runtime_outbound_state = get_stack_optional<RuntimeOutboundStateElement>(j, "RuntimeOutboundState");
        x.runtime_outbound_status = get_stack_optional<RuntimeOutboundStatusEnum>(j, "RuntimeOutboundStatus");
        x.status_event_interfaces = get_stack_optional<StatusEventInterfaces>(j, "StatusEventInterfaces");
        x.status_event_outbounds = get_stack_optional<StatusEventOutbounds>(j, "StatusEventOutbounds");
        x.status_event_service = get_stack_optional<StatusEventService>(j, "StatusEventService");
        x.status_event_snapshot = get_stack_optional<StatusEventSnapshot>(j, "StatusEventSnapshot");
        x.test_group_strategy = get_stack_optional<Strategy>(j, "TestGroupStrategy");
        x.validation_error = get_stack_optional<ValidationErrorElement>(j, "ValidationError");
    }

    inline void to_json(json & j, const KeenPbrTypesHwuqQh & x) {
        j = json::object();
        j["ApiConfig"] = x.api_config;
        j["AuthenticationConfig"] = x.authentication_config;
        j["AuthLoginRequest"] = x.auth_login_request;
        j["AuthLoginResponse"] = x.auth_login_response;
        j["AuthPasswordRequest"] = x.auth_password_request;
        j["AuthPasswordStatus"] = x.auth_password_status;
        j["AuthSettingsRequest"] = x.auth_settings_request;
        j["AuthSettingsResponse"] = x.auth_settings_response;
        j["AuthStatusResponse"] = x.auth_status_response;
        j["CacheMetadata"] = x.cache_metadata;
        j["CheckStatus"] = x.check_status;
        j["CircuitBreakerConfig"] = x.circuit_breaker_config;
        j["ConfigObject"] = x.config_object;
        j["ConfigStateResponse"] = x.config_state_response;
        j["ConfigUpdateResponse"] = x.config_update_response;
        j["ConntrackOnSwitch"] = x.conntrack_on_switch;
        j["CorsConfig"] = x.cors_config;
        j["DaemonConfig"] = x.daemon_config;
        j["DefaultGateway"] = x.default_gateway;
        j["DnsConfig"] = x.dns_config;
        j["DnsmasqHealth"] = x.dnsmasq_health;
        j["DnsRule"] = x.dns_rule;
        j["DnsServer"] = x.dns_server;
        j["DnsSystemResolver"] = x.dns_system_resolver;
        j["DnsTestGapEvent"] = x.dns_test_gap_event;
        j["DnsTestInterceptEvent"] = x.dns_test_intercept_event;
        j["DnsTestServer"] = x.dns_test_server;
        j["DraftConfig"] = x.draft_config;
        j["ErrorResponse"] = x.error_response;
        j["FirewallChain"] = x.firewall_chain;
        j["FirewallRuleCheck"] = x.firewall_rule_check;
        j["FwmarkConfig"] = x.fwmark_config;
        j["HealthResponse"] = x.health_response;
        j["IcmpCandidate"] = x.icmp_candidate;
        j["InterceptCapabilitiesHealth"] = x.intercept_capabilities_health;
        j["InterceptConfig"] = x.intercept_config;
        j["InterceptCountersHealth"] = x.intercept_counters_health;
        j["InterceptDnsConfig"] = x.intercept_dns_config;
        j["InterceptHealth"] = x.intercept_health;
        j["InterceptKernelQueue"] = x.intercept_kernel_queue;
        j["InterceptL7Config"] = x.intercept_l7_config;
        j["InterceptMarkerConfig"] = x.intercept_marker_config;
        j["InterceptProbeFeature"] = x.intercept_probe_feature;
        j["InterceptWriteLatency"] = x.intercept_write_latency;
        j["IprouteConfig"] = x.iproute_config;
        j["LifecycleOperation"] = x.lifecycle_operation;
        j["LifecycleOperationAcceptedResponse"] = x.lifecycle_operation_accepted_response;
        j["LifecycleOperationStage"] = x.lifecycle_operation_stage;
        j["ListConfig"] = x.list_config;
        j["ListRefreshRequest"] = x.list_refresh_request;
        j["ListRefreshResponse"] = x.list_refresh_response;
        j["ListRefreshState"] = x.list_refresh_state;
        j["ListsAutoupdateConfig"] = x.lists_autoupdate_config;
        j["Outbound"] = x.outbound;
        j["OutboundGroup"] = x.outbound_group;
        j["OutboundGroupMember"] = x.outbound_group_member;
        j["PolicyRuleCheck"] = x.policy_rule_check;
        j["ReloadResponse"] = x.reload_response;
        j["ResolverIntegrationMode"] = x.resolver_integration_mode;
        j["RetryConfig"] = x.retry_config;
        j["RouteConfig"] = x.route_config;
        j["RouteRule"] = x.route_rule;
        j["RouteTableCheck"] = x.route_table_check;
        j["RoutingHealthErrorResponse"] = x.routing_health_error_response;
        j["RoutingHealthResponse"] = x.routing_health_response;
        j["RoutingTestEntry"] = x.routing_test_entry;
        j["RoutingTestListMatch"] = x.routing_test_list_match;
        j["RoutingTestRequest"] = x.routing_test_request;
        j["RoutingTestResponse"] = x.routing_test_response;
        j["RoutingTestRuleDiagnostic"] = x.routing_test_rule_diagnostic;
        j["RoutingTestRuleIpDiagnostic"] = x.routing_test_rule_ip_diagnostic;
        j["RoutingTestSetWriteEvidence"] = x.routing_test_set_write_evidence;
        j["RuntimeInterfaceInventoryEntry"] = x.runtime_interface_inventory_entry;
        j["RuntimeInterfaceInventoryResponse"] = x.runtime_interface_inventory_response;
        j["RuntimeInterfaceInventoryStatus"] = x.runtime_interface_inventory_status;
        j["RuntimeInterfaceState"] = x.runtime_interface_state;
        j["RuntimeInterfaceStatus"] = x.runtime_interface_status;
        j["RuntimeOutboundsResponse"] = x.runtime_outbounds_response;
        j["RuntimeOutboundState"] = x.runtime_outbound_state;
        j["RuntimeOutboundStatus"] = x.runtime_outbound_status;
        j["StatusEventInterfaces"] = x.status_event_interfaces;
        j["StatusEventOutbounds"] = x.status_event_outbounds;
        j["StatusEventService"] = x.status_event_service;
        j["StatusEventSnapshot"] = x.status_event_snapshot;
        j["TestGroupStrategy"] = x.test_group_strategy;
        j["ValidationError"] = x.validation_error;
    }

    inline void from_json(const json & j, CheckStatus & x) {
        if (j == "mismatch") x = CheckStatus::MISMATCH;
        else if (j == "missing") x = CheckStatus::MISSING;
        else if (j == "ok") x = CheckStatus::OK;
        else { throw std::runtime_error("Cannot deserialize to enumeration \"CheckStatus\""); }
    }

    inline void to_json(json & j, const CheckStatus & x) {
        switch (x) {
            case CheckStatus::MISMATCH: j = "mismatch"; break;
            case CheckStatus::MISSING: j = "missing"; break;
            case CheckStatus::OK: j = "ok"; break;
            default: throw std::runtime_error("Unexpected value in enumeration \"CheckStatus\": " + std::to_string(static_cast<int>(x)));
        }
    }

    inline void from_json(const json & j, DaemonConfigFirewallBackend & x) {
        if (j == "auto") x = DaemonConfigFirewallBackend::AUTO;
        else if (j == "iptables") x = DaemonConfigFirewallBackend::IPTABLES;
        else if (j == "nftables") x = DaemonConfigFirewallBackend::NFTABLES;
        else { throw std::runtime_error("Cannot deserialize to enumeration \"DaemonConfigFirewallBackend\""); }
    }

    inline void to_json(json & j, const DaemonConfigFirewallBackend & x) {
        switch (x) {
            case DaemonConfigFirewallBackend::AUTO: j = "auto"; break;
            case DaemonConfigFirewallBackend::IPTABLES: j = "iptables"; break;
            case DaemonConfigFirewallBackend::NFTABLES: j = "nftables"; break;
            default: throw std::runtime_error("Unexpected value in enumeration \"DaemonConfigFirewallBackend\": " + std::to_string(static_cast<int>(x)));
        }
    }

    inline void from_json(const json & j, StrictEnforcementAction & x) {
        if (j == "blackhole") x = StrictEnforcementAction::BLACKHOLE;
        else if (j == "unreachable") x = StrictEnforcementAction::UNREACHABLE;
        else { throw std::runtime_error("Cannot deserialize to enumeration \"StrictEnforcementAction\""); }
    }

    inline void to_json(json & j, const StrictEnforcementAction & x) {
        switch (x) {
            case StrictEnforcementAction::BLACKHOLE: j = "blackhole"; break;
            case StrictEnforcementAction::UNREACHABLE: j = "unreachable"; break;
            default: throw std::runtime_error("Unexpected value in enumeration \"StrictEnforcementAction\": " + std::to_string(static_cast<int>(x)));
        }
    }

    inline void from_json(const json & j, ResolverIntegration & x) {
        if (j == "dnsmasq") x = ResolverIntegration::DNSMASQ;
        else if (j == "none") x = ResolverIntegration::NONE;
        else { throw std::runtime_error("Cannot deserialize to enumeration \"ResolverIntegration\""); }
    }

    inline void to_json(json & j, const ResolverIntegration & x) {
        switch (x) {
            case ResolverIntegration::DNSMASQ: j = "dnsmasq"; break;
            case ResolverIntegration::NONE: j = "none"; break;
            default: throw std::runtime_error("Unexpected value in enumeration \"ResolverIntegration\": " + std::to_string(static_cast<int>(x)));
        }
    }

    inline void from_json(const json & j, DnsServerType & x) {
        if (j == "keenetic") x = DnsServerType::KEENETIC;
        else if (j == "static") x = DnsServerType::STATIC;
        else { throw std::runtime_error("Cannot deserialize to enumeration \"DnsServerType\""); }
    }

    inline void to_json(json & j, const DnsServerType & x) {
        switch (x) {
            case DnsServerType::KEENETIC: j = "keenetic"; break;
            case DnsServerType::STATIC: j = "static"; break;
            default: throw std::runtime_error("Unexpected value in enumeration \"DnsServerType\": " + std::to_string(static_cast<int>(x)));
        }
    }

    inline void from_json(const json & j, ConntrackOnSwitch & x) {
        if (j == "delete") x = ConntrackOnSwitch::DELETE;
        else if (j == "preserve") x = ConntrackOnSwitch::PRESERVE;
        else { throw std::runtime_error("Cannot deserialize to enumeration \"ConntrackOnSwitch\""); }
    }

    inline void to_json(json & j, const ConntrackOnSwitch & x) {
        switch (x) {
            case ConntrackOnSwitch::DELETE: j = "delete"; break;
            case ConntrackOnSwitch::PRESERVE: j = "preserve"; break;
            default: throw std::runtime_error("Unexpected value in enumeration \"ConntrackOnSwitch\": " + std::to_string(static_cast<int>(x)));
        }
    }

    inline void from_json(const json & j, Strategy & x) {
        if (j == "balance") x = Strategy::BALANCE;
        else if (j == "priority") x = Strategy::PRIORITY;
        else { throw std::runtime_error("Cannot deserialize to enumeration \"Strategy\""); }
    }

    inline void to_json(json & j, const Strategy & x) {
        switch (x) {
            case Strategy::BALANCE: j = "balance"; break;
            case Strategy::PRIORITY: j = "priority"; break;
            default: throw std::runtime_error("Unexpected value in enumeration \"Strategy\": " + std::to_string(static_cast<int>(x)));
        }
    }

    inline void from_json(const json & j, OutboundType & x) {
        if (j == "blackhole") x = OutboundType::BLACKHOLE;
        else if (j == "icmptest") x = OutboundType::ICMPTEST;
        else if (j == "ignore") x = OutboundType::IGNORE;
        else if (j == "interface") x = OutboundType::INTERFACE;
        else if (j == "table") x = OutboundType::TABLE;
        else if (j == "urltest") x = OutboundType::URLTEST;
        else { throw std::runtime_error("Cannot deserialize to enumeration \"OutboundType\""); }
    }

    inline void to_json(json & j, const OutboundType & x) {
        switch (x) {
            case OutboundType::BLACKHOLE: j = "blackhole"; break;
            case OutboundType::ICMPTEST: j = "icmptest"; break;
            case OutboundType::IGNORE: j = "ignore"; break;
            case OutboundType::INTERFACE: j = "interface"; break;
            case OutboundType::TABLE: j = "table"; break;
            case OutboundType::URLTEST: j = "urltest"; break;
            default: throw std::runtime_error("Unexpected value in enumeration \"OutboundType\": " + std::to_string(static_cast<int>(x)));
        }
    }

    inline void from_json(const json & j, DefaultGateway & x) {
        if (j == "ipv4") x = DefaultGateway::IPV4;
        else if (j == "ipv6") x = DefaultGateway::IPV6;
        else { throw std::runtime_error("Cannot deserialize to enumeration \"DefaultGateway\""); }
    }

    inline void to_json(json & j, const DefaultGateway & x) {
        switch (x) {
            case DefaultGateway::IPV4: j = "ipv4"; break;
            case DefaultGateway::IPV6: j = "ipv6"; break;
            default: throw std::runtime_error("Unexpected value in enumeration \"DefaultGateway\": " + std::to_string(static_cast<int>(x)));
        }
    }

    inline void from_json(const json & j, ConfigUpdateResponseStatus & x) {
        if (j == "ok") x = ConfigUpdateResponseStatus::OK;
        else { throw std::runtime_error("Cannot deserialize to enumeration \"ConfigUpdateResponseStatus\""); }
    }

    inline void to_json(json & j, const ConfigUpdateResponseStatus & x) {
        switch (x) {
            case ConfigUpdateResponseStatus::OK: j = "ok"; break;
            default: throw std::runtime_error("Unexpected value in enumeration \"ConfigUpdateResponseStatus\": " + std::to_string(static_cast<int>(x)));
        }
    }

    inline void from_json(const json & j, DnsTestGapEventType & x) {
        if (j == "GAP") x = DnsTestGapEventType::GAP;
        else { throw std::runtime_error("Cannot deserialize to enumeration \"DnsTestGapEventType\""); }
    }

    inline void to_json(json & j, const DnsTestGapEventType & x) {
        switch (x) {
            case DnsTestGapEventType::GAP: j = "GAP"; break;
            default: throw std::runtime_error("Unexpected value in enumeration \"DnsTestGapEventType\": " + std::to_string(static_cast<int>(x)));
        }
    }

    inline void from_json(const json & j, Source & x) {
        if (j == "dns") x = Source::DNS;
        else if (j == "http") x = Source::HTTP;
        else if (j == "marker") x = Source::MARKER;
        else if (j == "quic") x = Source::QUIC;
        else if (j == "sni") x = Source::SNI;
        else { throw std::runtime_error("Cannot deserialize to enumeration \"Source\""); }
    }

    inline void to_json(json & j, const Source & x) {
        switch (x) {
            case Source::DNS: j = "dns"; break;
            case Source::HTTP: j = "http"; break;
            case Source::MARKER: j = "marker"; break;
            case Source::QUIC: j = "quic"; break;
            case Source::SNI: j = "sni"; break;
            default: throw std::runtime_error("Unexpected value in enumeration \"Source\": " + std::to_string(static_cast<int>(x)));
        }
    }

    inline void from_json(const json & j, TimeoutCause & x) {
        if (j == "admission_blocked") x = TimeoutCause::ADMISSION_BLOCKED;
        else if (j == "budget_spent_by_batch") x = TimeoutCause::BUDGET_SPENT_BY_BATCH;
        else if (j == "late_batch_full") x = TimeoutCause::LATE_BATCH_FULL;
        else if (j == "other") x = TimeoutCause::OTHER;
        else if (j == "own_write_slow") x = TimeoutCause::OWN_WRITE_SLOW;
        else { throw std::runtime_error("Cannot deserialize to enumeration \"TimeoutCause\""); }
    }

    inline void to_json(json & j, const TimeoutCause & x) {
        switch (x) {
            case TimeoutCause::ADMISSION_BLOCKED: j = "admission_blocked"; break;
            case TimeoutCause::BUDGET_SPENT_BY_BATCH: j = "budget_spent_by_batch"; break;
            case TimeoutCause::LATE_BATCH_FULL: j = "late_batch_full"; break;
            case TimeoutCause::OTHER: j = "other"; break;
            case TimeoutCause::OWN_WRITE_SLOW: j = "own_write_slow"; break;
            default: throw std::runtime_error("Unexpected value in enumeration \"TimeoutCause\": " + std::to_string(static_cast<int>(x)));
        }
    }

    inline void from_json(const json & j, DnsTestInterceptEventType & x) {
        if (j == "INTERCEPT") x = DnsTestInterceptEventType::INTERCEPT;
        else { throw std::runtime_error("Cannot deserialize to enumeration \"DnsTestInterceptEventType\""); }
    }

    inline void to_json(json & j, const DnsTestInterceptEventType & x) {
        switch (x) {
            case DnsTestInterceptEventType::INTERCEPT: j = "INTERCEPT"; break;
            default: throw std::runtime_error("Unexpected value in enumeration \"DnsTestInterceptEventType\": " + std::to_string(static_cast<int>(x)));
        }
    }

    inline void from_json(const json & j, DnsmasqAlive & x) {
        if (j == "alive") x = DnsmasqAlive::ALIVE;
        else if (j == "dead") x = DnsmasqAlive::DEAD;
        else if (j == "unknown") x = DnsmasqAlive::UNKNOWN;
        else { throw std::runtime_error("Cannot deserialize to enumeration \"DnsmasqAlive\""); }
    }

    inline void to_json(json & j, const DnsmasqAlive & x) {
        switch (x) {
            case DnsmasqAlive::ALIVE: j = "alive"; break;
            case DnsmasqAlive::DEAD: j = "dead"; break;
            case DnsmasqAlive::UNKNOWN: j = "unknown"; break;
            default: throw std::runtime_error("Unexpected value in enumeration \"DnsmasqAlive\": " + std::to_string(static_cast<int>(x)));
        }
    }

    inline void from_json(const json & j, ProbeStatus & x) {
        if (j == "invalid") x = ProbeStatus::INVALID;
        else if (j == "missing") x = ProbeStatus::MISSING;
        else if (j == "not_checked") x = ProbeStatus::NOT_CHECKED;
        else if (j == "ok") x = ProbeStatus::OK;
        else if (j == "query_failed") x = ProbeStatus::QUERY_FAILED;
        else { throw std::runtime_error("Cannot deserialize to enumeration \"ProbeStatus\""); }
    }

    inline void to_json(json & j, const ProbeStatus & x) {
        switch (x) {
            case ProbeStatus::INVALID: j = "invalid"; break;
            case ProbeStatus::MISSING: j = "missing"; break;
            case ProbeStatus::NOT_CHECKED: j = "not_checked"; break;
            case ProbeStatus::OK: j = "ok"; break;
            case ProbeStatus::QUERY_FAILED: j = "query_failed"; break;
            default: throw std::runtime_error("Unexpected value in enumeration \"ProbeStatus\": " + std::to_string(static_cast<int>(x)));
        }
    }

    inline void from_json(const json & j, State & x) {
        if (j == "applying") x = State::APPLYING;
        else if (j == "disabled") x = State::DISABLED;
        else if (j == "error") x = State::ERROR;
        else if (j == "ok") x = State::OK;
        else if (j == "reconciling") x = State::RECONCILING;
        else { throw std::runtime_error("Cannot deserialize to enumeration \"State\""); }
    }

    inline void to_json(json & j, const State & x) {
        switch (x) {
            case State::APPLYING: j = "applying"; break;
            case State::DISABLED: j = "disabled"; break;
            case State::ERROR: j = "error"; break;
            case State::OK: j = "ok"; break;
            case State::RECONCILING: j = "reconciling"; break;
            default: throw std::runtime_error("Unexpected value in enumeration \"State\": " + std::to_string(static_cast<int>(x)));
        }
    }

    inline void from_json(const json & j, VerificationState & x) {
        if (j == "failed") x = VerificationState::FAILED;
        else if (j == "unavailable") x = VerificationState::UNAVAILABLE;
        else if (j == "verified") x = VerificationState::VERIFIED;
        else { throw std::runtime_error("Cannot deserialize to enumeration \"VerificationState\""); }
    }

    inline void to_json(json & j, const VerificationState & x) {
        switch (x) {
            case VerificationState::FAILED: j = "failed"; break;
            case VerificationState::UNAVAILABLE: j = "unavailable"; break;
            case VerificationState::VERIFIED: j = "verified"; break;
            default: throw std::runtime_error("Unexpected value in enumeration \"VerificationState\": " + std::to_string(static_cast<int>(x)));
        }
    }

    inline void from_json(const json & j, PayloadReplacement & x) {
        if (j == "supported") x = PayloadReplacement::SUPPORTED;
        else if (j == "unknown") x = PayloadReplacement::UNKNOWN;
        else if (j == "unsupported") x = PayloadReplacement::UNSUPPORTED;
        else { throw std::runtime_error("Cannot deserialize to enumeration \"PayloadReplacement\""); }
    }

    inline void to_json(json & j, const PayloadReplacement & x) {
        switch (x) {
            case PayloadReplacement::SUPPORTED: j = "supported"; break;
            case PayloadReplacement::UNKNOWN: j = "unknown"; break;
            case PayloadReplacement::UNSUPPORTED: j = "unsupported"; break;
            default: throw std::runtime_error("Unexpected value in enumeration \"PayloadReplacement\": " + std::to_string(static_cast<int>(x)));
        }
    }

    inline void from_json(const json & j, InterceptProbeFeatureStatus & x) {
        if (j == "error") x = InterceptProbeFeatureStatus::ERROR;
        else if (j == "not_run") x = InterceptProbeFeatureStatus::NOT_RUN;
        else if (j == "ok") x = InterceptProbeFeatureStatus::OK;
        else if (j == "skipped") x = InterceptProbeFeatureStatus::SKIPPED;
        else if (j == "unsupported") x = InterceptProbeFeatureStatus::UNSUPPORTED;
        else { throw std::runtime_error("Cannot deserialize to enumeration \"InterceptProbeFeatureStatus\""); }
    }

    inline void to_json(json & j, const InterceptProbeFeatureStatus & x) {
        switch (x) {
            case InterceptProbeFeatureStatus::ERROR: j = "error"; break;
            case InterceptProbeFeatureStatus::NOT_RUN: j = "not_run"; break;
            case InterceptProbeFeatureStatus::OK: j = "ok"; break;
            case InterceptProbeFeatureStatus::SKIPPED: j = "skipped"; break;
            case InterceptProbeFeatureStatus::UNSUPPORTED: j = "unsupported"; break;
            default: throw std::runtime_error("Unexpected value in enumeration \"InterceptProbeFeatureStatus\": " + std::to_string(static_cast<int>(x)));
        }
    }

    inline void from_json(const json & j, LifecycleOperationStageStatus & x) {
        if (j == "failed") x = LifecycleOperationStageStatus::FAILED;
        else if (j == "pending") x = LifecycleOperationStageStatus::PENDING;
        else if (j == "running") x = LifecycleOperationStageStatus::RUNNING;
        else if (j == "skipped") x = LifecycleOperationStageStatus::SKIPPED;
        else if (j == "succeeded") x = LifecycleOperationStageStatus::SUCCEEDED;
        else { throw std::runtime_error("Cannot deserialize to enumeration \"LifecycleOperationStageStatus\""); }
    }

    inline void to_json(json & j, const LifecycleOperationStageStatus & x) {
        switch (x) {
            case LifecycleOperationStageStatus::FAILED: j = "failed"; break;
            case LifecycleOperationStageStatus::PENDING: j = "pending"; break;
            case LifecycleOperationStageStatus::RUNNING: j = "running"; break;
            case LifecycleOperationStageStatus::SKIPPED: j = "skipped"; break;
            case LifecycleOperationStageStatus::SUCCEEDED: j = "succeeded"; break;
            default: throw std::runtime_error("Unexpected value in enumeration \"LifecycleOperationStageStatus\": " + std::to_string(static_cast<int>(x)));
        }
    }

    inline void from_json(const json & j, LifecycleOperationStatus & x) {
        if (j == "failed") x = LifecycleOperationStatus::FAILED;
        else if (j == "running") x = LifecycleOperationStatus::RUNNING;
        else if (j == "succeeded") x = LifecycleOperationStatus::SUCCEEDED;
        else { throw std::runtime_error("Cannot deserialize to enumeration \"LifecycleOperationStatus\""); }
    }

    inline void to_json(json & j, const LifecycleOperationStatus & x) {
        switch (x) {
            case LifecycleOperationStatus::FAILED: j = "failed"; break;
            case LifecycleOperationStatus::RUNNING: j = "running"; break;
            case LifecycleOperationStatus::SUCCEEDED: j = "succeeded"; break;
            default: throw std::runtime_error("Unexpected value in enumeration \"LifecycleOperationStatus\": " + std::to_string(static_cast<int>(x)));
        }
    }

    inline void from_json(const json & j, LifecycleOperationType & x) {
        if (j == "apply_config") x = LifecycleOperationType::APPLY_CONFIG;
        else if (j == "restart") x = LifecycleOperationType::RESTART;
        else if (j == "rollback_config") x = LifecycleOperationType::ROLLBACK_CONFIG;
        else if (j == "start") x = LifecycleOperationType::START;
        else if (j == "stop") x = LifecycleOperationType::STOP;
        else { throw std::runtime_error("Cannot deserialize to enumeration \"LifecycleOperationType\""); }
    }

    inline void to_json(json & j, const LifecycleOperationType & x) {
        switch (x) {
            case LifecycleOperationType::APPLY_CONFIG: j = "apply_config"; break;
            case LifecycleOperationType::RESTART: j = "restart"; break;
            case LifecycleOperationType::ROLLBACK_CONFIG: j = "rollback_config"; break;
            case LifecycleOperationType::START: j = "start"; break;
            case LifecycleOperationType::STOP: j = "stop"; break;
            default: throw std::runtime_error("Unexpected value in enumeration \"LifecycleOperationType\": " + std::to_string(static_cast<int>(x)));
        }
    }

    inline void from_json(const json & j, RuntimeState & x) {
        if (j == "applying") x = RuntimeState::APPLYING;
        else if (j == "broken") x = RuntimeState::BROKEN;
        else if (j == "restart_required") x = RuntimeState::RESTART_REQUIRED;
        else if (j == "running") x = RuntimeState::RUNNING;
        else if (j == "shutting_down") x = RuntimeState::SHUTTING_DOWN;
        else if (j == "starting") x = RuntimeState::STARTING;
        else if (j == "stopped") x = RuntimeState::STOPPED;
        else { throw std::runtime_error("Cannot deserialize to enumeration \"RuntimeState\""); }
    }

    inline void to_json(json & j, const RuntimeState & x) {
        switch (x) {
            case RuntimeState::APPLYING: j = "applying"; break;
            case RuntimeState::BROKEN: j = "broken"; break;
            case RuntimeState::RESTART_REQUIRED: j = "restart_required"; break;
            case RuntimeState::RUNNING: j = "running"; break;
            case RuntimeState::SHUTTING_DOWN: j = "shutting_down"; break;
            case RuntimeState::STARTING: j = "starting"; break;
            case RuntimeState::STOPPED: j = "stopped"; break;
            default: throw std::runtime_error("Unexpected value in enumeration \"RuntimeState\": " + std::to_string(static_cast<int>(x)));
        }
    }

    inline void from_json(const json & j, HealthResponseStatus & x) {
        if (j == "degraded") x = HealthResponseStatus::DEGRADED;
        else if (j == "running") x = HealthResponseStatus::RUNNING;
        else if (j == "stopped") x = HealthResponseStatus::STOPPED;
        else { throw std::runtime_error("Cannot deserialize to enumeration \"HealthResponseStatus\""); }
    }

    inline void to_json(json & j, const HealthResponseStatus & x) {
        switch (x) {
            case HealthResponseStatus::DEGRADED: j = "degraded"; break;
            case HealthResponseStatus::RUNNING: j = "running"; break;
            case HealthResponseStatus::STOPPED: j = "stopped"; break;
            default: throw std::runtime_error("Unexpected value in enumeration \"HealthResponseStatus\": " + std::to_string(static_cast<int>(x)));
        }
    }

    inline void from_json(const json & j, LifecycleOperationAcceptedResponseStatus & x) {
        if (j == "accepted") x = LifecycleOperationAcceptedResponseStatus::ACCEPTED;
        else { throw std::runtime_error("Cannot deserialize to enumeration \"LifecycleOperationAcceptedResponseStatus\""); }
    }

    inline void to_json(json & j, const LifecycleOperationAcceptedResponseStatus & x) {
        switch (x) {
            case LifecycleOperationAcceptedResponseStatus::ACCEPTED: j = "accepted"; break;
            default: throw std::runtime_error("Unexpected value in enumeration \"LifecycleOperationAcceptedResponseStatus\": " + std::to_string(static_cast<int>(x)));
        }
    }

    inline void from_json(const json & j, ExpectedAction & x) {
        if (j == "blackhole") x = ExpectedAction::BLACKHOLE;
        else if (j == "lookup") x = ExpectedAction::LOOKUP;
        else if (j == "unreachable") x = ExpectedAction::UNREACHABLE;
        else { throw std::runtime_error("Cannot deserialize to enumeration \"ExpectedAction\""); }
    }

    inline void to_json(json & j, const ExpectedAction & x) {
        switch (x) {
            case ExpectedAction::BLACKHOLE: j = "blackhole"; break;
            case ExpectedAction::LOOKUP: j = "lookup"; break;
            case ExpectedAction::UNREACHABLE: j = "unreachable"; break;
            default: throw std::runtime_error("Unexpected value in enumeration \"ExpectedAction\": " + std::to_string(static_cast<int>(x)));
        }
    }

    inline void from_json(const json & j, RoutingHealthErrorResponseOverall & x) {
        if (j == "error") x = RoutingHealthErrorResponseOverall::ERROR;
        else { throw std::runtime_error("Cannot deserialize to enumeration \"RoutingHealthErrorResponseOverall\""); }
    }

    inline void to_json(json & j, const RoutingHealthErrorResponseOverall & x) {
        switch (x) {
            case RoutingHealthErrorResponseOverall::ERROR: j = "error"; break;
            default: throw std::runtime_error("Unexpected value in enumeration \"RoutingHealthErrorResponseOverall\": " + std::to_string(static_cast<int>(x)));
        }
    }

    inline void from_json(const json & j, RoutingHealthResponseFirewallBackend & x) {
        if (j == "iptables") x = RoutingHealthResponseFirewallBackend::IPTABLES;
        else if (j == "nftables") x = RoutingHealthResponseFirewallBackend::NFTABLES;
        else { throw std::runtime_error("Cannot deserialize to enumeration \"RoutingHealthResponseFirewallBackend\""); }
    }

    inline void to_json(json & j, const RoutingHealthResponseFirewallBackend & x) {
        switch (x) {
            case RoutingHealthResponseFirewallBackend::IPTABLES: j = "iptables"; break;
            case RoutingHealthResponseFirewallBackend::NFTABLES: j = "nftables"; break;
            default: throw std::runtime_error("Unexpected value in enumeration \"RoutingHealthResponseFirewallBackend\": " + std::to_string(static_cast<int>(x)));
        }
    }

    inline void from_json(const json & j, RoutingHealthResponseOverall & x) {
        if (j == "degraded") x = RoutingHealthResponseOverall::DEGRADED;
        else if (j == "error") x = RoutingHealthResponseOverall::ERROR;
        else if (j == "ok") x = RoutingHealthResponseOverall::OK;
        else { throw std::runtime_error("Cannot deserialize to enumeration \"RoutingHealthResponseOverall\""); }
    }

    inline void to_json(json & j, const RoutingHealthResponseOverall & x) {
        switch (x) {
            case RoutingHealthResponseOverall::DEGRADED: j = "degraded"; break;
            case RoutingHealthResponseOverall::ERROR: j = "error"; break;
            case RoutingHealthResponseOverall::OK: j = "ok"; break;
            default: throw std::runtime_error("Unexpected value in enumeration \"RoutingHealthResponseOverall\": " + std::to_string(static_cast<int>(x)));
        }
    }

    inline void from_json(const json & j, Proto & x) {
        if (j == "other") x = Proto::OTHER;
        else if (j == "tcp") x = Proto::TCP;
        else if (j == "udp") x = Proto::UDP;
        else { throw std::runtime_error("Cannot deserialize to enumeration \"Proto\""); }
    }

    inline void to_json(json & j, const Proto & x) {
        switch (x) {
            case Proto::OTHER: j = "other"; break;
            case Proto::TCP: j = "tcp"; break;
            case Proto::UDP: j = "udp"; break;
            default: throw std::runtime_error("Unexpected value in enumeration \"Proto\": " + std::to_string(static_cast<int>(x)));
        }
    }

    inline void from_json(const json & j, RoutingTestSetWriteEvidenceStatus & x) {
        if (j == "not_tracked") x = RoutingTestSetWriteEvidenceStatus::NOT_TRACKED;
        else if (j == "no_record") x = RoutingTestSetWriteEvidenceStatus::NO_RECORD;
        else if (j == "recorded") x = RoutingTestSetWriteEvidenceStatus::RECORDED;
        else { throw std::runtime_error("Cannot deserialize to enumeration \"RoutingTestSetWriteEvidenceStatus\""); }
    }

    inline void to_json(json & j, const RoutingTestSetWriteEvidenceStatus & x) {
        switch (x) {
            case RoutingTestSetWriteEvidenceStatus::NOT_TRACKED: j = "not_tracked"; break;
            case RoutingTestSetWriteEvidenceStatus::NO_RECORD: j = "no_record"; break;
            case RoutingTestSetWriteEvidenceStatus::RECORDED: j = "recorded"; break;
            default: throw std::runtime_error("Unexpected value in enumeration \"RoutingTestSetWriteEvidenceStatus\": " + std::to_string(static_cast<int>(x)));
        }
    }

    inline void from_json(const json & j, RuntimeInterfaceInventoryStatusEnum & x) {
        if (j == "down") x = RuntimeInterfaceInventoryStatusEnum::DOWN;
        else if (j == "up") x = RuntimeInterfaceInventoryStatusEnum::UP;
        else { throw std::runtime_error("Cannot deserialize to enumeration \"RuntimeInterfaceInventoryStatusEnum\""); }
    }

    inline void to_json(json & j, const RuntimeInterfaceInventoryStatusEnum & x) {
        switch (x) {
            case RuntimeInterfaceInventoryStatusEnum::DOWN: j = "down"; break;
            case RuntimeInterfaceInventoryStatusEnum::UP: j = "up"; break;
            default: throw std::runtime_error("Unexpected value in enumeration \"RuntimeInterfaceInventoryStatusEnum\": " + std::to_string(static_cast<int>(x)));
        }
    }

    inline void from_json(const json & j, RuntimeInterfaceStatusEnum & x) {
        if (j == "active") x = RuntimeInterfaceStatusEnum::ACTIVE;
        else if (j == "backup") x = RuntimeInterfaceStatusEnum::BACKUP;
        else if (j == "degraded") x = RuntimeInterfaceStatusEnum::DEGRADED;
        else if (j == "unavailable") x = RuntimeInterfaceStatusEnum::UNAVAILABLE;
        else if (j == "unknown") x = RuntimeInterfaceStatusEnum::UNKNOWN;
        else { throw std::runtime_error("Cannot deserialize to enumeration \"RuntimeInterfaceStatusEnum\""); }
    }

    inline void to_json(json & j, const RuntimeInterfaceStatusEnum & x) {
        switch (x) {
            case RuntimeInterfaceStatusEnum::ACTIVE: j = "active"; break;
            case RuntimeInterfaceStatusEnum::BACKUP: j = "backup"; break;
            case RuntimeInterfaceStatusEnum::DEGRADED: j = "degraded"; break;
            case RuntimeInterfaceStatusEnum::UNAVAILABLE: j = "unavailable"; break;
            case RuntimeInterfaceStatusEnum::UNKNOWN: j = "unknown"; break;
            default: throw std::runtime_error("Unexpected value in enumeration \"RuntimeInterfaceStatusEnum\": " + std::to_string(static_cast<int>(x)));
        }
    }

    inline void from_json(const json & j, RuntimeOutboundStatusEnum & x) {
        if (j == "degraded") x = RuntimeOutboundStatusEnum::DEGRADED;
        else if (j == "healthy") x = RuntimeOutboundStatusEnum::HEALTHY;
        else if (j == "unavailable") x = RuntimeOutboundStatusEnum::UNAVAILABLE;
        else if (j == "unknown") x = RuntimeOutboundStatusEnum::UNKNOWN;
        else { throw std::runtime_error("Cannot deserialize to enumeration \"RuntimeOutboundStatusEnum\""); }
    }

    inline void to_json(json & j, const RuntimeOutboundStatusEnum & x) {
        switch (x) {
            case RuntimeOutboundStatusEnum::DEGRADED: j = "degraded"; break;
            case RuntimeOutboundStatusEnum::HEALTHY: j = "healthy"; break;
            case RuntimeOutboundStatusEnum::UNAVAILABLE: j = "unavailable"; break;
            case RuntimeOutboundStatusEnum::UNKNOWN: j = "unknown"; break;
            default: throw std::runtime_error("Unexpected value in enumeration \"RuntimeOutboundStatusEnum\": " + std::to_string(static_cast<int>(x)));
        }
    }

    inline void from_json(const json & j, StatusEventInterfacesType & x) {
        if (j == "interfaces") x = StatusEventInterfacesType::INTERFACES;
        else { throw std::runtime_error("Cannot deserialize to enumeration \"StatusEventInterfacesType\""); }
    }

    inline void to_json(json & j, const StatusEventInterfacesType & x) {
        switch (x) {
            case StatusEventInterfacesType::INTERFACES: j = "interfaces"; break;
            default: throw std::runtime_error("Unexpected value in enumeration \"StatusEventInterfacesType\": " + std::to_string(static_cast<int>(x)));
        }
    }

    inline void from_json(const json & j, StatusEventOutboundsType & x) {
        if (j == "outbounds") x = StatusEventOutboundsType::OUTBOUNDS;
        else { throw std::runtime_error("Cannot deserialize to enumeration \"StatusEventOutboundsType\""); }
    }

    inline void to_json(json & j, const StatusEventOutboundsType & x) {
        switch (x) {
            case StatusEventOutboundsType::OUTBOUNDS: j = "outbounds"; break;
            default: throw std::runtime_error("Unexpected value in enumeration \"StatusEventOutboundsType\": " + std::to_string(static_cast<int>(x)));
        }
    }

    inline void from_json(const json & j, StatusEventServiceType & x) {
        if (j == "service") x = StatusEventServiceType::SERVICE;
        else { throw std::runtime_error("Cannot deserialize to enumeration \"StatusEventServiceType\""); }
    }

    inline void to_json(json & j, const StatusEventServiceType & x) {
        switch (x) {
            case StatusEventServiceType::SERVICE: j = "service"; break;
            default: throw std::runtime_error("Unexpected value in enumeration \"StatusEventServiceType\": " + std::to_string(static_cast<int>(x)));
        }
    }

    inline void from_json(const json & j, StatusEventSnapshotType & x) {
        if (j == "snapshot") x = StatusEventSnapshotType::SNAPSHOT;
        else { throw std::runtime_error("Cannot deserialize to enumeration \"StatusEventSnapshotType\""); }
    }

    inline void to_json(json & j, const StatusEventSnapshotType & x) {
        switch (x) {
            case StatusEventSnapshotType::SNAPSHOT: j = "snapshot"; break;
            default: throw std::runtime_error("Unexpected value in enumeration \"StatusEventSnapshotType\": " + std::to_string(static_cast<int>(x)));
        }
    }
}
}
