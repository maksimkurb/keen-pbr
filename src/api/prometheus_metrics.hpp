#pragma once

#include "../config/config.hpp"
#include "../daemon/list_refresh_stats.hpp"
#include "../firewall/firewall_counters.hpp"
#include "../daemon/runtime_state_store.hpp"
#include "../intercept/intercept_processor.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <string>

namespace keen_pbr3 {

// Process-level telemetry owned by the daemon.  Read once per scrape.
struct DaemonMetrics {
    uint64_t firewall_apply_errors{0};
    int64_t process_start_unix_s{0};
    // Empty until a config apply (or the initial start) completed.
    std::optional<int64_t> config_reload_last_success_unix_s;
    uint64_t config_reload_errors{0};
    std::map<std::string, ListRefreshStats> lists;  // remote lists seen by the refresher
    // Read from the live iptables counters at scrape time; absent on nftables
    // and while no firewall is applied.
    FirewallCounters firewall_counters;
};

// Renders the Prometheus text exposition.  Everything here runs at scrape
// time; the packet paths only touch the relaxed atomics in InterceptCounters.
// All durations are float seconds.
std::string prometheus_metrics(const InterceptCounters* counters,
                               const NetlinkMetricsSnapshot& netlink,
                               const ControlRuntimeSnapshot& runtime,
                               const OutboundRuntimeSnapshot& outbounds,
                               const Config& config,
                               const DaemonMetrics& daemon,
                               const std::string& firewall_backend);

} // namespace keen_pbr3
