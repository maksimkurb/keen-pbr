#pragma once

#include "../config/config.hpp"
#include "../lists/list_manager.hpp"
#include "dns_router.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace keen_pbr3 {

class DnsmasqGenerator {
public:
    // Construct with DNS router (for resolving domains to DNS servers),
    // list manager (for domain lists), route config (for mapping lists to ipset names),
    // and DNS config (for rules mapping lists to DNS servers).
    DnsmasqGenerator(const DnsRouter& dns_router,
                     const ListManager& list_manager,
                     const RouteConfig& route_config,
                     const DnsConfig& dns_config);

    // Generate dnsmasq configuration content as a string.
    // Produces ipset= and server= directives for all matched domains.
    std::string generate() const;

    // Generate and write configuration to the specified file path.
    void write(const std::filesystem::path& output_path) const;

private:
    // Build the ipset name for a given list name.
    // Returns the ipset set name used by the firewall backend.
    static std::string ipset_name(const std::string& list_name);

    // Strip wildcard prefix from domain (*.example.com -> example.com).
    static std::string strip_wildcard(const std::string& domain);

    const DnsRouter& dns_router_;
    const ListManager& list_manager_;
    const RouteConfig& route_config_;
    const DnsConfig& dns_config_;
};

} // namespace keen_pbr3
