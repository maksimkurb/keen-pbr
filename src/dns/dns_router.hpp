#pragma once

#include "../config/config.hpp"
#include "dns_server.hpp"

#include <map>
#include <string>

namespace keen_pbr3 {

class DnsServerRegistry {
public:
    // Construct from DNS config.
    // Parses all DNS server definitions and validates tags.
    explicit DnsServerRegistry(const DnsConfig& dns_config);

    std::vector<const DnsServerConfig*> get_servers(const std::string& tag) const;

    // Get fallback server configs in configured order.
    std::vector<const DnsServerConfig*> fallback_servers() const;

private:
    std::map<std::string, std::vector<DnsServerConfig>> servers_;
    std::vector<std::string> fallback_tags_;
};

} // namespace keen_pbr3
