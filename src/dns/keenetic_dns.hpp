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

// One upstream of the Keenetic built-in DNS, as shown to the user. kind and
// target together identify it: the router's local port for it can change.
struct KeeneticDnsProxyUpstream {
    std::string kind;       // DoH | DoT | Plain
    std::string target;     // DoH: URI; DoT: "address@name" or URI; Plain: address
};

// Servers the proxy uses only for the names under one domain
// ("dns_server = 85.21.192.5 beeline.ru"); not part of its upstream list.
struct KeeneticDnsDomainServers {
    std::string domain;
    std::vector<std::string> addresses;
};

// The Keenetic built-in DNS proxy ("System").
struct KeeneticDnsProxy {
    std::string name;
    std::vector<KeeneticDnsProxyUpstream> upstreams;
    std::vector<KeeneticDnsDomainServers> domain_servers;  // in RCI order
};

struct KeeneticDnsSnapshot {
    // System group only: the addresses keen-pbr uses directly.
    std::vector<std::string> addresses;
    std::vector<KeeneticDnsUpstreamEntry> upstreams;
    std::vector<KeeneticStaticDnsEntry> static_entries;
    // The System proxy (the only entry).
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
    return a.name == b.name && a.upstreams == b.upstreams &&
           a.domain_servers == b.domain_servers;
}
inline bool operator==(const KeeneticDnsSnapshot& a, const KeeneticDnsSnapshot& b) {
    return a.addresses == b.addresses && a.upstreams == b.upstreams &&
           a.static_entries == b.static_entries && a.proxies == b.proxies;
}

// The Keenetic DNS proxy that holds the router's regular DNS settings.
constexpr const char* kKeeneticSystemPolicy = "System";

// The identifying target of a System upstream: its target, or the address of
// a plain one.
inline std::string keenetic_upstream_target(const KeeneticDnsUpstreamEntry& upstream) {
    return upstream.target.empty() ? upstream.address : upstream.target;
}

// RCI endpoint used as source of truth for the built-in DNS proxy:
// GET http://127.0.0.1:79/rci/show/dns-proxy
//
// We read proxy-status entry with proxy-name == "System" and only consider
// unscoped "dns_server = ..." directives as upstreams. Domain-scoped entries
// are kept apart (KeeneticDnsProxy::domain_servers).
// When the System policy has unscoped encrypted resolvers, we use all of them
// in order. Otherwise we fall back to all unscoped plaintext resolvers.
// Every encrypted upstream is a local stub ("127.0.0.1:<port>") named by the
// comment of its line; the port can change, the comment's protocol and target
// do not. The other proxies (Policy0, ...) are ignored.
KeeneticDnsSnapshot extract_keenetic_dns_snapshot_from_rci(const std::string& response_body);

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

// Re-reads the DNS proxy when the cache is older than 5 minutes or
// force_refresh is set. A failed fetch keeps the cached snapshot (and returns
// it together with the error). Every attempt, failed or not, counts for
// keenetic_dns_last_attempt().
KeeneticDnsCacheView refresh_keenetic_dns_cache(bool force_refresh = false);

// The cached snapshot without fetching; nullopt before the first successful fetch.
std::optional<KeeneticDnsCacheView> peek_keenetic_dns_cache();

// When the RCI was last queried (successfully or not); nullopt if never.
std::optional<std::chrono::steady_clock::time_point> keenetic_dns_last_attempt();

// Current addresses of the upstreams a dns.servers keenetic_upstreams names.
struct KeeneticUpstreamResolution {
    std::vector<std::string> addresses;           // "127.0.0.1:<port>" etc., in selection order
    std::vector<KeeneticDnsProxyUpstream> missing;  // selected, but not in System any more
};

// Matches `selected` (protocol + target) against the System upstreams of
// `snapshot`. An empty selection means every upstream, in router order.
KeeneticUpstreamResolution resolve_keenetic_upstreams(
    const KeeneticDnsSnapshot& snapshot,
    const std::vector<KeeneticDnsProxyUpstream>& selected);

// resolve_keenetic_upstreams() on the cached snapshot (read when stale).
// Throws KeeneticDnsError only when the RCI cannot be read and nothing is cached.
KeeneticUpstreamResolution resolve_keenetic_dns_upstreams(
    const std::vector<KeeneticDnsProxyUpstream>& selected);

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
void set_keenetic_dns_now_fn_for_tests(KeeneticDnsNowFn now_fn);
void reset_keenetic_dns_test_state();
#endif

} // namespace keen_pbr3
