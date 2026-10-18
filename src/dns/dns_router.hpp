#pragma once

#include "../config/config.hpp"
#include "dns_balancer.hpp"
#include "dns_server.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace keen_pbr3 {

// What the enabled dns.rules say about one list: the DNS server tags of all
// rules naming it, in rule order and without repeats.
struct DnsListUsage {
    std::vector<std::string> servers;
    bool allow_rebind = false;
};

std::map<std::string, DnsListUsage> collect_dns_list_usage(const DnsConfig& dns_config);

// Where dnsmasq sends the queries of one list (or the default ones).
struct DnsTargets {
    // The `server=` addresses: "ip", "ip#port", or the forwarder
    // "<dns.proxy_address>#port".
    std::vector<std::string> addresses;
    // The servers behind them: "ip", "ip#port" or the DoH URL.
    std::vector<std::string> upstreams;
    // One per address: which servers answer there, for the comment above
    // the `server=` rows.
    std::vector<std::string> notes;
};

class DnsServerRegistry {
public:
    // Construct from DNS config.
    // Parses all DNS server definitions and validates tags.
    explicit DnsServerRegistry(const DnsConfig& dns_config);

    std::vector<const DnsServerConfig*> get_servers(const std::string& tag) const;

    // Targets of the dnsmasq `server=` rows of a list named by dns.rules, and
    // of the default servers (dns.fallback). A list with exactly one plain DNS
    // server is sent straight to it. A list with a DoH server or several
    // servers goes to a forwarder (see balancer_groups()), which tries them in
    // turn: dnsmasq then stores one server entry per domain, not one per
    // server.
    const DnsTargets& list_targets(const std::string& list) const;
    const DnsTargets& fallback_targets() const { return fallback_; }

    // dns.proxy_address, or its default: where the forwarder listens.
    const std::string& proxy_address() const { return proxy_address_; }

    // The forwarder listeners the targets above point at, with their ports.
    const std::vector<DnsBalancerGroup>& balancer_groups() const { return groups_; }
    // Who resolves through each forwarder port.
    const DnsProxyUsers& balancer_users() const { return users_; }
    // Set when there are more distinct server sets than listener ports.
    const std::string& balancer_error() const { return balancer_error_; }

    // The servers that resolve the host names of DoH URLs, in the order they
    // are asked: dns.bootstrap, else the first plain DNS server of dns.servers
    // (`bootstrap_is_auto()`). Empty: the system resolver.
    const std::vector<DnsBalancerUpstream>& bootstrap_servers() const { return bootstrap_; }
    bool bootstrap_is_auto() const { return bootstrap_auto_; }

    bool uses_doh() const { return uses_doh_; }

private:
    std::map<std::string, std::vector<DnsServerConfig>> servers_;
    std::map<std::string, DnsTargets> list_targets_;
    DnsTargets fallback_;
    std::vector<DnsBalancerGroup> groups_;
    DnsProxyUsers users_;
    std::string proxy_address_;
    std::string balancer_error_;
    std::vector<DnsBalancerUpstream> bootstrap_;
    bool bootstrap_auto_{false};
    bool uses_doh_{false};
};

// "ip" or "ip#port", the way dnsmasq writes a server address.
std::string dnsmasq_server_address(const DnsServerConfig& server);

} // namespace keen_pbr3
