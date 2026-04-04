#include "dns_router.hpp"
#include "keenetic_dns.hpp"

namespace keen_pbr3 {

DnsServerRegistry::DnsServerRegistry(const DnsConfig& dns_config)
    : fallback_tags_(dns_config.fallback.value_or(std::vector<std::string>{})) {
    // Parse all DNS server definitions into DnsServerConfig
    for (const auto& server : dns_config.servers.value_or(std::vector<DnsServer>{})) {
        std::string resolved_address;
        const auto server_type = server.type.value_or(api::DnsServerType::STATIC);

        if (server_type == api::DnsServerType::KEENETIC) {
            resolved_address = resolve_keenetic_dns_address();
        } else if (server_type == api::DnsServerType::STATIC) {
            if (!server.address.has_value()) {
                throw DnsError("DNS server '" + server.tag + "' is missing address");
            }
            resolved_address = *server.address;
        } else {
            throw DnsError("DNS server '" + server.tag + "' has unsupported type");
        }
        servers_.emplace(
            server.tag,
            parse_dns_server(server.tag, resolved_address, server.detour));
    }

    // Validate that fallback server tags exist
    for (const auto& fallback_tag : fallback_tags_) {
        if (servers_.find(fallback_tag) == servers_.end()) {
            throw DnsError("DNS fallback server tag not found: '" + fallback_tag + "'");
        }
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

std::vector<const DnsServerConfig*> DnsServerRegistry::fallback_servers() const {
    std::vector<const DnsServerConfig*> fallback_servers;
    fallback_servers.reserve(fallback_tags_.size());
    for (const auto& fallback_tag : fallback_tags_) {
        fallback_servers.push_back(&servers_.at(fallback_tag));
    }
    return fallback_servers;
}

} // namespace keen_pbr3
