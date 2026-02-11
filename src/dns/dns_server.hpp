#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace keen_pbr3 {

class DnsError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct DnsServerConfig {
    std::string tag;
    std::string address;      // Original address string (IPv4 or IPv6)
    std::optional<std::string> detour;
    std::string resolved_ip;  // The validated IP address
};

// Build a DnsServerConfig from a config-level DnsServer definition.
// Only accepts valid IPv4 or IPv6 addresses; throws DnsError otherwise.
DnsServerConfig parse_dns_server(const std::string& tag,
                                  const std::string& address,
                                  const std::optional<std::string>& detour);

// Validate a DNS server address (must be valid IPv4 or IPv6).
// Throws DnsError if invalid.
void validate_dns_address(const std::string& address);

} // namespace keen_pbr3
