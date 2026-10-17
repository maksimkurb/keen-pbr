#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef KEEN_PBR3_TESTING
#include <functional>
#include <chrono>
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

struct KeeneticDnsSnapshot {
    std::vector<std::string> addresses;
    std::vector<KeeneticDnsUpstreamEntry> upstreams;
    std::vector<KeeneticStaticDnsEntry> static_entries;
};

// RCI endpoint used as source of truth for the built-in DNS proxy:
// GET http://127.0.0.1:79/rci/show/dns-proxy
//
// We read proxy-status entry with proxy-name == "System" and only consider
// unscoped "dns_server = ..." directives. Domain-scoped entries are ignored.
// When the System policy has unscoped encrypted resolvers, we use all of them
// in order. Otherwise we fall back to all unscoped plaintext resolvers.
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
