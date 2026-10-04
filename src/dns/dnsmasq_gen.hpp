#pragma once

#include "../config/config.hpp"
#include "../lists/list_streamer.hpp"
#include "dns_router.hpp"

#include <cstddef>
#include <map>
#include <ostream>
#include <string>

namespace keen_pbr3 {

struct DnsmasqGenStats {
    size_t rules = 0;    // enabled dns.rules entries
    size_t domains = 0;  // domains written into server= rows
};

// Generates the dnsmasq config used for per-list upstream selection
// (dns.resolver_integration = dnsmasq): scoped server= rows for the domains
// of every enabled dns.rules entry plus optional default upstreams from
// dns.fallback.
class DnsmasqGenerator {
public:
    DnsmasqGenerator(const DnsServerRegistry& dns_registry,
                     ListStreamer& list_streamer,
                     const DnsConfig& dns_config,
                     const std::map<std::string, ListConfig>& lists);

    // Write the config to `out` and return the lowercase hex MD5 of exactly
    // the bytes written. May throw if a list cannot be streamed.
    std::string generate(std::ostream& out, DnsmasqGenStats* stats = nullptr);

private:
    // Strip wildcard prefix from domain (*.example.com -> example.com).
    static std::string strip_wildcard(const std::string& domain);

    const DnsServerRegistry& dns_registry_;
    ListStreamer& list_streamer_;
    const DnsConfig& dns_config_;
    const std::map<std::string, ListConfig>& lists_;
};

} // namespace keen_pbr3
