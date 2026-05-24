#include <doctest/doctest.h>

#include "../src/daemon/resolver_health.hpp"
#include "../src/daemon/resolver_sync_state_machine.hpp"

using namespace keen_pbr3;

namespace {

ResolverConfigHashProbeResult make_probe_result(ResolverConfigHashProbeStatus status,
                                                std::optional<std::int64_t> ts = std::nullopt,
                                                std::string hash = "",
                                                std::optional<std::string> raw_txt = std::nullopt) {
    ResolverConfigHashProbeResult result;
    result.status = status;
    result.parsed_value.ts = ts;
    result.parsed_value.hash = std::move(hash);
    result.raw_txt = std::move(raw_txt);
    return result;
}

} // namespace

TEST_CASE("resolver health: matching live TXT yields healthy converged state") {
    const auto probe = make_probe_result(
        ResolverConfigHashProbeStatus::SUCCESS,
        1744060805,
        "0123456789abcdef0123456789abcdef",
        "1744060805|0123456789abcdef0123456789abcdef");
    const auto state = build_resolver_actual_state(true, true, probe, 1744060806);
    CHECK(state.live_status == api::ResolverLiveStatus::HEALTHY);
    CHECK(state.last_probe_ts == std::optional<std::int64_t>{1744060806});
    CHECK(state.actual_hash == "0123456789abcdef0123456789abcdef");
    CHECK(state.actual_ts == std::optional<std::int64_t>{1744060805});
    CHECK(classify_resolver_config_sync_state(state.actual_ts, 1744060800, 1744060806, true) ==
          std::optional<api::ResolverConfigSyncState>{api::ResolverConfigSyncState::CONVERGED});
}

TEST_CASE("resolver health: older live TXT during apply stays healthy and converging") {
    const auto probe = make_probe_result(
        ResolverConfigHashProbeStatus::SUCCESS,
        1744060800,
        "0123456789abcdef0123456789abcdef");
    const auto state = build_resolver_actual_state(true, true, probe, 1744060806);
    CHECK(state.live_status == api::ResolverLiveStatus::HEALTHY);
    CHECK(state.actual_hash == "0123456789abcdef0123456789abcdef");
    CHECK(state.actual_ts == std::optional<std::int64_t>{1744060800});
    CHECK(classify_resolver_config_sync_state(state.actual_ts, 1744060805, 1744060806, false) ==
          std::optional<api::ResolverConfigSyncState>{api::ResolverConfigSyncState::CONVERGING});
}

TEST_CASE("resolver health: query failure clears actual state and reports unavailable") {
    const auto probe = make_probe_result(ResolverConfigHashProbeStatus::QUERY_FAILED);
    const auto state = build_resolver_actual_state(true, true, probe, 1744060806);
    CHECK(state.live_status == api::ResolverLiveStatus::UNAVAILABLE);
    CHECK(state.last_probe_ts == std::optional<std::int64_t>{1744060806});
    CHECK(state.actual_hash.empty());
    CHECK_FALSE(state.actual_ts.has_value());
}

TEST_CASE("resolver health: missing or invalid TXT reports degraded") {
    const auto missing_state = build_resolver_actual_state(
        true,
        true,
        make_probe_result(ResolverConfigHashProbeStatus::NO_USABLE_TXT),
        1744060806);
    CHECK(missing_state.live_status == api::ResolverLiveStatus::DEGRADED);
    CHECK(missing_state.actual_hash.empty());
    CHECK_FALSE(missing_state.actual_ts.has_value());

    const auto invalid_state = build_resolver_actual_state(
        true,
        true,
        make_probe_result(ResolverConfigHashProbeStatus::INVALID_TXT, std::nullopt, "not-a-md5"),
        1744060807);
    CHECK(invalid_state.live_status == api::ResolverLiveStatus::DEGRADED);
    CHECK(invalid_state.actual_hash.empty());
    CHECK_FALSE(invalid_state.actual_ts.has_value());
}

TEST_CASE("resolver health: stopped runtime or missing resolver config reports unknown") {
    const auto probe = make_probe_result(
        ResolverConfigHashProbeStatus::SUCCESS,
        1744060805,
        "0123456789abcdef0123456789abcdef");
    CHECK(build_resolver_actual_state(false, true, probe, 1744060806).live_status ==
          api::ResolverLiveStatus::UNKNOWN);
    CHECK(build_resolver_actual_state(true, false, probe, 1744060806).live_status ==
          api::ResolverLiveStatus::UNKNOWN);
    CHECK(build_resolver_actual_state(true, true, std::nullopt, std::nullopt).live_status ==
          api::ResolverLiveStatus::UNKNOWN);
}

TEST_CASE("resolver sync state machine: fresh apply converges after matching TXT") {
    ResolverSyncStateMachine machine;
    machine.apply_started(100, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");

    auto snapshot = machine.snapshot(101);
    CHECK(snapshot.sync_state ==
          std::optional<api::ResolverConfigSyncState>{api::ResolverConfigSyncState::CONVERGING});
    CHECK(snapshot.live_status == api::ResolverLiveStatus::HEALTHY);

    machine.probe_succeeded("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", 101, 102);
    snapshot = machine.snapshot(102);
    CHECK(snapshot.sync_state ==
          std::optional<api::ResolverConfigSyncState>{api::ResolverConfigSyncState::CONVERGED});
    CHECK(snapshot.probe_status == api::ResolverConfigProbeStatus::SUCCESS);
    CHECK(snapshot.live_status == api::ResolverLiveStatus::HEALTHY);
}

TEST_CASE("resolver sync state machine: hash mismatch after window is stale") {
    ResolverSyncStateMachine machine;
    machine.apply_started(100, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    machine.probe_succeeded("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", 101, 102);

    const auto snapshot = machine.snapshot(120);
    CHECK(snapshot.sync_state ==
          std::optional<api::ResolverConfigSyncState>{api::ResolverConfigSyncState::STALE});
    CHECK(snapshot.live_status == api::ResolverLiveStatus::HEALTHY);
}

TEST_CASE("resolver sync state machine: missing or invalid TXT becomes stale not unavailable") {
    ResolverSyncStateMachine missing_machine;
    missing_machine.apply_started(100, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    missing_machine.probe_failed(ResolverConfigHashProbeStatus::NO_USABLE_TXT, 120);
    auto snapshot = missing_machine.snapshot(120);
    CHECK(snapshot.sync_state ==
          std::optional<api::ResolverConfigSyncState>{api::ResolverConfigSyncState::STALE});
    CHECK(snapshot.probe_status == api::ResolverConfigProbeStatus::MISSING_TXT);
    CHECK(snapshot.live_status == api::ResolverLiveStatus::DEGRADED);

    ResolverSyncStateMachine invalid_machine;
    invalid_machine.apply_started(100, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    invalid_machine.probe_failed(ResolverConfigHashProbeStatus::INVALID_TXT, 120);
    snapshot = invalid_machine.snapshot(120);
    CHECK(snapshot.sync_state ==
          std::optional<api::ResolverConfigSyncState>{api::ResolverConfigSyncState::STALE});
    CHECK(snapshot.probe_status == api::ResolverConfigProbeStatus::INVALID_TXT);
    CHECK(snapshot.live_status == api::ResolverLiveStatus::DEGRADED);
}

TEST_CASE("resolver sync state machine: missing TXT during apply stays converging") {
    ResolverSyncStateMachine machine;
    machine.apply_started(100, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    machine.probe_failed(ResolverConfigHashProbeStatus::NO_USABLE_TXT, 101);

    const auto snapshot = machine.snapshot(105);
    CHECK(snapshot.sync_state ==
          std::optional<api::ResolverConfigSyncState>{api::ResolverConfigSyncState::CONVERGING});
    CHECK(snapshot.probe_status == api::ResolverConfigProbeStatus::MISSING_TXT);
    CHECK(snapshot.live_status == api::ResolverLiveStatus::HEALTHY);
}

TEST_CASE("resolver sync state machine: query failure is probe error without stale claim") {
    ResolverSyncStateMachine machine;
    machine.apply_started(100, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    machine.probe_failed(ResolverConfigHashProbeStatus::QUERY_FAILED, 101);

    auto snapshot = machine.snapshot(105);
    CHECK(snapshot.sync_state ==
          std::optional<api::ResolverConfigSyncState>{api::ResolverConfigSyncState::CONVERGING});
    CHECK(snapshot.probe_status == api::ResolverConfigProbeStatus::QUERY_FAILED);

    snapshot = machine.snapshot(120);
    CHECK_FALSE(snapshot.sync_state.has_value());
    CHECK(snapshot.probe_status == api::ResolverConfigProbeStatus::QUERY_FAILED);
    CHECK(snapshot.live_status == api::ResolverLiveStatus::UNAVAILABLE);
}

TEST_CASE("resolver sync state machine: stopped or not configured is unknown") {
    ResolverSyncStateMachine stopped_machine;
    stopped_machine.apply_started(100, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    stopped_machine.runtime_stopped();
    auto snapshot = stopped_machine.snapshot(120);
    CHECK_FALSE(snapshot.sync_state.has_value());
    CHECK(snapshot.probe_status == api::ResolverConfigProbeStatus::NOT_CONFIGURED);
    CHECK(snapshot.live_status == api::ResolverLiveStatus::UNKNOWN);

    ResolverSyncStateMachine missing_config_machine;
    missing_config_machine.expected_hash_updated("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    missing_config_machine.resolver_not_configured();
    snapshot = missing_config_machine.snapshot(120);
    CHECK_FALSE(snapshot.sync_state.has_value());
    CHECK(snapshot.probe_status == api::ResolverConfigProbeStatus::NOT_CONFIGURED);
    CHECK(snapshot.live_status == api::ResolverLiveStatus::UNKNOWN);
}
