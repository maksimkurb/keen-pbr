#include "dns_router.hpp"

namespace keen_pbr3 {

DnsServerRegistry::DnsServerRegistry(const DnsConfig& dns_config)
    : fallback_tag_(dns_config.fallback.value_or("")) {
    // Parse all DNS server definitions into DnsServerConfig
    for (const auto& server : dns_config.servers.value_or(std::vector<DnsServer>{})) {
        servers_.emplace(
            server.tag,
            parse_dns_server(server.tag, server.address, server.detour));
    }

    // Validate that fallback server tag exists
    if (servers_.find(fallback_tag_) == servers_.end()) {
        throw DnsError("DNS fallback server tag not found: '" + fallback_tag_ + "'");
    }

    // Validate that all rule server tags exist
    for (const auto& rule : dns_config.rules.value_or(std::vector<DnsRule>{})) {
        if (servers_.find(rule.server) == servers_.end()) {
            throw DnsError("DNS rule references unknown server tag: '" + rule.server + "'");
        }
    }
}

const DnsServerConfig* DnsServerRegistry::get_server(const std::string& tag) const {
    auto it = servers_.find(tag);
    if (it == servers_.end()) {
        return nullptr;
    }
    return &it->second;
}

const DnsServerConfig& DnsServerRegistry::fallback() const {
    return servers_.at(fallback_tag_);
}

} // namespace keen_pbr3
