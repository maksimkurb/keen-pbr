#pragma once

#include <stdexcept>
#include <string>

namespace keen_pbr3 {

class KeeneticDnsError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// RCI endpoint used as source of truth for the built-in DNS proxy:
// GET http://127.0.0.1:79/rci/show/dns-proxy
//
// We read proxy-status entry with proxy-name == "System" and parse the first
// "dns_server = ..." directive from proxy-config.
std::string extract_keenetic_dns_address_from_rci(const std::string& response_body);

// Resolve built-in DNS server address via Keenetic RCI.
// Throws KeeneticDnsError on network/protocol/parse/validation errors.
std::string resolve_keenetic_dns_address();

} // namespace keen_pbr3
