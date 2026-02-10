#pragma once

#include "../config/config.hpp"
#include "../lists/list_streamer.hpp"
#include "dns_router.hpp"

#include <ostream>
#include <string>

namespace keen_pbr3 {

class DnsmasqGenerator {
public:
    // Construct with DNS server registry (for server tag lookup),
    // list streamer (for streaming domain entries), route config (for mapping lists to ipset names),
    // DNS config (for rules mapping lists to DNS servers), and lists config (for list definitions).
    DnsmasqGenerator(const DnsServerRegistry& dns_registry,
                     ListStreamer& list_streamer,
                     const RouteConfig& route_config,
                     const DnsConfig& dns_config,
                     const std::map<std::string, ListConfig>& lists);

    // Generate dnsmasq configuration and stream it to the output.
    // Produces ipset= and server= directives for all matched domains.
    void generate(std::ostream& out);

private:
    // Build the ipset name for a given list name.
    static std::string ipset_name(const std::string& list_name);

    // Strip wildcard prefix from domain (*.example.com -> example.com).
    static std::string strip_wildcard(const std::string& domain);

    const DnsServerRegistry& dns_registry_;
    ListStreamer& list_streamer_;
    const RouteConfig& route_config_;
    const DnsConfig& dns_config_;
    const std::map<std::string, ListConfig>& lists_;
};

} // namespace keen_pbr3
