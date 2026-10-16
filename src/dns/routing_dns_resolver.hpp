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

} // namespace keen_pbr3
