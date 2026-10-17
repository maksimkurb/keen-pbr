#pragma once

#include "../config/config.hpp"
#include "dns_server.hpp"
#include "keenetic_dns.hpp"

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace keen_pbr3 {

// A type=keenetic DNS server whose Keenetic DNS proxy group is not on the router.
struct MissingKeeneticPolicy {
    std::string tag;
    std::string policy;
    api::KeeneticOnMissing mode{api::KeeneticOnMissing::BLOCK};
};

// The type=keenetic servers of `dns_config` that name a group (other than
// System) the cached Keenetic snapshot does not contain. Empty while nothing
// has been read from the router yet.
std::vector<MissingKeeneticPolicy> find_missing_keenetic_policies(const DnsConfig& dns_config);

class DnsServerRegistry {
public:
    // Construct from DNS config.
    // Parses all DNS server definitions and validates tags.
    explicit DnsServerRegistry(const DnsConfig& dns_config);

    std::vector<const DnsServerConfig*> get_servers(const std::string& tag) const;

    // The tag answers nothing: its Keenetic policy is missing and the server
    // is set to block. Queries for its domains must not reach other upstreams.
    bool is_blocked(const std::string& tag) const;

    // The tag forwards to the System upstreams (it is the System group, or a
    // missing policy set to use System).
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

    // Every type=keenetic server whose policy is missing, blocked or not.
    const std::vector<MissingKeeneticPolicy>& missing_keenetic_policies() const {
        return missing_policies_;
    }

private:
    std::map<std::string, std::vector<DnsServerConfig>> servers_;
    std::set<std::string> blocked_tags_;
    std::set<std::string> system_tags_;
    std::vector<std::pair<std::string, DnsServerConfig>> system_domain_servers_;
    std::vector<KeeneticStaticDnsEntry> system_static_entries_;
    std::vector<MissingKeeneticPolicy> missing_policies_;
    bool system_seen_{false};  // System's records are taken once
};

} // namespace keen_pbr3
