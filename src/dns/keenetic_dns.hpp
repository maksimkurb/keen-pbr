#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>

namespace keen_pbr3 {

class KeeneticDnsError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// RCI endpoint used as source of truth for the built-in DNS proxy:
// GET http://127.0.0.1:79/rci/show/dns-proxy
//
// We read proxy-status entry with proxy-name == "System" and parse the first
// "dns_server = ..." directive from proxy-config.
std::string extract_keenetic_dns_address_from_rci(const std::string& response_body);

enum class KeeneticDnsRefreshStatus : uint8_t {
    UNCHANGED,
    UPDATED,
    FETCH_FAILED_USED_CACHE,
    FETCH_FAILED_NO_CACHE,
};

struct KeeneticDnsRefreshResult {
    KeeneticDnsRefreshStatus status{KeeneticDnsRefreshStatus::FETCH_FAILED_NO_CACHE};
    std::optional<std::string> address;
    std::string error;
};

// Refresh cached built-in DNS server address via Keenetic RCI.
// When a previously cached value exists, fetch failures keep the cache intact
// and return FETCH_FAILED_USED_CACHE with the cached address.
KeeneticDnsRefreshResult refresh_keenetic_dns_address_cache(bool force_refresh = false);

// Resolve built-in DNS server address via Keenetic RCI.
// Uses a 5-minute cache, attempts a refetch when the cache is stale or when
// force_refresh=true, and falls back to the previously cached value on fetch
// failures when possible.
// Throws KeeneticDnsError only when no usable cached value exists.
std::string resolve_keenetic_dns_address(bool force_refresh = false);

#ifdef KEEN_PBR3_TESTING
using KeeneticDnsFetchFn = std::function<std::string()>;
using KeeneticDnsNowFn = std::function<std::chrono::steady_clock::time_point()>;

void set_keenetic_dns_fetcher_for_tests(KeeneticDnsFetchFn fetcher);
void set_keenetic_dns_now_fn_for_tests(KeeneticDnsNowFn now_fn);
void reset_keenetic_dns_test_state();
#endif

} // namespace keen_pbr3
