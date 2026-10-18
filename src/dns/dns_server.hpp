#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

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

// A DNS-over-HTTPS endpoint split into the parts the DoH client needs.
struct DohUrl {
    bool        secure = true;             // https (false only when plain http is allowed)
    std::string host;                      // name or IP literal, without brackets
    uint16_t    port = 443;
    std::string path = "/";                // path and query, starts with '/'
    std::string ip;                        // set when `host` is an IP literal
};

// Parse "https://host[:port][/path[?query]]". Plain http is accepted only when
// `allow_plain_http` is set (tests); userinfo and fragments are rejected.
// Throws DnsError on any other form.
DohUrl parse_doh_url(const std::string& url, bool allow_plain_http = false);

struct DnsServerConfig {
    std::string tag;
    std::string address;                   // original string
    std::optional<std::string> detour;
    std::string resolved_ip;               // bare IP (no port, no brackets); empty for DoH
    uint16_t    port = 53;                 // parsed port, default 53 (443 for DoH)
    std::string doh_url;                   // type=doh: the https:// URL; empty otherwise
    std::vector<std::string> ips;          // type=doh: fixed server addresses; empty: look the host up

    bool is_doh() const { return !doh_url.empty(); }
};

// Build a DnsServerConfig from a config-level DnsServer definition.
// Accepts "ip", "ip:port", "[ipv6]:port"; throws DnsError otherwise.
DnsServerConfig parse_dns_server(const std::string& tag,
                                  const std::string& address,
                                  const std::optional<std::string>& detour);

// Build a DnsServerConfig for a type=doh server; throws DnsError when the
// URL is not a valid https:// URL.
DnsServerConfig parse_doh_server(const std::string& tag,
                                 const std::string& url,
                                 const std::optional<std::string>& detour,
                                 const std::vector<std::string>& ips = {});

// Validate a DNS server address (must be valid IPv4 or IPv6, with optional port).
// Throws DnsError if invalid.
void validate_dns_address(const std::string& address);

} // namespace keen_pbr3
