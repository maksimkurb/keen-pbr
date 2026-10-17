#pragma once

#include "../config/config.hpp"
#include "dns_balancer.hpp"
#include "dns_server.hpp"
#include "keenetic_dns.hpp"

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace keen_pbr3 {

// A type=keenetic DNS server naming upstreams the Keenetic built-in DNS no
// longer has.
struct MissingKeeneticUpstreams {
    std::string tag;
    std::vector<KeeneticDnsProxyUpstream> upstreams;  // the missing ones
    bool blocked{true};  // none of the selected upstreams is left: queries are refused
};

// The type=keenetic servers of `dns_config` that select upstreams (protocol +
// target) the cached Keenetic snapshot does not contain. Empty while nothing
// has been read from the router yet.
std::vector<MissingKeeneticUpstreams> find_missing_keenetic_upstreams(const DnsConfig& dns_config);

class DnsServerRegistry {
public:
    // Construct from DNS config.
    // Parses all DNS server definitions and validates tags.
    explicit DnsServerRegistry(const DnsConfig& dns_config);

    std::vector<const DnsServerConfig*> get_servers(const std::string& tag) const;

    // The tag answers nothing: every Keenetic upstream it selects is gone.
    // Queries for its domains must not reach other upstreams.
    bool is_blocked(const std::string& tag) const;

    // The tag is a type=keenetic server (it forwards to upstreams of the
    // Keenetic System DNS).
    bool uses_keenetic_system(const std::string& tag) const;

    // Servers System uses for single domains only, as (domain, server). dnsmasq
    // gets them next to the System upstreams, since System's own DNS proxy is
    // bypassed. Empty without a System keenetic server.
    const std::vector<std::pair<std::string, DnsServerConfig>>& keenetic_system_domain_servers() const {
        return system_domain_servers_;
    }

    // System's static records (static_a/static_aaaa). dnsmasq gets them when
    // it replaces System as the default resolver. Empty without a System
    // keenetic server.
    const std::vector<KeeneticStaticDnsEntry>& keenetic_system_static_entries() const {
        return system_static_entries_;
    }

    // Every type=keenetic server that selects an upstream the router lacks.
    const std::vector<MissingKeeneticUpstreams>& missing_keenetic_upstreams() const {
        return missing_upstreams_;
    }

    // The balancer groups the servers with two or more selected upstreams
    // need, on kDnsBalancerAddress; their server entries point at them.
    const std::vector<DnsBalancerGroup>& balancer_groups() const {
        return balancer_groups_;
    }

private:
    std::map<std::string, std::vector<DnsServerConfig>> servers_;
    std::set<std::string> blocked_tags_;
    std::set<std::string> system_tags_;
    std::vector<std::pair<std::string, DnsServerConfig>> system_domain_servers_;
    std::vector<KeeneticStaticDnsEntry> system_static_entries_;
    std::vector<MissingKeeneticUpstreams> missing_upstreams_;
    std::vector<DnsBalancerGroup> balancer_groups_;
    bool system_seen_{false};  // System's records are taken once
};

} // namespace keen_pbr3
