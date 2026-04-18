#include "keenetic_dns.hpp"

#include "dns_server.hpp"
#include "../http/http_client.hpp"

#include <nlohmann/json.hpp>

#include <cctype>
#include <chrono>
#include <mutex>
#include <sstream>
#include <functional>
#include <chrono>

namespace keen_pbr3 {

namespace {

constexpr const char* kRciDnsProxyEndpoint = "http://127.0.0.1:79/rci/show/dns-proxy";
constexpr auto kKeeneticDnsCacheTtl = std::chrono::minutes(5);

struct KeeneticDnsCacheState {
    std::optional<std::string> address;
    std::chrono::steady_clock::time_point fetched_at{};
};

KeeneticDnsCacheState& keenetic_dns_cache_state() {
    static KeeneticDnsCacheState state;
    return state;
}

std::mutex& keenetic_dns_cache_mutex() {
    static std::mutex mutex;
    return mutex;
}

using FetchFn = std::function<std::string()>;
using NowFn = std::function<std::chrono::steady_clock::time_point()>;

FetchFn& keenetic_dns_fetch_fn() {
    static FetchFn fetch_fn = []() {
        HttpClient client;
        client.set_timeout(std::chrono::seconds(3));
        return client.download(kRciDnsProxyEndpoint);
    };
    return fetch_fn;
}

NowFn& keenetic_dns_now_fn() {
    static NowFn now_fn = []() {
        return std::chrono::steady_clock::now();
    };
    return now_fn;
}

bool is_cache_fresh(const KeeneticDnsCacheState& state,
                    const std::chrono::steady_clock::time_point now) {
    return state.address.has_value() && now - state.fetched_at < kKeeneticDnsCacheTtl;
}

std::string trim_copy(const std::string& s) {
    size_t begin = 0;
    while (begin < s.size() && std::isspace(static_cast<unsigned char>(s[begin]))) {
        ++begin;
    }
    size_t end = s.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
        --end;
    }
    return s.substr(begin, end - begin);
}

std::string extract_address_from_dns_server_line(const std::string& line) {
    constexpr const char* kPrefix = "dns_server = ";
    if (line.rfind(kPrefix, 0) != 0) {
        return "";
    }

    std::string rest = trim_copy(line.substr(std::char_traits<char>::length(kPrefix)));
    const auto comment_pos = rest.find('#');
    if (comment_pos != std::string::npos) {
        rest = trim_copy(rest.substr(0, comment_pos));
    }
    if (rest.empty()) {
        return "";
    }

    const auto first_space = rest.find_first_of(" \t");
    if (first_space != std::string::npos) {
        rest = rest.substr(0, first_space);
    }
    return trim_copy(rest);
}

} // namespace

std::string extract_keenetic_dns_address_from_rci(const std::string& response_body) {
    using json = nlohmann::json;

    json doc;
    try {
        doc = json::parse(response_body);
    } catch (const std::exception& e) {
        throw KeeneticDnsError(std::string("Failed to parse RCI JSON response: ") + e.what());
    }

    const auto status_it = doc.find("proxy-status");
    if (status_it == doc.end() || !status_it->is_array()) {
        throw KeeneticDnsError(
            "RCI response missing 'proxy-status' array (endpoint: /rci/show/dns-proxy)");
    }

    for (const auto& entry : *status_it) {
        if (!entry.is_object()) {
            continue;
        }

        const auto name_it = entry.find("proxy-name");
        if (name_it == entry.end() || !name_it->is_string()) {
            continue;
        }
        if (name_it->get<std::string>() != "System") {
            continue;
        }

        const auto cfg_it = entry.find("proxy-config");
        if (cfg_it == entry.end() || !cfg_it->is_string()) {
            throw KeeneticDnsError(
                "RCI response has 'System' DNS proxy but missing string field 'proxy-config'");
        }

        std::istringstream in(cfg_it->get<std::string>());
        std::string line;
        while (std::getline(in, line)) {
            const std::string address = extract_address_from_dns_server_line(trim_copy(line));
            if (address.empty()) {
                continue;
            }

            try {
                (void)parse_dns_address_str(address);
            } catch (const DnsError& e) {
                throw KeeneticDnsError(
                    "RCI returned invalid dns_server address '" + address + "': " + e.what());
            }
            return address;
        }

        throw KeeneticDnsError(
            "Built-in DNS proxy appears disabled or has no 'dns_server = ...' directives in System policy");
    }

    throw KeeneticDnsError(
        "RCI response does not contain DNS proxy policy 'System' (endpoint: /rci/show/dns-proxy)");
}

KeeneticDnsRefreshResult refresh_keenetic_dns_address_cache(bool force_refresh) {
#ifdef USE_KEENETIC_API
    std::lock_guard<std::mutex> lock(keenetic_dns_cache_mutex());
    KeeneticDnsCacheState& cache = keenetic_dns_cache_state();
    const auto now = keenetic_dns_now_fn()();

    if (!force_refresh && is_cache_fresh(cache, now)) {
        return {
            KeeneticDnsRefreshStatus::UNCHANGED,
            cache.address,
            ""
        };
    }

    try {
        const std::string response = keenetic_dns_fetch_fn()();
        const std::string address = extract_keenetic_dns_address_from_rci(response);
        const bool changed = !cache.address.has_value() || *cache.address != address;
        cache.address = address;
        cache.fetched_at = now;
        return {
            changed ? KeeneticDnsRefreshStatus::UPDATED
                    : KeeneticDnsRefreshStatus::UNCHANGED,
            cache.address,
            ""
        };
    } catch (const HttpError& e) {
        const std::string error =
            "Failed to query Keenetic RCI endpoint /rci/show/dns-proxy: " +
            std::string(e.what());
        if (cache.address.has_value()) {
            return {KeeneticDnsRefreshStatus::FETCH_FAILED_USED_CACHE, cache.address, error};
        }
        return {KeeneticDnsRefreshStatus::FETCH_FAILED_NO_CACHE, std::nullopt, error};
    } catch (const KeeneticDnsError& e) {
        if (cache.address.has_value()) {
            return {
                KeeneticDnsRefreshStatus::FETCH_FAILED_USED_CACHE,
                cache.address,
                e.what()
            };
        }
        return {
            KeeneticDnsRefreshStatus::FETCH_FAILED_NO_CACHE,
            std::nullopt,
            e.what()
        };
    }
#else
    (void)force_refresh;
    return {
        KeeneticDnsRefreshStatus::FETCH_FAILED_NO_CACHE,
        std::nullopt,
        "DNS server type 'keenetic' requires build with USE_KEENETIC_API=ON"
    };
#endif
}

std::string resolve_keenetic_dns_address(bool force_refresh) {
    const KeeneticDnsRefreshResult result = refresh_keenetic_dns_address_cache(force_refresh);
    if (result.address.has_value()) {
        return *result.address;
    }
    throw KeeneticDnsError(result.error);
}

#ifdef KEEN_PBR3_TESTING
void set_keenetic_dns_fetcher_for_tests(KeeneticDnsFetchFn fetcher) {
    std::lock_guard<std::mutex> lock(keenetic_dns_cache_mutex());
    keenetic_dns_fetch_fn() = std::move(fetcher);
}

void set_keenetic_dns_now_fn_for_tests(KeeneticDnsNowFn now_fn) {
    std::lock_guard<std::mutex> lock(keenetic_dns_cache_mutex());
    keenetic_dns_now_fn() = std::move(now_fn);
}

void reset_keenetic_dns_test_state() {
    std::lock_guard<std::mutex> lock(keenetic_dns_cache_mutex());
    keenetic_dns_cache_state() = KeeneticDnsCacheState{};
    keenetic_dns_fetch_fn() = []() {
        HttpClient client;
        client.set_timeout(std::chrono::seconds(3));
        return client.download(kRciDnsProxyEndpoint);
    };
    keenetic_dns_now_fn() = []() {
        return std::chrono::steady_clock::now();
    };
}
#endif

} // namespace keen_pbr3
