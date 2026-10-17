#include "dns_router.hpp"
#include "keenetic_dns.hpp"

#include <algorithm>

namespace keen_pbr3 {

namespace {

DnsBalancerUpstream balancer_upstream(const DnsServerConfig& server) {
    if (server.is_doh()) {
        return DnsBalancerUpstream::doh(server.doh_url, server.detour.value_or(""));
    }
    return DnsBalancerUpstream::plain(server.resolved_ip, server.port);
}

std::string upstream_label(const DnsServerConfig& server) {
    return server.is_doh() ? server.doh_url : dnsmasq_server_address(server);
}

std::string group_key(const std::vector<DnsBalancerUpstream>& upstreams) {
    std::string key;
    for (const auto& upstream : upstreams) {
        key += upstream.kind == DnsBalancerUpstreamKind::Doh
                   ? "d|" + upstream.url + "|" + upstream.detour
                   : "p|" + upstream.ip + "|" + std::to_string(upstream.port);
        key += '\n';
    }
    return key;
}

void add_unique(std::vector<std::string>& values, std::string value) {
    if (std::find(values.begin(), values.end(), value) == values.end()) {
        values.push_back(std::move(value));
    }
}

} // namespace

std::string dnsmasq_server_address(const DnsServerConfig& server) {
    std::string address = server.resolved_ip;
    if (server.port != 53) {
        address += "#" + std::to_string(server.port);
    }
    return address;
}

std::map<std::string, DnsListUsage> collect_dns_list_usage(const DnsConfig& dns_config) {
    std::map<std::string, DnsListUsage> lists;
    for (const auto& rule : dns_config.rules.value_or(std::vector<DnsRule>{})) {
        if (!dns_rule_enabled(rule)) {
            continue;
        }
        for (const auto& list_name : rule.list) {
            DnsListUsage& usage = lists[list_name];
            usage.allow_rebind = usage.allow_rebind || rule.allow_domain_rebinding.value_or(false);
            for (const auto& tag : rule.servers) {
                add_unique(usage.servers, tag);
            }
        }
    }
    return lists;
}

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
        } else if (server_type == api::DnsServerType::DOH) {
            if (!server.address.has_value()) {
                throw DnsError("DNS server '" + server.tag + "' is missing address");
            }
            servers_[server.tag].push_back(
                parse_doh_server(server.tag, *server.address, server.detour));
        } else {
            throw DnsError("DNS server '" + server.tag + "' has unsupported type");
        }
        if (!bootstrap_.has_value()) {
            const auto resolved = servers_.find(server.tag);
            if (resolved != servers_.end() && !resolved->second.empty() &&
                !resolved->second.front().is_doh()) {
                bootstrap_ = balancer_upstream(resolved->second.front());
            }
        }
    }

    // Decide, for the servers each list (and the default set) uses, between
    // dnsmasq talking to them directly and the forwarder doing it.
    struct Plan {
        std::vector<DnsBalancerUpstream> upstreams;  // forwarder upstreams
        DnsTargets direct;                           // used when !forwarded
        bool forwarded{false};
        std::string key;
    };
    const auto plan_for = [&](const std::vector<std::string>& tags) {
        Plan plan;
        bool any_doh = false;
        std::size_t tags_with_servers = 0;
        for (const auto& tag : tags) {
            const auto found = servers_.find(tag);
            if (found == servers_.end()) continue;
            if (!found->second.empty()) ++tags_with_servers;
            for (const DnsServerConfig& server : found->second) {
                any_doh = any_doh || server.is_doh();
                const DnsBalancerUpstream upstream = balancer_upstream(server);
                if (std::find(plan.upstreams.begin(), plan.upstreams.end(), upstream) ==
                    plan.upstreams.end()) {
                    plan.upstreams.push_back(upstream);
                }
                add_unique(plan.direct.addresses, upstream_label(server));
                add_unique(plan.direct.upstreams, upstream_label(server));
            }
        }
        uses_doh_ = uses_doh_ || any_doh;
        plan.forwarded = !plan.upstreams.empty() && (any_doh || tags_with_servers > 1);
        if (plan.forwarded) plan.key = group_key(plan.upstreams);
        return plan;
    };

    std::map<std::string, Plan> list_plans;
    for (const auto& [list_name, usage] : collect_dns_list_usage(dns_config)) {
        list_plans.emplace(list_name, plan_for(usage.servers));
    }
    // Default servers: plain ones are separate `server=` rows; every DoH one
    // gets a forwarder of its own (no balancing across the default servers).
    std::vector<Plan> fallback_plans;
    for (const auto& tag : dns_config.fallback.value_or(std::vector<std::string>{})) {
        const auto found = servers_.find(tag);
        if (found == servers_.end()) continue;
        for (const DnsServerConfig& server : found->second) {
            Plan plan;
            if (server.is_doh()) {
                uses_doh_ = true;
                plan.upstreams = {balancer_upstream(server)};
                plan.forwarded = true;
                plan.key = group_key(plan.upstreams);
            }
            plan.direct.addresses = {upstream_label(server)};
            plan.direct.upstreams = {upstream_label(server)};
            fallback_plans.push_back(std::move(plan));
        }
    }

    // One listener per distinct set of upstreams; the port follows the sorted
    // sets, so the same config always gives the same ports.
    std::map<std::string, std::vector<DnsBalancerUpstream>> distinct;
    for (const auto& [name, plan] : list_plans) {
        if (plan.forwarded) distinct.emplace(plan.key, plan.upstreams);
    }
    for (const auto& plan : fallback_plans) {
        if (plan.forwarded) distinct.emplace(plan.key, plan.upstreams);
    }
    std::map<std::string, std::string> key_address;
    for (const auto& [key, upstreams] : distinct) {
        if (groups_.size() >= kDnsBalancerMaxGroups) {
            balancer_error_ = "too many different DNS server sets: at most " +
                              std::to_string(kDnsBalancerMaxGroups) +
                              " lists or default servers can use several servers or DNS-over-HTTPS";
            break;
        }
        DnsBalancerGroup group;
        group.port = static_cast<std::uint16_t>(kDnsBalancerBasePort + groups_.size());
        group.upstreams = upstreams;
        key_address[key] = std::string(kDnsBalancerAddress) + "#" + std::to_string(group.port);
        groups_.push_back(std::move(group));
    }

    for (auto& [list_name, plan] : list_plans) {
        DnsTargets targets = plan.direct;
        if (plan.forwarded) {
            const auto address = key_address.find(plan.key);
            targets.addresses.clear();
            if (address != key_address.end()) targets.addresses.push_back(address->second);
        }
        list_targets_.emplace(list_name, std::move(targets));
    }
    for (const auto& plan : fallback_plans) {
        if (plan.forwarded) {
            const auto address = key_address.find(plan.key);
            if (address != key_address.end()) add_unique(fallback_.addresses, address->second);
        } else {
            for (const auto& address : plan.direct.addresses) add_unique(fallback_.addresses, address);
        }
        for (const auto& upstream : plan.direct.upstreams) add_unique(fallback_.upstreams, upstream);
    }
}

const DnsTargets& DnsServerRegistry::list_targets(const std::string& list) const {
    static const DnsTargets none;
    const auto found = list_targets_.find(list);
    return found == list_targets_.end() ? none : found->second;
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
