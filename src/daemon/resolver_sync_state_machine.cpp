#include "resolver_sync_state_machine.hpp"

namespace keen_pbr3 {

namespace {

bool within_converging_window(std::optional<std::int64_t> apply_started_ts,
                              std::int64_t now_ts) {
    return apply_started_ts.has_value() &&
           (now_ts - *apply_started_ts) <=
               ResolverSyncStateMachine::kConvergingWindowSeconds;
}

bool actual_is_current(const ResolverSyncSnapshot& snapshot) {
    if (!snapshot.apply_started_ts.has_value()) {
        return snapshot.probe_status == api::ResolverConfigProbeStatus::SUCCESS;
    }
    return snapshot.actual_ts.has_value() &&
           *snapshot.actual_ts >= *snapshot.apply_started_ts;
}

} // namespace

api::ResolverConfigProbeStatus resolver_probe_status_from_hash_probe_status(
    ResolverConfigHashProbeStatus status) {
    switch (status) {
    case ResolverConfigHashProbeStatus::SUCCESS:
        return api::ResolverConfigProbeStatus::SUCCESS;
    case ResolverConfigHashProbeStatus::NO_USABLE_TXT:
        return api::ResolverConfigProbeStatus::MISSING_TXT;
    case ResolverConfigHashProbeStatus::INVALID_TXT:
        return api::ResolverConfigProbeStatus::INVALID_TXT;
    case ResolverConfigHashProbeStatus::QUERY_FAILED:
        return api::ResolverConfigProbeStatus::QUERY_FAILED;
    }
    return api::ResolverConfigProbeStatus::UNKNOWN;
}

void ResolverSyncStateMachine::runtime_stopped() {
    runtime_active_ = false;
    resolver_configured_ = false;
    actual_hash_.clear();
    actual_ts_.reset();
    last_probe_ts_.reset();
    probe_status_ = api::ResolverConfigProbeStatus::NOT_CONFIGURED;
}

void ResolverSyncStateMachine::resolver_not_configured() {
    runtime_active_ = true;
    resolver_configured_ = false;
    actual_hash_.clear();
    actual_ts_.reset();
    last_probe_ts_.reset();
    probe_status_ = api::ResolverConfigProbeStatus::NOT_CONFIGURED;
}

void ResolverSyncStateMachine::expected_hash_updated(std::string expected_hash) {
    runtime_active_ = true;
    resolver_configured_ = true;
    expected_hash_ = std::move(expected_hash);
}

void ResolverSyncStateMachine::apply_started(std::int64_t ts, std::string expected_hash) {
    runtime_active_ = true;
    resolver_configured_ = true;
    apply_started_ts_ = ts;
    expected_hash_ = std::move(expected_hash);
}

void ResolverSyncStateMachine::probe_succeeded(std::string actual_hash,
                                               std::optional<std::int64_t> actual_ts,
                                               std::optional<std::int64_t> probe_ts) {
    runtime_active_ = true;
    resolver_configured_ = true;
    actual_hash_ = std::move(actual_hash);
    actual_ts_ = actual_ts;
    last_probe_ts_ = probe_ts;
    probe_status_ = api::ResolverConfigProbeStatus::SUCCESS;
}

void ResolverSyncStateMachine::probe_failed(ResolverConfigHashProbeStatus status,
                                            std::optional<std::int64_t> probe_ts) {
    runtime_active_ = true;
    resolver_configured_ = true;
    if (status != ResolverConfigHashProbeStatus::QUERY_FAILED) {
        actual_hash_.clear();
        actual_ts_.reset();
    }
    last_probe_ts_ = probe_ts;
    probe_status_ = resolver_probe_status_from_hash_probe_status(status);
}

ResolverSyncSnapshot ResolverSyncStateMachine::snapshot(std::int64_t now_ts) const {
    ResolverSyncSnapshot result;
    result.expected_hash = expected_hash_;
    result.actual_hash = actual_hash_;
    result.actual_ts = actual_ts_;
    result.last_probe_ts = last_probe_ts_;
    result.apply_started_ts = apply_started_ts_;
    result.probe_status = probe_status_;

    if (!runtime_active_ || !resolver_configured_) {
        result.probe_status = api::ResolverConfigProbeStatus::NOT_CONFIGURED;
        result.live_status = api::ResolverLiveStatus::UNKNOWN;
        return result;
    }

    if (expected_hash_.empty()) {
        result.live_status = api::ResolverLiveStatus::UNKNOWN;
        return result;
    }

    const bool converging = within_converging_window(apply_started_ts_, now_ts) &&
        (probe_status_ == api::ResolverConfigProbeStatus::UNKNOWN ||
         probe_status_ == api::ResolverConfigProbeStatus::QUERY_FAILED ||
         probe_status_ == api::ResolverConfigProbeStatus::MISSING_TXT ||
         probe_status_ == api::ResolverConfigProbeStatus::INVALID_TXT ||
         (actual_ts_.has_value() && apply_started_ts_.has_value() &&
          *actual_ts_ < *apply_started_ts_));

    if (converging) {
        result.sync_state = api::ResolverConfigSyncState::CONVERGING;
        result.live_status = probe_status_ == api::ResolverConfigProbeStatus::QUERY_FAILED
            ? api::ResolverLiveStatus::UNAVAILABLE
            : api::ResolverLiveStatus::HEALTHY;
        return result;
    }

    if (probe_status_ == api::ResolverConfigProbeStatus::QUERY_FAILED) {
        result.live_status = api::ResolverLiveStatus::UNAVAILABLE;
        return result;
    }

    if (probe_status_ == api::ResolverConfigProbeStatus::SUCCESS) {
        result.sync_state = (actual_is_current(result) && expected_hash_ == actual_hash_)
            ? api::ResolverConfigSyncState::CONVERGED
            : api::ResolverConfigSyncState::STALE;
        result.live_status = api::ResolverLiveStatus::HEALTHY;
        return result;
    }

    if (probe_status_ == api::ResolverConfigProbeStatus::MISSING_TXT ||
        probe_status_ == api::ResolverConfigProbeStatus::INVALID_TXT) {
        result.sync_state = api::ResolverConfigSyncState::STALE;
        result.live_status = api::ResolverLiveStatus::DEGRADED;
        return result;
    }

    result.live_status = api::ResolverLiveStatus::UNKNOWN;
    return result;
}

} // namespace keen_pbr3
