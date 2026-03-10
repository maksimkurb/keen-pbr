#pragma once

#include "../config/config.hpp"
#include "../lists/list_streamer.hpp"
#include "dns_router.hpp"

#include <functional>
#include <ostream>
#include <stdexcept>
#include <string>

namespace keen_pbr3 {

enum class ResolverType {
    DNSMASQ_IPSET,
    DNSMASQ_NFTSET,
};

class DnsmasqGenerator {
public:
    // Parse "dnsmasq-ipset" / "dnsmasq-nftset" string → ResolverType.
    // Throws std::invalid_argument if unknown.
    static ResolverType parse_resolver_type(const std::string& s);

    // Construct with DNS server registry (for server tag lookup),
    // list streamer (for streaming domain entries), route config (for mapping lists to ipset names),
    // DNS config (for rules mapping lists to DNS servers), and lists config (for list definitions).
    DnsmasqGenerator(const DnsServerRegistry& dns_registry,
                     ListStreamer& list_streamer,
                     const RouteConfig& route_config,
                     const DnsConfig& dns_config,
                     const std::map<std::string, ListConfig>& lists,
                     ResolverType resolver_type = ResolverType::DNSMASQ_IPSET);

    // Generate dnsmasq configuration and stream it to the output.
    // Produces ipset=/nftset= and server= directives for all matched domains.
    void generate(std::ostream& out);

    // Compute MD5 hash over the full generated directives (excluding TXT record line).
    // Returns 32-char lowercase hex string.
    std::string compute_config_hash();

    // Static overload: construct a temporary DnsmasqGenerator and compute hash.
    static std::string compute_config_hash(
        const DnsServerRegistry& dns_registry,
        ListStreamer& list_streamer,
        const RouteConfig& route_config,
        const DnsConfig& dns_config,
        const std::map<std::string, ListConfig>& lists,
        ResolverType resolver_type = ResolverType::DNSMASQ_IPSET);

    // Build the dynamic (dnsmasq-populated) IPv4/IPv6 set names for a given list name.
    // These are the sets referenced by ipset=/nftset= directives in dnsmasq config.
    // Static IP/CIDR sets use the "kpbr4_" / "kpbr6_" prefix (without "d").
    static std::string ipset_name_v4(const std::string& list_name) {
        return "kpbr4d_" + list_name;
    }
    static std::string ipset_name_v6(const std::string& list_name) {
        return "kpbr6d_" + list_name;
    }

private:
    // Emit all dnsmasq directives (header comment, ipset=/nftset=, server= lines)
    // without the TXT record line. Used by generate() and compute_config_hash().
    void generate_directives(std::ostream& out);

    // Iterate every (domain, list_name) pair for all ipset-backed lists in output order,
    // calling callback once per domain per list.
    void for_each_ipset_domain(
        std::function<void(const std::string& domain,
                           const std::string& list_name)> callback);

    // Strip wildcard prefix from domain (*.example.com -> example.com).
    static std::string strip_wildcard(const std::string& domain);

    const DnsServerRegistry& dns_registry_;
    ListStreamer& list_streamer_;
    const RouteConfig& route_config_;
    const DnsConfig& dns_config_;
    const std::map<std::string, ListConfig>& lists_;
    ResolverType resolver_type_;
};

} // namespace keen_pbr3
