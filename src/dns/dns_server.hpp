#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>

namespace keen_pbr3 {

class DnsError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct ParsedDnsAddress {
    std::string ip;       // bare IP, without brackets or port
    uint16_t    port = 53;
};

// Parse "ip", "ip:port", "[ipv6]:port" → ParsedDnsAddress.
// Throws DnsError on invalid format, invalid IP, or port out of range 1-65535.
ParsedDnsAddress parse_dns_address_str(const std::string& address);

struct DnsServerConfig {
    std::string tag;
    std::string address;                   // original string
    std::optional<std::string> detour;
    std::string resolved_ip;               // bare IP (no port, no brackets)
    uint16_t    port = 53;                 // parsed port, default 53
};

// Build a DnsServerConfig from a config-level DnsServer definition.
// Accepts "ip", "ip:port", "[ipv6]:port"; throws DnsError otherwise.
DnsServerConfig parse_dns_server(const std::string& tag,
                                  const std::string& address,
                                  const std::optional<std::string>& detour);

// Validate a DNS server address (must be valid IPv4 or IPv6, with optional port).
// Throws DnsError if invalid.
void validate_dns_address(const std::string& address);

} // namespace keen_pbr3
