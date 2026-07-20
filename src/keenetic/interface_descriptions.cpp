#ifdef WITH_API

#include "interface_descriptions.hpp"

#include "../http/http_client.hpp"
#include "../util/system_info.hpp"

#include <arpa/inet.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <utility>
#include <vector>

namespace keen_pbr3 {
namespace {

constexpr const char* kInterfacesEndpoint = "http://127.0.0.1:79/rci/show/interface";
constexpr const char* kRciEndpoint = "http://127.0.0.1:79/rci/";
constexpr auto kCacheTtl = std::chrono::minutes(2);

struct KeeneticInterface {
    std::string id;
    std::string description;
    std::string connected;
    std::vector<std::string> addresses;
};

struct CacheState {
    struct DescriptionMappings {
        std::map<std::string, std::string> values;
        bool keys_are_addresses{false};
    };
    std::optional<DescriptionMappings> descriptions;
    std::chrono::steady_clock::time_point fetched_at{};
};

CacheState& cache_state() {
    static CacheState state;
    return state;
}

std::mutex& cache_mutex() {
    static std::mutex mutex;
    return mutex;
}

using FetchFn = std::function<std::string(
    const std::string& method, const std::string& url, const std::string& body)>;
using NowFn = std::function<std::chrono::steady_clock::time_point()>;

FetchFn default_fetcher() {
    return [](const std::string& method, const std::string& url, const std::string& body) {
        HttpClient client;
        client.set_timeout(std::chrono::seconds(3));
        client.set_max_response_size(256 * 1024);
        return method == "POST" ? client.post_json(url, body) : client.download(url);
    };
}

FetchFn& fetcher() {
    static FetchFn value = default_fetcher();
    return value;
}

NowFn& now_fn() {
    static NowFn value = [] { return std::chrono::steady_clock::now(); };
    return value;
}

bool cache_is_fresh(const CacheState& state, std::chrono::steady_clock::time_point now) {
    return state.descriptions.has_value() && now - state.fetched_at < kCacheTtl;
}

bool supports_system_name_endpoint(const std::string& version) {
    size_t dot = version.find('.');
    if (dot == std::string::npos) return false;
    try {
        const int major = std::stoi(version.substr(0, dot));
        size_t minor_end = dot + 1;
        while (minor_end < version.size() && std::isdigit(static_cast<unsigned char>(version[minor_end]))) {
            ++minor_end;
        }
        if (minor_end == dot + 1) return false;
        const int minor = std::stoi(version.substr(dot + 1, minor_end - dot - 1));
        return major > 4 || (major == 4 && minor >= 3);
    } catch (const std::exception&) {
        return false;
    }
}

std::optional<std::string> ipv4_cidr(const std::string& address, const std::string& mask) {
    in_addr parsed_address{};
    in_addr parsed_mask{};
    if (inet_pton(AF_INET, address.c_str(), &parsed_address) != 1 ||
        inet_pton(AF_INET, mask.c_str(), &parsed_mask) != 1) return std::nullopt;
    const uint32_t bits = ntohl(parsed_mask.s_addr);
    int prefix = 0;
    while (prefix < 32 && (bits & (uint32_t{1} << (31 - prefix)))) ++prefix;
    if (prefix < 32 && (bits & ((uint32_t{1} << (31 - prefix)) - 1))) return std::nullopt;
    return address + "/" + std::to_string(prefix);
}

std::optional<std::string> ipv6_cidr(const std::string& address, int prefix) {
    in6_addr parsed{};
    if (prefix < 0 || prefix > 128 || inet_pton(AF_INET6, address.c_str(), &parsed) != 1) {
        return std::nullopt;
    }
    return address + "/" + std::to_string(prefix);
}

std::vector<KeeneticInterface> parse_interfaces(const std::string& body) {
    const auto document = nlohmann::json::parse(body);
    std::vector<KeeneticInterface> result;
    const auto append = [&result](const nlohmann::json& value, const std::string& fallback_id) {
        if (!value.is_object()) return;
        KeeneticInterface interface;
        interface.id = value.value("id", fallback_id);
        interface.description = value.value("description", "");
        interface.connected = value.value("connected", "");
        if (interface.id.empty()) return;
        const std::string address = value.value("address", "");
        const std::string mask = value.value("mask", "");
        if (const auto cidr = ipv4_cidr(address, mask)) interface.addresses.push_back(*cidr);
        const auto ipv6 = value.find("ipv6");
        if (ipv6 != value.end() && ipv6->is_object()) {
            const auto addresses = ipv6->find("addresses");
            if (addresses != ipv6->end() && addresses->is_array()) {
                for (const auto& item : *addresses) {
                    if (!item.is_object()) continue;
                    const auto cidr = ipv6_cidr(item.value("address", ""),
                                                item.value("prefix-length", -1));
                    if (cidr) interface.addresses.push_back(*cidr);
                }
            }
        }
        result.push_back(std::move(interface));
    };

    if (document.is_object()) {
        for (auto it = document.begin(); it != document.end(); ++it) append(it.value(), it.key());
    } else if (document.is_array()) {
        for (const auto& item : document) append(item, "");
    } else {
        throw std::runtime_error("Keenetic interface response is not an object or array");
    }
    return result;
}

bool is_parent_id(const std::string& id) { return id.find('/') == std::string::npos; }

bool should_replace(const KeeneticInterface& existing, const KeeneticInterface& candidate) {
    if (is_parent_id(candidate.id) != is_parent_id(existing.id)) return is_parent_id(candidate.id);
    if (candidate.connected.empty() != existing.connected.empty()) return !candidate.connected.empty();
    return !candidate.description.empty() && existing.description.empty();
}

CacheState::DescriptionMappings map_by_system_names(
    const std::vector<KeeneticInterface>& interfaces, const std::string& response_body) {
    const auto response = nlohmann::json::parse(response_body);
    const auto show = response.find("show");
    if (show == response.end() || !show->is_object()) throw std::runtime_error("Invalid Keenetic bulk response");
    const auto items = show->find("interface");
    if (items == show->end() || !items->is_array()) throw std::runtime_error("Invalid Keenetic bulk response");

    std::map<std::string, KeeneticInterface> selected;
    for (size_t i = 0; i < interfaces.size() && i < items->size(); ++i) {
        const auto field = (*items)[i].find("system-name");
        if (field == (*items)[i].end() || !field->is_string() || interfaces[i].description.empty()) continue;
        const std::string system_name = field->get<std::string>();
        if (system_name.empty()) continue;
        const auto existing = selected.find(system_name);
        if (existing == selected.end() || should_replace(existing->second, interfaces[i])) {
            selected[system_name] = interfaces[i];
        }
    }
    CacheState::DescriptionMappings result;
    for (const auto& [name, interface] : selected) result.values[name] = interface.description;
    return result;
}

CacheState::DescriptionMappings map_by_addresses(const std::vector<KeeneticInterface>& interfaces) {
    std::map<std::string, KeeneticInterface> selected;
    for (const auto& interface : interfaces) {
        if (interface.description.empty()) continue;
        for (const auto& address : interface.addresses) {
            const auto existing = selected.find(address);
            if (existing == selected.end() || should_replace(existing->second, interface)) {
                selected[address] = interface;
            }
        }
    }
    CacheState::DescriptionMappings result;
    result.keys_are_addresses = true;
    for (const auto& [address, interface] : selected) result.values[address] = interface.description;
    return result;
}

std::optional<CacheState::DescriptionMappings> fetch_descriptions() {
    if (cached_system_info().os_type != "keenetic") return std::nullopt;
    const auto interfaces = parse_interfaces(fetcher()("GET", kInterfacesEndpoint, ""));
    if (supports_system_name_endpoint(cached_system_info().os_version)) {
        nlohmann::json request = {{"show", {{"interface", nlohmann::json::array()}}}};
        for (const auto& interface : interfaces) {
            request["show"]["interface"].push_back(
                {{"system-name", {{"name", interface.id}}}});
        }
        return map_by_system_names(interfaces, fetcher()("POST", kRciEndpoint, request.dump()));
    }
    return map_by_addresses(interfaces);
}

} // namespace

std::optional<CacheState::DescriptionMappings> resolve_keenetic_interface_descriptions() {
    try {
        return fetch_descriptions();
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

void populate_keenetic_interface_descriptions(
    api::RuntimeInterfaceInventoryResponse& response) {
    std::lock_guard<std::mutex> lock(cache_mutex());
    CacheState& cache = cache_state();
    const auto now = now_fn()();
    if (!cache_is_fresh(cache, now)) {
        if (const auto descriptions = resolve_keenetic_interface_descriptions()) {
            cache.descriptions = *descriptions;
            cache.fetched_at = now;
        }
    }
    if (!cache.descriptions) return;
    for (auto& interface : response.interfaces) {
        if (!cache.descriptions->keys_are_addresses) {
            const auto it = cache.descriptions->values.find(interface.name);
            if (it != cache.descriptions->values.end()) interface.description = it->second;
            continue;
        }
        const auto apply = [&interface, &cache](const std::optional<std::vector<std::string>>& addresses) {
            if (!addresses || interface.description) return;
            for (const auto& address : *addresses) {
                const auto it = cache.descriptions->values.find(address);
                if (it != cache.descriptions->values.end()) {
                    interface.description = it->second;
                    return;
                }
            }
        };
        apply(interface.ipv4_addresses);
        apply(interface.ipv6_addresses);
    }
}

#ifdef KEEN_PBR3_TESTING
void set_keenetic_interface_fetcher_for_tests(KeeneticInterfaceFetchFn value) {
    std::lock_guard<std::mutex> lock(cache_mutex());
    fetcher() = std::move(value);
}

void set_keenetic_interface_now_fn_for_tests(KeeneticInterfaceNowFn value) {
    std::lock_guard<std::mutex> lock(cache_mutex());
    now_fn() = std::move(value);
}

void reset_keenetic_interface_test_state() {
    std::lock_guard<std::mutex> lock(cache_mutex());
    cache_state() = CacheState{};
    fetcher() = default_fetcher();
    now_fn() = [] { return std::chrono::steady_clock::now(); };
}
#endif

} // namespace keen_pbr3

#endif // WITH_API
