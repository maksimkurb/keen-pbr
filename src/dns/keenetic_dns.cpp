#include "keenetic_dns.hpp"

#include "dns_server.hpp"
#include "../http/http_client.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <iterator>
#include <chrono>
#include <functional>
#include <mutex>
#include <optional>
#include <sstream>
#include <vector>

namespace keen_pbr3 {

namespace {

constexpr const char* kRciDnsProxyEndpoint = "http://127.0.0.1:79/rci/show/dns-proxy";
constexpr auto kKeeneticDnsCacheTtl = std::chrono::minutes(5);

struct KeeneticDnsCacheState {
    std::optional<KeeneticDnsSnapshot> snapshot;
    std::chrono::steady_clock::time_point fetched_at{};
    std::int64_t fetched_at_unix{0};
    std::optional<std::chrono::steady_clock::time_point> last_attempt;
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
    return state.snapshot.has_value() && now - state.fetched_at < kKeeneticDnsCacheTtl;
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

struct ParsedDnsServerLine {
    std::string address;
    bool has_specific_domains{false};
    std::string domain;
    bool is_encrypted{false};
    std::string kind;
    std::string target;
};

struct ParsedStaticDnsLine {
    std::string domain;
    std::string address;
};

ParsedDnsServerLine parse_dns_server_line(const std::string& line) {
    constexpr const char* kPrefix = "dns_server = ";
    if (line.rfind(kPrefix, 0) != 0) {
        return {};
    }

    const std::string after_prefix =
        trim_copy(line.substr(std::char_traits<char>::length(kPrefix)));
    std::string rest = after_prefix;
    std::string comment;
    const auto comment_pos = rest.find('#');
    if (comment_pos != std::string::npos) {
        comment = trim_copy(rest.substr(comment_pos + 1));
        rest = trim_copy(rest.substr(0, comment_pos));
    }
    if (rest.empty()) {
        return {};
    }

    ParsedDnsServerLine parsed;
    const auto first_space = rest.find_first_of(" \t");
    if (first_space == std::string::npos) {
        parsed.address = trim_copy(rest);
    } else {
        parsed.address = trim_copy(rest.substr(0, first_space));
        const std::string suffix = trim_copy(rest.substr(first_space + 1));
        parsed.has_specific_domains = !suffix.empty() && suffix != ".";
        // A single domain; a list of them is not reported.
        if (parsed.has_specific_domains && suffix.find_first_of(" \t") == std::string::npos) {
            parsed.domain = suffix;
        }
    }
    if (parsed.address.empty()) {
        return {};
    }

    if (comment.rfind("https://", 0) == 0) {
        parsed.is_encrypted = true;
        parsed.kind = "DoH";
        const auto suffix_pos = comment.find('@');
        parsed.target = comment.substr(0, suffix_pos);
    } else if (comment.rfind("tls://", 0) == 0) {
        parsed.is_encrypted = true;
        parsed.kind = "DoT";
        const auto suffix_pos = comment.find('@');
        parsed.target = comment.substr(0, suffix_pos);
    } else if (comment.rfind("tls.", 0) == 0 ||
               (comment.find('@') != std::string::npos &&
                comment.find("://") == std::string::npos)) {
        parsed.is_encrypted = true;
        parsed.kind = "DoT";
        // "8.8.8.8@dns.google": the address is part of the identity, two
        // upstreams can share a name.
        parsed.target = comment;
    } else {
        parsed.kind = "Plain";
    }
    return parsed;
}

ParsedStaticDnsLine parse_static_dns_line(const std::string& line) {
    constexpr const char* kStaticAPrefix = "static_a = ";
    constexpr const char* kStaticAAAAPrefix = "static_aaaa = ";

    std::string rest;
    if (line.rfind(kStaticAPrefix, 0) == 0) {
        rest = trim_copy(line.substr(std::char_traits<char>::length(kStaticAPrefix)));
    } else if (line.rfind(kStaticAAAAPrefix, 0) == 0) {
        rest = trim_copy(line.substr(std::char_traits<char>::length(kStaticAAAAPrefix)));
    } else {
        return {};
    }

    if (rest.empty()) {
        return {};
    }

    std::istringstream parts(rest);
    ParsedStaticDnsLine parsed;
    if (!(parts >> parsed.domain >> parsed.address)) {
        return {};
    }
    return parsed;
}

bool is_supported_static_dns_domain(const std::string& domain) {
    if (domain.empty() || domain.size() > 255) {
        return false;
    }
    if (domain == "*") {
        return false;
    }
    if (domain.size() >= 2 && domain[0] == '*' && domain[1] == '.') {
        return domain.size() > 2;
    }
    return true;
}

bool looks_like_ipv6_dns_server_address(const std::string& address) {
    if (address.empty()) {
        return false;
    }
    if (address.front() == '[') {
        return true;
    }

    size_t colon_count = 0;
    for (char c : address) {
        if (c == ':') {
            ++colon_count;
            if (colon_count > 1) {
                return true;
            }
        }
    }
    return false;
}

std::vector<ParsedDnsServerLine> collect_selected_keenetic_dns_servers(
    const std::vector<ParsedDnsServerLine>& unscoped_plaintext_servers,
    const std::vector<ParsedDnsServerLine>& unscoped_encrypted_servers) {
    if (!unscoped_encrypted_servers.empty()) {
        return unscoped_encrypted_servers;
    }
    return unscoped_plaintext_servers;
}

struct ParsedProxyConfig {
    std::vector<ParsedDnsServerLine> unscoped_plaintext_servers;
    std::vector<ParsedDnsServerLine> unscoped_encrypted_servers;
    std::vector<KeeneticStaticDnsEntry> static_entries;
    std::vector<KeeneticDnsDomainServers> domain_servers;
};

// Reads the System proxy-config text; invalid addresses are rejected.
ParsedProxyConfig parse_proxy_config(const std::string& config) {
    ParsedProxyConfig parsed;
    std::istringstream in(config);
    std::string line;
    while (std::getline(in, line)) {
        const std::string trimmed = trim_copy(line);

        const ParsedStaticDnsLine static_parsed = parse_static_dns_line(trimmed);
        if (!static_parsed.domain.empty() || !static_parsed.address.empty()) {
            if (!is_supported_static_dns_domain(static_parsed.domain)) {
                continue;
            }
            try {
                (void)parse_dns_address_str(static_parsed.address);
            } catch (const DnsError& e) {
                throw KeeneticDnsError(
                    "RCI returned invalid static dns address '" + static_parsed.address + "': " + e.what());
            }
            parsed.static_entries.push_back({static_parsed.domain, static_parsed.address});
            continue;
        }

        ParsedDnsServerLine server = parse_dns_server_line(trimmed);
        if (server.address.empty()) {
            continue;
        }
        if (looks_like_ipv6_dns_server_address(server.address)) {
            continue;
        }

        try {
            (void)parse_dns_address_str(server.address);
        } catch (const DnsError& e) {
            throw KeeneticDnsError(
                "RCI returned invalid dns_server address '" + server.address + "': " + e.what());
        }

        if (server.has_specific_domains) {
            if (!server.domain.empty()) {
                auto group = std::find_if(
                    parsed.domain_servers.begin(), parsed.domain_servers.end(),
                    [&](const KeeneticDnsDomainServers& entry) {
                        return entry.domain == server.domain;
                    });
                if (group == parsed.domain_servers.end()) {
                    parsed.domain_servers.push_back({server.domain, {}});
                    group = std::prev(parsed.domain_servers.end());
                }
                group->addresses.push_back(server.address);
            }
            continue;
        }
        if (server.is_encrypted) {
            parsed.unscoped_encrypted_servers.push_back(std::move(server));
        } else {
            parsed.unscoped_plaintext_servers.push_back(std::move(server));
        }
    }
    return parsed;
}

} // namespace

KeeneticDnsSnapshot extract_keenetic_dns_snapshot_from_rci(const std::string& response_body) {
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

    KeeneticDnsSnapshot snapshot;
    bool system_found = false;
    std::vector<ParsedDnsServerLine> system_servers;
    for (const auto& entry : *status_it) {
        if (!entry.is_object()) {
            continue;
        }

        const auto name_it = entry.find("proxy-name");
        if (name_it == entry.end() || !name_it->is_string()) {
            continue;
        }
        const std::string name = name_it->get<std::string>();
        if (name != kKeeneticSystemPolicy || system_found) {
            continue;
        }

        const auto cfg_it = entry.find("proxy-config");
        if (cfg_it == entry.end() || !cfg_it->is_string()) {
            throw KeeneticDnsError(
                "RCI response has 'System' DNS proxy but missing string field 'proxy-config'");
        }

        ParsedProxyConfig parsed = parse_proxy_config(cfg_it->get<std::string>());
        const std::vector<ParsedDnsServerLine> selected = collect_selected_keenetic_dns_servers(
            parsed.unscoped_plaintext_servers, parsed.unscoped_encrypted_servers);

        KeeneticDnsProxy proxy;
        proxy.name = name;
        proxy.upstreams.reserve(selected.size());
        for (const auto& server : selected) {
            proxy.upstreams.push_back(
                {server.kind, server.target.empty() ? server.address : server.target});
        }
        proxy.domain_servers = std::move(parsed.domain_servers);
        snapshot.proxies.push_back(std::move(proxy));

        system_found = true;
        system_servers = selected;
        snapshot.static_entries = std::move(parsed.static_entries);
    }

    if (!system_found) {
        throw KeeneticDnsError(
            "RCI response does not contain DNS proxy policy 'System' (endpoint: /rci/show/dns-proxy)");
    }
    if (system_servers.empty()) {
        throw KeeneticDnsError(
            "Built-in DNS proxy appears disabled or has no unscoped 'dns_server = ...' directives in System policy");
    }
    snapshot.addresses.reserve(system_servers.size());
    snapshot.upstreams.reserve(system_servers.size());
    for (const auto& server : system_servers) {
        snapshot.addresses.push_back(server.address);
        snapshot.upstreams.push_back({server.address, server.kind, server.target});
    }
    return snapshot;
}

KeeneticDnsCacheView refresh_keenetic_dns_cache(bool force_refresh) {
#ifdef USE_KEENETIC_API
    std::lock_guard<std::mutex> lock(keenetic_dns_cache_mutex());
    KeeneticDnsCacheState& cache = keenetic_dns_cache_state();
    const auto now = keenetic_dns_now_fn()();

    const auto view_of = [&cache](KeeneticDnsRefreshStatus status, std::string error) {
        KeeneticDnsCacheView view;
        view.status = status;
        view.snapshot = cache.snapshot;
        view.fetched_at_unix = cache.snapshot.has_value() ? cache.fetched_at_unix : 0;
        view.error = std::move(error);
        return view;
    };

    if (!force_refresh && is_cache_fresh(cache, now)) {
        return view_of(KeeneticDnsRefreshStatus::UNCHANGED, "");
    }

    cache.last_attempt = now;
    const auto failed = [&](std::string error) {
        return view_of(cache.snapshot.has_value()
                           ? KeeneticDnsRefreshStatus::FETCH_FAILED_USED_CACHE
                           : KeeneticDnsRefreshStatus::FETCH_FAILED_NO_CACHE,
                       std::move(error));
    };

    try {
        const std::string response = keenetic_dns_fetch_fn()();
        KeeneticDnsSnapshot snapshot = extract_keenetic_dns_snapshot_from_rci(response);
        const bool changed = !cache.snapshot.has_value() || !(*cache.snapshot == snapshot);
        cache.snapshot = std::move(snapshot);
        cache.fetched_at = now;
        cache.fetched_at_unix = std::chrono::duration_cast<std::chrono::seconds>(
                                    std::chrono::system_clock::now().time_since_epoch())
                                    .count();
        return view_of(changed ? KeeneticDnsRefreshStatus::UPDATED
                               : KeeneticDnsRefreshStatus::UNCHANGED,
                       "");
    } catch (const HttpError& e) {
        return failed("Failed to query Keenetic RCI endpoint /rci/show/dns-proxy: " +
                      std::string(e.what()));
    } catch (const KeeneticDnsError& e) {
        return failed(e.what());
    }
#else
    (void)force_refresh;
    KeeneticDnsCacheView view;
    view.error = "DNS server type 'keenetic' requires build with USE_KEENETIC_API=ON";
    return view;
#endif
}

std::optional<KeeneticDnsCacheView> peek_keenetic_dns_cache() {
    std::lock_guard<std::mutex> lock(keenetic_dns_cache_mutex());
    const KeeneticDnsCacheState& cache = keenetic_dns_cache_state();
    if (!cache.snapshot.has_value()) {
        return std::nullopt;
    }
    KeeneticDnsCacheView view;
    view.status = KeeneticDnsRefreshStatus::UNCHANGED;
    view.snapshot = cache.snapshot;
    view.fetched_at_unix = cache.fetched_at_unix;
    return view;
}

std::optional<std::chrono::steady_clock::time_point> keenetic_dns_last_attempt() {
    std::lock_guard<std::mutex> lock(keenetic_dns_cache_mutex());
    return keenetic_dns_cache_state().last_attempt;
}

KeeneticDnsRefreshResult refresh_keenetic_dns_address_cache(bool force_refresh) {
    const KeeneticDnsCacheView view = refresh_keenetic_dns_cache(force_refresh);
    KeeneticDnsRefreshResult result;
    result.status = view.status;
    if (view.snapshot.has_value()) {
        result.addresses = view.snapshot->addresses;
    }
    result.error = view.error;
    return result;
}

std::vector<std::string> resolve_keenetic_dns_addresses(bool force_refresh) {
    const KeeneticDnsRefreshResult result = refresh_keenetic_dns_address_cache(force_refresh);
    if (!result.addresses.empty()) {
        return result.addresses;
    }
    throw KeeneticDnsError(result.error);
}

KeeneticUpstreamResolution resolve_keenetic_upstreams(
    const KeeneticDnsSnapshot& snapshot,
    const std::vector<KeeneticDnsProxyUpstream>& selected) {
    KeeneticUpstreamResolution resolution;
    if (selected.empty()) {
        resolution.addresses = snapshot.addresses;
        return resolution;
    }
    for (const auto& wanted : selected) {
        const auto found = std::find_if(
            snapshot.upstreams.begin(), snapshot.upstreams.end(),
            [&](const KeeneticDnsUpstreamEntry& upstream) {
                return upstream.kind == wanted.kind &&
                       keenetic_upstream_target(upstream) == wanted.target;
            });
        if (found == snapshot.upstreams.end()) {
            resolution.missing.push_back(wanted);
        } else if (std::find(resolution.addresses.begin(), resolution.addresses.end(),
                             found->address) == resolution.addresses.end()) {
            resolution.addresses.push_back(found->address);
        }
    }
    return resolution;
}

KeeneticUpstreamResolution resolve_keenetic_dns_upstreams(
    const std::vector<KeeneticDnsProxyUpstream>& selected) {
    const KeeneticDnsCacheView view = refresh_keenetic_dns_cache(false);
    if (!view.snapshot.has_value()) {
        throw KeeneticDnsError(view.error);
    }
    return resolve_keenetic_upstreams(*view.snapshot, selected);
}

std::vector<KeeneticStaticDnsEntry> get_keenetic_static_dns_entries() {
    std::lock_guard<std::mutex> lock(keenetic_dns_cache_mutex());
    const KeeneticDnsCacheState& cache = keenetic_dns_cache_state();
    if (!cache.snapshot.has_value()) {
        return {};
    }
    return cache.snapshot->static_entries;
}

std::vector<std::string> get_keenetic_dns_addresses() {
    std::lock_guard<std::mutex> lock(keenetic_dns_cache_mutex());
    const KeeneticDnsCacheState& cache = keenetic_dns_cache_state();
    if (!cache.snapshot.has_value()) {
        return {};
    }
    return cache.snapshot->addresses;
}

std::vector<KeeneticDnsUpstreamEntry> get_keenetic_dns_upstreams() {
    std::lock_guard<std::mutex> lock(keenetic_dns_cache_mutex());
    const KeeneticDnsCacheState& cache = keenetic_dns_cache_state();
    if (!cache.snapshot.has_value()) {
        return {};
    }
    return cache.snapshot->upstreams;
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
