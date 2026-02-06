#include "dns_server.hpp"

#include <algorithm>
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

    // Handle bracketed form (strip brackets)
    // Not expected here, but be safe

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

bool is_doh_url(const std::string& addr) {
    return addr.size() > 8 && addr.substr(0, 8) == "https://";
}

bool is_system(const std::string& addr) {
    return addr == "system";
}

bool is_rcode_refused(const std::string& addr) {
    return addr == "rcode://refused";
}

} // namespace

DnsServerType parse_dns_address_type(const std::string& address) {
    if (is_system(address)) {
        return DnsServerType::System;
    }
    if (is_rcode_refused(address)) {
        return DnsServerType::Blocked;
    }
    if (is_doh_url(address)) {
        return DnsServerType::DoH;
    }
    if (is_valid_ip(address)) {
        return DnsServerType::PlainIP;
    }
    throw DnsError("Invalid DNS server address: '" + address + "'");
}

void validate_dns_address(const std::string& address) {
    // parse_dns_address_type already validates and throws on invalid input
    parse_dns_address_type(address);
}

DnsServerConfig parse_dns_server(const std::string& tag,
                                  const std::string& address,
                                  const std::optional<std::string>& detour) {
    DnsServerType type = parse_dns_address_type(address);

    DnsServerConfig config;
    config.tag = tag;
    config.type = type;
    config.address = address;
    config.detour = detour;

    switch (type) {
        case DnsServerType::PlainIP:
            config.resolved_ip = address;
            break;
        case DnsServerType::DoH:
            config.doh_url = address;
            break;
        case DnsServerType::System:
        case DnsServerType::Blocked:
            break;
    }

    return config;
}

} // namespace keen_pbr3
