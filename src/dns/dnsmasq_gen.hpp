#pragma once

#include "../config/config.hpp"
#include "../lists/list_streamer.hpp"
#include "dns_router.hpp"
#include "keenetic_dns.hpp"
#include <keen-pbr/version.hpp>

#include <functional>
#include <ostream>
#include <string>
#include <vector>

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
                     ResolverType resolver_type = ResolverType::DNSMASQ_IPSET,
                     std::string hash_version = KEEN_PBR3_VERSION_FULL_STRING);

    // Generate dnsmasq configuration and stream it to the output.
    // Produces ipset=/nftset= and server= directives for all matched domains.
    void generate(std::ostream& out);

    // Compute MD5 hash over canonical user-controlled resolver records.
    // Returns 32-char lowercase hex string.
    std::string compute_config_hash();

    // Static overload: construct a temporary DnsmasqGenerator and compute hash.
    static std::string compute_config_hash(
        const DnsServerRegistry& dns_registry,
        ListStreamer& list_streamer,
        const RouteConfig& route_config,
        const DnsConfig& dns_config,
        const std::map<std::string, ListConfig>& lists,
        std::string hash_version = KEEN_PBR3_VERSION_FULL_STRING);

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
    // Emit dnsmasq directives to out when provided and update the canonical
    // hash payload via the optional callback in the same pass over the lists.
    void generate_directives(
        std::ostream* out,
        const std::function<void(const std::string&)>& hash_record_callback = {});

    // Strip wildcard prefix from domain (*.example.com -> example.com).
    static std::string strip_wildcard(const std::string& domain);

    const DnsServerRegistry& dns_registry_;
    ListStreamer& list_streamer_;
    const RouteConfig& route_config_;
    const DnsConfig& dns_config_;
    const std::map<std::string, ListConfig>& lists_;
    std::vector<KeeneticStaticDnsEntry> keenetic_static_entries_;
    std::vector<KeeneticDnsUpstreamEntry> keenetic_dns_upstreams_;
    ResolverType resolver_type_;
    std::string hash_version_;
};

} // namespace keen_pbr3
