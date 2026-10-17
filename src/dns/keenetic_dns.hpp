#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef KEEN_PBR3_TESTING
#include <functional>
#endif

namespace keen_pbr3 {

class KeeneticDnsError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct KeeneticStaticDnsEntry {
    std::string domain;
    std::string address;
};

struct KeeneticDnsUpstreamEntry {
    std::string address;
    std::string kind;
    std::string target;
};

// One upstream of a Keenetic DNS proxy group, as shown to the user.
struct KeeneticDnsProxyUpstream {
    std::string kind;       // DoH | DoT | Plain
    std::string target;     // URI for DoH/DoT, address for Plain
};

// Servers a group uses only for the names under one domain
// ("dns_server = 85.21.192.5 beeline.ru"); not part of its upstream list.
struct KeeneticDnsDomainServers {
    std::string domain;
    std::vector<std::string> addresses;
};

// A Keenetic DNS proxy group ("System", "Policy0", ...).
struct KeeneticDnsProxy {
    std::string name;
    std::string description;  // from the Keenetic policy; empty when unknown
    uint16_t listen_port{0};  // dns_udp_port of the group's own DNS proxy
    std::vector<KeeneticDnsProxyUpstream> upstreams;
    std::vector<KeeneticDnsDomainServers> domain_servers;  // in RCI order
    // Interfaces the policy routes through (enabled "permit" entries); empty for System.
    std::vector<std::string> route_interfaces;
};

struct KeeneticDnsSnapshot {
    // System group only: the addresses keen-pbr uses directly.
    std::vector<std::string> addresses;
    std::vector<KeeneticDnsUpstreamEntry> upstreams;
    std::vector<KeeneticStaticDnsEntry> static_entries;
    // Every DNS proxy group, in RCI order.
    std::vector<KeeneticDnsProxy> proxies;
};

inline bool operator==(const KeeneticStaticDnsEntry& a, const KeeneticStaticDnsEntry& b) {
    return a.domain == b.domain && a.address == b.address;
}
inline bool operator==(const KeeneticDnsUpstreamEntry& a, const KeeneticDnsUpstreamEntry& b) {
    return a.address == b.address && a.kind == b.kind && a.target == b.target;
}
inline bool operator==(const KeeneticDnsProxyUpstream& a, const KeeneticDnsProxyUpstream& b) {
    return a.kind == b.kind && a.target == b.target;
}
inline bool operator==(const KeeneticDnsDomainServers& a, const KeeneticDnsDomainServers& b) {
    return a.domain == b.domain && a.addresses == b.addresses;
}
inline bool operator==(const KeeneticDnsProxy& a, const KeeneticDnsProxy& b) {
    return a.name == b.name && a.description == b.description &&
           a.listen_port == b.listen_port && a.upstreams == b.upstreams &&
           a.domain_servers == b.domain_servers && a.route_interfaces == b.route_interfaces;
}
inline bool operator==(const KeeneticDnsSnapshot& a, const KeeneticDnsSnapshot& b) {
    return a.addresses == b.addresses && a.upstreams == b.upstreams &&
           a.static_entries == b.static_entries && a.proxies == b.proxies;
}

// The Keenetic DNS proxy group that holds the router's regular DNS settings.
constexpr const char* kKeeneticSystemPolicy = "System";

// True when `policy` (a dns.servers keenetic_policy value) names the System
// group: unset and "System" both do.
inline bool is_keenetic_system_policy(const std::string& policy) {
    return policy.empty() || policy == kKeeneticSystemPolicy;
}

// RCI endpoint used as source of truth for the built-in DNS proxy:
// GET http://127.0.0.1:79/rci/show/dns-proxy
//
// We read proxy-status entry with proxy-name == "System" and only consider
// unscoped "dns_server = ..." directives. Domain-scoped entries are ignored.
// When the System policy has unscoped encrypted resolvers, we use all of them
// in order. Otherwise we fall back to all unscoped plaintext resolvers.
KeeneticDnsSnapshot extract_keenetic_dns_snapshot_from_rci(const std::string& response_body);

// Every other proxy group (Policy0, ...) is read as well: its listen port
// (dns_udp_port) and upstreams, for display and for forwarding to it.
//
// Fills KeeneticDnsProxy::description and route_interfaces from the policy listing
// (GET http://127.0.0.1:79/rci/show/rc/ip/policy), a JSON object keyed by
// policy name. Throws KeeneticDnsError when the body is not that.
void apply_keenetic_policy_descriptions(KeeneticDnsSnapshot& snapshot,
                                        const std::string& policy_listing_body);

enum class KeeneticDnsRefreshStatus : uint8_t {
    UNCHANGED,
    UPDATED,
    FETCH_FAILED_USED_CACHE,
    FETCH_FAILED_NO_CACHE,
};

struct KeeneticDnsRefreshResult {
    KeeneticDnsRefreshStatus status{KeeneticDnsRefreshStatus::FETCH_FAILED_NO_CACHE};
    std::vector<std::string> addresses;
    std::string error;
};

// The cached snapshot with the time it was read and the outcome of the call
// that produced this view.
struct KeeneticDnsCacheView {
    KeeneticDnsRefreshStatus status{KeeneticDnsRefreshStatus::FETCH_FAILED_NO_CACHE};
    std::optional<KeeneticDnsSnapshot> snapshot;
    std::int64_t fetched_at_unix{0};  // wall clock seconds of the snapshot; 0 without one
    std::string error;                // why the fetch failed; empty on success
};

// Re-reads the DNS proxy groups when the cache is older than 5 minutes or
// force_refresh is set. A failed fetch keeps the cached snapshot (and returns
// it together with the error). Every attempt, failed or not, counts for
// keenetic_dns_last_attempt().
KeeneticDnsCacheView refresh_keenetic_dns_cache(bool force_refresh = false);

// The cached snapshot without fetching; nullopt before the first successful fetch.
std::optional<KeeneticDnsCacheView> peek_keenetic_dns_cache();

// When the RCI was last queried (successfully or not); nullopt if never.
std::optional<std::chrono::steady_clock::time_point> keenetic_dns_last_attempt();

// Where a dns.servers keenetic_policy points to.
struct KeeneticPolicyResolution {
    std::vector<std::string> addresses;  // one "127.0.0.1:<port>" for a policy; the System upstreams for System
    bool missing{false};                 // the policy is not in the RCI (or has no DNS port)
};

// System (or empty): its upstreams, as resolve_keenetic_dns_addresses().
// Another group: the address of that group's DNS proxy, or missing.
// Throws KeeneticDnsError only when the RCI cannot be read and nothing is cached.
KeeneticPolicyResolution resolve_keenetic_dns_policy(const std::string& policy);

// Refresh cached built-in DNS server addresses via Keenetic RCI.
// When a previously cached value exists, fetch failures keep the cache intact
// and return FETCH_FAILED_USED_CACHE with the cached addresses.
KeeneticDnsRefreshResult refresh_keenetic_dns_address_cache(bool force_refresh = false);

// Resolve built-in DNS server addresses via Keenetic RCI.
// Uses a 5-minute cache, attempts a refetch when the cache is stale or when
// force_refresh=true, and falls back to the previously cached value on fetch
// failures when possible.
// Throws KeeneticDnsError only when no usable cached value exists.
std::vector<std::string> resolve_keenetic_dns_addresses(bool force_refresh = false);

// Return the cached static_a/static_aaaa entries extracted from Keenetic RCI.
// Returns an empty vector when no Keenetic DNS snapshot has been cached yet.
std::vector<KeeneticStaticDnsEntry> get_keenetic_static_dns_entries();
std::vector<std::string> get_keenetic_dns_addresses();
std::vector<KeeneticDnsUpstreamEntry> get_keenetic_dns_upstreams();

#ifdef KEEN_PBR3_TESTING
using KeeneticDnsFetchFn = std::function<std::string()>;
using KeeneticDnsNowFn = std::function<std::chrono::steady_clock::time_point()>;

void set_keenetic_dns_fetcher_for_tests(KeeneticDnsFetchFn fetcher);
// Serves /rci/show/rc/ip/policy; the default stub fails like an unreachable RCI.
void set_keenetic_policy_fetcher_for_tests(KeeneticDnsFetchFn fetcher);
void set_keenetic_dns_now_fn_for_tests(KeeneticDnsNowFn now_fn);
void reset_keenetic_dns_test_state();
#endif

} // namespace keen_pbr3
