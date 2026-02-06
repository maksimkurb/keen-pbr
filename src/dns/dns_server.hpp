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

enum class DnsServerType {
    PlainIP,    // Plain IP address (e.g., "8.8.8.8", "2001:4860:4860::8888")
    DoH,        // DNS-over-HTTPS URL (e.g., "https://dns.google/dns-query")
    System,     // System resolver ("system")
    Blocked,    // Blocking response ("rcode://refused")
};

struct DnsServerConfig {
    std::string tag;
    DnsServerType type;
    std::string address;  // Original address string
    std::optional<std::string> detour;

    // Parsed fields depending on type
    std::string resolved_ip;   // For PlainIP: the IP address
    std::string doh_url;       // For DoH: the full URL
};

// Parse a DNS server address string and determine its type
DnsServerType parse_dns_address_type(const std::string& address);

// Build a DnsServerConfig from a config-level DnsServer definition
DnsServerConfig parse_dns_server(const std::string& tag,
                                  const std::string& address,
                                  const std::optional<std::string>& detour);

// Validate a DNS server address, throw DnsError if invalid
void validate_dns_address(const std::string& address);

} // namespace keen_pbr3
