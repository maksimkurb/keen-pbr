#include "dns_router.hpp"
#include "keenetic_dns.hpp"

namespace keen_pbr3 {

namespace {

const KeeneticDnsProxy* find_proxy(const KeeneticDnsSnapshot& snapshot, const std::string& name) {
    for (const auto& proxy : snapshot.proxies) {
        if (proxy.name == name) {
            return &proxy;
        }
    }
    return nullptr;
}

bool looks_like_ipv4(const std::string& address) {
    return address.find(':') == std::string::npos || address.find(':') == address.rfind(':');
}

std::string protocol_kind(api::KeeneticDnsProtocol protocol) {
    switch (protocol) {
        case api::KeeneticDnsProtocol::DO_H: return "DoH";
        case api::KeeneticDnsProtocol::DO_T: return "DoT";
        case api::KeeneticDnsProtocol::PLAIN: break;
    }
    return "Plain";
}

std::vector<KeeneticDnsProxyUpstream> selected_upstreams(const DnsServer& server) {
    std::vector<KeeneticDnsProxyUpstream> selected;
    for (const auto& upstream : server.keenetic_upstreams.value_or(
             std::vector<api::KeeneticDnsProxyUpstreamElement>{})) {
        selected.push_back({protocol_kind(upstream.protocol), upstream.target});
    }
    return selected;
}

bool is_keenetic_server(const DnsServer& server) {
    return server.type.value_or(api::DnsServerType::STATIC) == api::DnsServerType::KEENETIC;
}

// "Keenetic System, DoH https://dns.example/dns-query" for a System upstream.
std::string system_upstream_note(const KeeneticDnsSnapshot* snapshot, const std::string& address) {
    std::string note = "Keenetic System";
    if (snapshot == nullptr) {
        return note;
    }
    for (const auto& upstream : snapshot->upstreams) {
        if (upstream.address != address) {
            continue;
        }
        note += ", " + upstream.kind;
        if (!upstream.target.empty()) {
            note += " " + upstream.target;
        }
        break;
    }
    return note;
}

std::string balancer_note(const KeeneticDnsSnapshot* snapshot,
                          const std::vector<std::string>& addresses) {
    std::string note = "Keenetic System, balanced across " + std::to_string(addresses.size());
    for (size_t i = 0; i < addresses.size(); ++i) {
        note += i == 0 ? ": " : ", ";
        std::string label = addresses[i];
        if (snapshot != nullptr) {
            for (const auto& upstream : snapshot->upstreams) {
                if (upstream.address == addresses[i]) {
                    label = upstream.kind + " " + keenetic_upstream_target(upstream);
                    break;
                }
            }
        }
        note += label;
    }
    return note;
}

} // namespace

std::vector<MissingKeeneticUpstreams> find_missing_keenetic_upstreams(const DnsConfig& dns_config) {
    std::vector<MissingKeeneticUpstreams> missing;
    const auto cache = peek_keenetic_dns_cache();
    if (!cache.has_value() || !cache->snapshot.has_value()) {
        return missing;
    }
    for (const auto& server : dns_config.servers.value_or(std::vector<DnsServer>{})) {
        if (!is_keenetic_server(server)) {
            continue;
        }
        const auto resolution = resolve_keenetic_upstreams(*cache->snapshot, selected_upstreams(server));
        if (!resolution.missing.empty()) {
            missing.push_back({server.tag, resolution.missing, resolution.addresses.empty()});
        }
    }
    return missing;
}

DnsServerRegistry::DnsServerRegistry(const DnsConfig& dns_config) {
    // Parse all DNS server definitions into DnsServerConfig
    for (const auto& server : dns_config.servers.value_or(std::vector<DnsServer>{})) {
        const auto server_type = server.type.value_or(api::DnsServerType::STATIC);
        if (server_type == api::DnsServerType::KEENETIC) {
            const auto selected = selected_upstreams(server);
            const KeeneticUpstreamResolution resolution = resolve_keenetic_dns_upstreams(selected);
            if (!resolution.missing.empty()) {
                missing_upstreams_.push_back(
                    {server.tag, resolution.missing, resolution.addresses.empty()});
            }
            const auto cache = peek_keenetic_dns_cache();
            const KeeneticDnsSnapshot* snapshot =
                cache.has_value() && cache->snapshot.has_value() ? &*cache->snapshot : nullptr;
            servers_[server.tag];  // a blocked tag is known, with no servers
            if (resolution.addresses.empty()) {
                blocked_tags_.insert(server.tag);  // nothing to forward to
            } else if (selected.size() >= 2) {
                // One server= row per list: dnsmasq stores a record per domain
                // and server, so the balancer stands in for all upstreams.
                DnsBalancerGroup group;
                group.port = static_cast<std::uint16_t>(kDnsBalancerBasePort + balancer_groups_.size());
                for (const auto& address : resolution.addresses) {
                    if (looks_like_ipv4(address)) {
                        const ParsedDnsAddress parsed = parse_dns_address_str(address);
                        group.upstreams.push_back({parsed.ip, parsed.port});
                    }
                }
                DnsServerConfig& resolved = servers_[server.tag].emplace_back(parse_dns_server(
                    server.tag, std::string(kDnsBalancerAddress) + ":" + std::to_string(group.port),
                    std::nullopt));
                resolved.note = balancer_note(snapshot, resolution.addresses);
                balancer_groups_.push_back(std::move(group));
            } else {
                for (const auto& resolved_address : resolution.addresses) {
                    DnsServerConfig& resolved = servers_[server.tag].emplace_back(
                        parse_dns_server(server.tag, resolved_address, server.detour));
                    resolved.note = system_upstream_note(snapshot, resolved_address);
                }
            }
            const bool serves = blocked_tags_.count(server.tag) == 0;
            if (serves) {
                system_tags_.insert(server.tag);
            }
            if (serves && !system_seen_ && snapshot != nullptr) {
                system_seen_ = true;
                system_static_entries_ = snapshot->static_entries;
                if (const KeeneticDnsProxy* system = find_proxy(*snapshot, kKeeneticSystemPolicy)) {
                    for (const auto& scoped : system->domain_servers) {
                        for (const auto& address : scoped.addresses) {
                            DnsServerConfig& resolved = system_domain_servers_.emplace_back(
                                scoped.domain,
                                parse_dns_server(server.tag, address, std::nullopt)).second;
                            resolved.note = "Keenetic System, only " + scoped.domain;
                        }
                    }
                }
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

bool DnsServerRegistry::is_blocked(const std::string& tag) const {
    return blocked_tags_.count(tag) != 0;
}

bool DnsServerRegistry::uses_keenetic_system(const std::string& tag) const {
    return system_tags_.count(tag) != 0;
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
