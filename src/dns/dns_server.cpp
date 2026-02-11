#include "dns_server.hpp"

#include <charconv>
#include <cstdint>

namespace keen_pbr3 {

namespace {

bool is_valid_ipv4(const std::string& addr) {
    int dots = 0;
    size_t start = 0;
    for (size_t i = 0; i <= addr.size(); ++i) {
        if (i == addr.size() || addr[i] == '.') {
            if (i == start) return false;
            uint32_t octet = 0;
            auto [ptr, ec] = std::from_chars(addr.data() + start, addr.data() + i, octet);
            if (ec != std::errc{} || ptr != addr.data() + i) return false;
            if (octet > 255) return false;
            if (i < addr.size()) ++dots;
            start = i + 1;
        } else if (addr[i] < '0' || addr[i] > '9') {
            return false;
        }
    }
    return dots == 3;
}

bool is_valid_ipv6(const std::string& addr) {
    if (addr.empty()) return false;

    // Must contain at least one colon
    if (addr.find(':') == std::string::npos) return false;

    // Check for valid characters
    for (char c : addr) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
              (c >= 'A' && c <= 'F') || c == ':' || c == '.')) {
            return false;
        }
    }

    // Basic structure: groups separated by colons, :: allowed once
    int double_colon_count = 0;
    size_t pos = 0;
    while ((pos = addr.find("::", pos)) != std::string::npos) {
        ++double_colon_count;
        pos += 2;
    }
    if (double_colon_count > 1) return false;

    return true;
}

bool is_valid_ip(const std::string& addr) {
    return is_valid_ipv4(addr) || is_valid_ipv6(addr);
}

} // namespace

void validate_dns_address(const std::string& address) {
    if (!is_valid_ip(address)) {
        throw DnsError("Invalid DNS server address: '" + address +
                        "' (must be a valid IPv4 or IPv6 address)");
    }
}

DnsServerConfig parse_dns_server(const std::string& tag,
                                  const std::string& address,
                                  const std::optional<std::string>& detour) {
    validate_dns_address(address);

    DnsServerConfig config;
    config.tag = tag;
    config.address = address;
    config.detour = detour;
    config.resolved_ip = address;

    return config;
}

} // namespace keen_pbr3
