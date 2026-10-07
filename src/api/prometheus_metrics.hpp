#pragma once

#include "../config/config.hpp"
#include "../daemon/runtime_state_store.hpp"
#include "../intercept/intercept_processor.hpp"

#include <string>

namespace keen_pbr3 {

std::string prometheus_metrics(const InterceptCounters* counters,
                               const NetlinkMetricsSnapshot& netlink,
                               const ControlRuntimeSnapshot& runtime,
                               const OutboundRuntimeSnapshot& outbounds,
                               const Config& config,
                               uint64_t firewall_apply_errors,
                               const std::string& firewall_backend);

} // namespace keen_pbr3
