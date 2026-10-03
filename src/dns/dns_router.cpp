#include "dns_router.hpp"
#include "keenetic_dns.hpp"

namespace keen_pbr3 {

DnsServerRegistry::DnsServerRegistry(const DnsConfig& dns_config) {
    // Parse all DNS server definitions into DnsServerConfig
    for (const auto& server : dns_config.servers.value_or(std::vector<DnsServer>{})) {
        const auto server_type = server.type.value_or(api::DnsServerType::STATIC);
        if (server_type == api::DnsServerType::KEENETIC) {
            for (const auto& resolved_address : resolve_keenetic_dns_addresses()) {
                servers_[server.tag].push_back(
                    parse_dns_server(server.tag, resolved_address, server.detour));
            }
        } else if (server_type == api::DnsServerType::STATIC) {
            if (!server.address.has_value()) {
                throw DnsError("DNS server '" + server.tag + "' is missing address");
            }
            servers_[server.tag].push_back(
                parse_dns_server(server.tag, *server.address, server.detour));
        } else {
            throw DnsError("DNS server '" + server.tag + "' has unsupported type");
        }
    }
}

std::vector<const DnsServerConfig*> DnsServerRegistry::get_servers(const std::string& tag) const {
    std::vector<const DnsServerConfig*> resolved_servers;
    auto it = servers_.find(tag);
    if (it == servers_.end()) {
        return resolved_servers;
    }
    resolved_servers.reserve(it->second.size());
    for (const auto& server : it->second) {
        resolved_servers.push_back(&server);
    }
    return resolved_servers;
}

} // namespace keen_pbr3
