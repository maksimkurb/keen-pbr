#include "resolver_health.hpp"

namespace keen_pbr3 {

ResolverActualState build_resolver_actual_state(
    bool routing_runtime_active,
    bool resolver_configured,
    const std::optional<ResolverConfigHashProbeResult>& probe_result,
    std::optional<std::int64_t> probe_completed_ts) {
    ResolverActualState state;
    if (!routing_runtime_active || !resolver_configured) {
        return state;
    }

    if (!probe_result.has_value()) {
        return state;
    }

    state.last_probe_ts = probe_completed_ts;
    switch (probe_result->status) {
    case ResolverConfigHashProbeStatus::SUCCESS:
        state.live_status = api::ResolverLiveStatus::HEALTHY;
        state.actual_hash = probe_result->parsed_value.hash;
        state.actual_ts = probe_result->parsed_value.ts;
        break;
    case ResolverConfigHashProbeStatus::NO_USABLE_TXT:
    case ResolverConfigHashProbeStatus::INVALID_TXT:
        state.live_status = api::ResolverLiveStatus::DEGRADED;
        break;
    case ResolverConfigHashProbeStatus::QUERY_FAILED:
        state.live_status = api::ResolverLiveStatus::UNAVAILABLE;
        break;
    }

    return state;
}

std::optional<api::ResolverConfigSyncState> classify_resolver_config_sync_state(
    const std::optional<std::int64_t>& actual_ts,
    const std::optional<std::int64_t>& apply_started_ts,
    std::int64_t now_ts,
    bool hash_equal) {
    if (!actual_ts.has_value()) {
        return std::nullopt;
    }
    if (!apply_started_ts.has_value()) {
        return hash_equal
            ? std::optional<api::ResolverConfigSyncState>(api::ResolverConfigSyncState::CONVERGED)
            : std::optional<api::ResolverConfigSyncState>(api::ResolverConfigSyncState::STALE);
    }
    constexpr std::int64_t kConvergingWindowSeconds = 15;
    if (*actual_ts < *apply_started_ts) {
        if ((now_ts - *apply_started_ts) <= kConvergingWindowSeconds) {
            return api::ResolverConfigSyncState::CONVERGING;
        }
        return hash_equal
            ? std::optional<api::ResolverConfigSyncState>(api::ResolverConfigSyncState::CONVERGED)
            : std::optional<api::ResolverConfigSyncState>(api::ResolverConfigSyncState::STALE);
    }
    return hash_equal
        ? std::optional<api::ResolverConfigSyncState>(api::ResolverConfigSyncState::CONVERGED)
        : std::optional<api::ResolverConfigSyncState>(api::ResolverConfigSyncState::STALE);
}

} // namespace keen_pbr3
