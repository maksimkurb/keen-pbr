#pragma once

#include "../api/generated/api_types.hpp"
#include "routing_health.hpp"

#include <vector>

namespace keen_pbr3 {

// Inputs of the overview health verdict. Null pointers mean "not available".
struct HealthIssueInputs {
    bool service_running{false};
    const RoutingHealthReport* routing_health{nullptr};
    const api::InterceptHealthClass* intercept{nullptr};
    // intercept.dns / intercept.l7 requested by the config.
    bool requested_dns{false};
    bool requested_l7{false};
    const api::DnsmasqHealth* dnsmasq{nullptr};
    const std::vector<api::RuntimeOutboundStateElement>* outbounds{nullptr};
};

// Kernel checks (capabilities, iptables extensions, probes) that apply to
// the requested interception features.
std::vector<api::KernelCheckElement> collect_kernel_checks(
    const api::InterceptHealthClass& intercept, bool requested_dns, bool requested_l7);

// Classifies every health problem as an error or a warning. The web UI
// overview and `keen-pbr status` both show this list.
api::HealthIssuesResponse collect_health_issues(const HealthIssueInputs& inputs);

} // namespace keen_pbr3
