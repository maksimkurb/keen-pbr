#pragma once
#include <nlohmann/json.hpp>
#include <cstdint>
#include <optional>
#include <string>

namespace keen_pbr3 {

enum class RoutingDnsSource { System, Dnsmasq };

struct RoutingDnsResolver {
    RoutingDnsSource source{RoutingDnsSource::System};
    std::string address;                     // Dnsmasq only
    std::uint16_t port{0};
    std::string instance;                    // selector
    std::optional<std::string> unavailable;  // Dnsmasq expected but not usable: no query, dns_error
    std::optional<std::string> warning;      // query anyway, add this warning
};

inline nlohmann::json routing_dns_resolver_to_json(const RoutingDnsResolver& r) {
    nlohmann::json j{{"source", r.source == RoutingDnsSource::Dnsmasq ? "dnsmasq" : "system"}};
    if (r.source == RoutingDnsSource::Dnsmasq) {
        j["address"] = r.address;
        j["port"] = r.port;
        j["instance"] = r.instance;
    }
    if (r.unavailable) j["unavailable"] = *r.unavailable;
    if (r.warning) j["warning"] = *r.warning;
    return j;
}

// std::nullopt when `j` is not an object or has no valid "source".
inline std::optional<RoutingDnsResolver> routing_dns_resolver_from_json(const nlohmann::json& j) {
    if (!j.is_object() || !j.contains("source") || !j.at("source").is_string()) return std::nullopt;
    RoutingDnsResolver r;
    const std::string source = j.at("source").get<std::string>();
    if (source == "dnsmasq") r.source = RoutingDnsSource::Dnsmasq;
    else if (source != "system") return std::nullopt;
    r.address = j.value("address", std::string{});
    r.port = j.value("port", std::uint16_t{0});
    r.instance = j.value("instance", std::string{});
    if (j.contains("unavailable") && j.at("unavailable").is_string()) r.unavailable = j.at("unavailable").get<std::string>();
    if (j.contains("warning") && j.at("warning").is_string()) r.warning = j.at("warning").get<std::string>();
    if (r.source == RoutingDnsSource::Dnsmasq && !r.unavailable && (r.address.empty() || r.port == 0)) {
        r.unavailable = "invalid dnsmasq endpoint";
    }
    return r;
}

} // namespace keen_pbr3
