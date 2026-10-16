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

private:
    std::map<std::string, std::vector<DnsServerConfig>> servers_;
};

} // namespace keen_pbr3
