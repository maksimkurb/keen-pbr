#pragma once

#include "../api/generated/api_types.hpp"
#include "routing_health.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace keen_pbr3 {

// A type=keenetic DNS server whose Keenetic DNS proxy group is gone from the router.
struct KeeneticDnsMissingInput {
    std::string tag;
    std::string policy;
    bool block{true};  // true: queries are blocked; false: the System upstreams are used
};

// Inputs of the overview health verdict. Null pointers mean "not available".
struct HealthIssueInputs {
    bool service_running{false};
    const RoutingHealthReport* routing_health{nullptr};
    const api::InterceptHealthClass* intercept{nullptr};
    // intercept.dns / intercept.l7 requested by the config.
    bool requested_dns{false};
    bool requested_l7{false};
    const api::DnsmasqHealth* dnsmasq{nullptr};
    // Documentation section on installing dnsmasq on this OS.
    std::string dnsmasq_setup_docs_url;
    const std::vector<api::RuntimeOutboundStateElement>* outbounds{nullptr};
    std::vector<KeeneticDnsMissingInput> keenetic_dns_missing;
};

// Kernel checks (capabilities, iptables extensions, probes) that apply to
// the requested interception features.
std::vector<api::KernelCheckElement> collect_kernel_checks(
    const api::InterceptHealthClass& intercept, bool requested_dns, bool requested_l7);

// The dnsmasq section of the installation guide for `os_type` (keenetic,
// openwrt, debian), else the generic "Do I need dnsmasq?" section.
std::string dnsmasq_setup_docs_url(std::string_view os_type);

// Classifies every health problem as an error or a warning. The web UI
// overview and `keen-pbr status` both show this list.
api::HealthIssuesResponse collect_health_issues(const HealthIssueInputs& inputs);

} // namespace keen_pbr3
