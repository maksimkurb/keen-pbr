#include "dns_router.hpp"
#include "keenetic_dns.hpp"
#include "../config/config_defaults.hpp"

#include <algorithm>

namespace keen_pbr3 {

namespace {

api::KeeneticOnMissing on_missing_of(const DnsServer& server) {
    return server.keenetic_on_missing.value_or(config_defaults::keenetic_on_missing);
}

const KeeneticDnsProxy* find_proxy(const KeeneticDnsSnapshot& snapshot, const std::string& name) {
    for (const auto& proxy : snapshot.proxies) {
        if (proxy.name == name) {
            return &proxy;
        }
    }
    return nullptr;
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

// "Keenetic group "VPN Only" (Policy0)", or "Keenetic group Policy0" without a description.
std::string policy_note(const KeeneticDnsSnapshot* snapshot, const std::string& policy) {
    const KeeneticDnsProxy* proxy = snapshot != nullptr ? find_proxy(*snapshot, policy) : nullptr;
    if (proxy == nullptr || proxy->description.empty()) {
        return "Keenetic group " + policy;
    }
    return "Keenetic group \"" + proxy->description + "\" (" + policy + ")";
}

} // namespace

std::vector<MissingKeeneticPolicy> find_missing_keenetic_policies(const DnsConfig& dns_config) {
    std::vector<MissingKeeneticPolicy> missing;
    const auto snapshot = peek_keenetic_dns_cache();
    if (!snapshot.has_value() || !snapshot->snapshot.has_value()) {
        return missing;
    }
    for (const auto& server : dns_config.servers.value_or(std::vector<DnsServer>{})) {
        const std::string policy = server.keenetic_policy.value_or("");
        if (server.type.value_or(api::DnsServerType::STATIC) != api::DnsServerType::KEENETIC ||
            is_keenetic_system_policy(policy)) {
            continue;
        }
        const auto& proxies = snapshot->snapshot->proxies;
        const bool present = std::any_of(proxies.begin(), proxies.end(), [&](const KeeneticDnsProxy& proxy) {
            return proxy.name == policy && proxy.listen_port != 0;
        });
        if (!present) {
            missing.push_back({server.tag, policy, on_missing_of(server)});
        }
    }
    return missing;
}

DnsServerRegistry::DnsServerRegistry(const DnsConfig& dns_config) {
    // Parse all DNS server definitions into DnsServerConfig
    for (const auto& server : dns_config.servers.value_or(std::vector<DnsServer>{})) {
        const auto server_type = server.type.value_or(api::DnsServerType::STATIC);
        if (server_type == api::DnsServerType::KEENETIC) {
            const std::string policy = server.keenetic_policy.value_or("");
            const KeeneticPolicyResolution resolution = resolve_keenetic_dns_policy(policy);
            std::vector<std::string> addresses = resolution.addresses;
            if (is_keenetic_system_policy(policy)) {
                system_tags_.insert(server.tag);
            }
            if (resolution.missing) {
                const auto mode = on_missing_of(server);
                missing_policies_.push_back({server.tag, policy, mode});
                if (mode == api::KeeneticOnMissing::SYSTEM) {
                    addresses = resolve_keenetic_dns_addresses();
                    system_tags_.insert(server.tag);
                } else {
                    addresses.clear();
                    blocked_tags_.insert(server.tag);
                }
            }
            const auto cache = peek_keenetic_dns_cache();
            const KeeneticDnsSnapshot* snapshot =
                cache.has_value() && cache->snapshot.has_value() ? &*cache->snapshot : nullptr;
            const bool via_system = system_tags_.count(server.tag) != 0;
            servers_[server.tag];  // a blocked tag is known, with no servers
            for (const auto& resolved_address : addresses) {
                DnsServerConfig& resolved = servers_[server.tag].emplace_back(
                    parse_dns_server(server.tag, resolved_address, server.detour));
                resolved.note = via_system ? system_upstream_note(snapshot, resolved_address)
                                           : policy_note(snapshot, policy);
                if (via_system && resolution.missing) {
                    resolved.note += " (Keenetic group " + policy + " is missing)";
                }
            }
            if (via_system && !system_seen_ && snapshot != nullptr) {
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
