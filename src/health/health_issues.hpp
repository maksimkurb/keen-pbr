#pragma once

#include "../api/generated/api_types.hpp"
#include "../dns/dns_balancer.hpp"
#include "routing_health.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace keen_pbr3 {

struct UndownloadedListInfo {
    std::string name;
    std::string url;
    std::optional<std::string> detour;
    std::string error;
};

// A DNS server of the DNS proxy that failed its last kDnsFailingStreak
// attempts in a row.
struct DnsServerFailure {
    std::string tag;      // tags of the dns.servers entries, comma-joined
    std::string address;  // "ip:port" or the DoH URL
    std::string detour;   // outbound tag, empty for none
    std::string reason;   // result of the last failed attempt, e.g. "timeout"
    std::string error;    // its text
    std::uint32_t count{0};  // failed attempts in a row
    // Set when every server of a group the server is in fails; `entries` are
    // then the lists (or the default servers) that group answers for.
    bool group_down{false};
    std::vector<std::string> entries;
};

// The servers of `stats` that keep failing, each once however many groups it
// is in. Servers that are not usable (no DoH support, no routing mark) are
// left out: they have warnings of their own.
std::vector<DnsServerFailure> collect_dns_server_failures(const DnsBalancerStats& stats,
                                                          const DnsProxyUsers& users);

// Inputs of the overview health verdict. Null pointers mean "not available".
struct HealthIssueInputs {
    bool service_running{false};
    const RoutingHealthReport* routing_health{nullptr};
    const api::InterceptHealth* intercept{nullptr};
    // intercept.dns / intercept.l7 requested by the config.
    bool requested_dns{false};
    bool requested_l7{false};
    const api::DnsmasqHealth* dnsmasq{nullptr};
    // Documentation section on installing dnsmasq on this OS.
    std::string dnsmasq_setup_docs_url;
    const std::vector<api::RuntimeOutboundState>* outbounds{nullptr};
    // The config has DNS-over-HTTPS servers, but no CA certificates were found
    // to verify them: they are not used.
    bool doh_ca_missing{false};
    // Lists with a url that are not downloaded yet and failed last time.
    std::vector<UndownloadedListInfo> undownloaded_lists;
    // DNS servers of the DNS proxy that stopped answering.
    std::vector<DnsServerFailure> dns_server_failures;
    // DNS management is on, but dns.fallback names no upstream server.
    bool dns_upstream_missing{false};
    // Tags of the DoH servers whose host name has to be looked up while no
    // bootstrap server is chosen.
    std::vector<std::string> doh_without_bootstrap;
};

// Kernel checks (capabilities, iptables extensions, probes) that apply to
// the requested interception features.
std::vector<api::KernelCheck> collect_kernel_checks(
    const api::InterceptHealth& intercept, bool requested_dns, bool requested_l7);

// The dnsmasq section of the installation guide for `os_type` (keenetic,
// openwrt, debian), else the generic "Do I need dnsmasq?" section.
std::string dnsmasq_setup_docs_url(std::string_view os_type);

// Classifies every health problem as an error or a warning. The web UI
// overview and `keen-pbr status` both show this list.
api::HealthIssuesResponse collect_health_issues(const HealthIssueInputs& inputs);

} // namespace keen_pbr3
