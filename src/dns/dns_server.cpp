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

ParsedDnsAddress parse_dns_address_str(const std::string& address) {
    if (address.empty()) {
        throw DnsError("Invalid DNS server address: empty string");
    }

    std::string ip;
    uint16_t    port = 53;

    if (address[0] == '[') {
        // Bracketed IPv6: "[::1]" or "[::1]:port"
        auto close = address.find(']');
        if (close == std::string::npos) {
            throw DnsError("Invalid DNS server address: '" + address +
                           "' (missing closing ']')");
        }
        ip = address.substr(1, close - 1);
        if (close + 1 < address.size()) {
            if (address[close + 1] != ':') {
                throw DnsError("Invalid DNS server address: '" + address +
                               "' (expected ':' after ']')");
            }
            const std::string port_str = address.substr(close + 2);
            uint32_t p = 0;
            auto [ptr, ec] = std::from_chars(port_str.data(),
                                              port_str.data() + port_str.size(), p);
            if (ec != std::errc{} || ptr != port_str.data() + port_str.size()) {
                throw DnsError("Invalid DNS server address: '" + address +
                               "' (non-numeric port)");
            }
            if (p < 1 || p > 65535) {
                throw DnsError("Invalid DNS server address: '" + address +
                               "' (port out of range 1-65535)");
            }
            port = static_cast<uint16_t>(p);
        }
    } else {
        int colon_count = 0;
        for (char c : address) {
            if (c == ':') ++colon_count;
        }
        if (colon_count > 1) {
            // Bare IPv6 (no brackets, no port)
            ip   = address;
            port = 53;
        } else if (colon_count == 1) {
            // IPv4:port
            auto sep = address.find(':');
            ip = address.substr(0, sep);
            const std::string port_str = address.substr(sep + 1);
            uint32_t p = 0;
            auto [ptr, ec] = std::from_chars(port_str.data(),
                                              port_str.data() + port_str.size(), p);
            if (ec != std::errc{} || ptr != port_str.data() + port_str.size()) {
                throw DnsError("Invalid DNS server address: '" + address +
                               "' (non-numeric port)");
            }
            if (p < 1 || p > 65535) {
                throw DnsError("Invalid DNS server address: '" + address +
                               "' (port out of range 1-65535)");
            }
            port = static_cast<uint16_t>(p);
        } else {
            // Bare IPv4
            ip   = address;
            port = 53;
        }
    }

    if (!is_valid_ip(ip)) {
        throw DnsError("Invalid DNS server address: '" + address +
                       "' (not a valid IPv4 or IPv6 address)");
    }

    return {ip, port};
}

void validate_dns_address(const std::string& address) {
    parse_dns_address_str(address); // throws on any error
}

DnsServerConfig parse_dns_server(const std::string& tag,
                                  const std::string& address,
                                  const std::optional<std::string>& detour) {
    auto parsed = parse_dns_address_str(address);

    DnsServerConfig config;
    config.tag         = tag;
    config.address     = address;
    config.detour      = detour;
    config.resolved_ip = parsed.ip;
    config.port        = parsed.port;

    return config;
}

} // namespace keen_pbr3
