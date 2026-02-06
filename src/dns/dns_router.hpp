#pragma once

#include "../config/config.hpp"
#include "../lists/list_manager.hpp"
#include "dns_server.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace keen_pbr3 {

class DnsRouter {
public:
    // Construct from DNS config and loaded lists.
    // Parses all DNS server definitions and stores rules for matching.
    DnsRouter(const DnsConfig& dns_config, const ListManager& list_manager);

    // Find the DNS server config for a given domain name.
    // Matches against dns.rules in config order; first match wins.
    // Returns the fallback server if no rule matches.
    const DnsServerConfig& resolve(const std::string& domain) const;

    // Get a server config by tag. Returns nullptr if not found.
    const DnsServerConfig* get_server(const std::string& tag) const;

    // Get the fallback server config.
    const DnsServerConfig& fallback() const;

private:
    // Check if a domain matches any domain in the given list.
    bool domain_matches_list(const std::string& domain,
                             const std::string& list_name) const;

    // Check if a domain matches a pattern (exact or wildcard).
    static bool domain_matches_pattern(const std::string& domain,
                                       const std::string& pattern);

    std::map<std::string, DnsServerConfig> servers_;
    std::vector<DnsRule> rules_;
    std::string fallback_tag_;
    const ListManager& list_manager_;
};

} // namespace keen_pbr3
