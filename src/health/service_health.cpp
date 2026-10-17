#include "service_health.hpp"
#include "../dns/doh_support.hpp"

#include <keen-pbr/version.hpp>
#include <nlohmann/json.hpp>

namespace keen_pbr3 {

namespace {

api::RuntimeState to_api_runtime_state(const std::string& state) {
    if (state == "starting") return api::RuntimeState::STARTING;
    if (state == "running") return api::RuntimeState::RUNNING;
    if (state == "restart_required") return api::RuntimeState::RESTART_REQUIRED;
    if (state == "applying") return api::RuntimeState::APPLYING;
    if (state == "stopped") return api::RuntimeState::STOPPED;
    if (state == "broken") return api::RuntimeState::BROKEN;
    return api::RuntimeState::SHUTTING_DOWN;
}

} // namespace

namespace {
nlohmann::json lifecycle_operation_json(const LifecycleOperationSnapshot& operation) {
    nlohmann::json stages = nlohmann::json::array();
    for (const auto& stage : operation.stages) {
        stages.push_back({{"id", stage.id}, {"title", stage.title},
                          {"status", lifecycle_operation_status_name(stage.status)},
                          {"detail", stage.detail}});
    }
    nlohmann::json result{{"id", operation.id},
                          {"type", lifecycle_operation_type_name(operation.type)},
                          {"status", lifecycle_operation_result_name(operation.result)},
                          {"started_at", operation.started_at},
                          {"stages", std::move(stages)}};
    if (operation.finished_at) result["finished_at"] = *operation.finished_at;
    if (!operation.error.empty()) result["error"] = operation.error;
    return result;
}
} // namespace

api::HealthResponse build_health_response(const ServiceHealthState& service_health) {
        api::HealthResponse resp;
        resp.version = KEEN_PBR3_VERSION_STRING;
        resp.build = KEEN_PBR3_VERSION_RELEASE_STRING;
        resp.status = service_health.status;
        resp.runtime_state = to_api_runtime_state(service_health.runtime_state);
        resp.runtime_state_reason = service_health.runtime_state_reason;
        resp.os_type = service_health.os_type;
        resp.os_version = service_health.os_version;
        resp.build_variant = service_health.build_variant;
        resp.doh_supported = kDohBuiltIn;
        resp.apply_started_ts = service_health.apply_started_ts;

        if (service_health.lifecycle_operation) {
            resp.lifecycle_operation = lifecycle_operation_json(*service_health.lifecycle_operation)
                                           .get<api::LifecycleOperation>();
        }

        resp.intercept = service_health.intercept;
        resp.dnsmasq = service_health.dnsmasq;
        resp.config_is_draft = service_health.config_is_draft;
        resp.rollback_available = service_health.rollback_available;
        return resp;
}

api::DnsmasqHealth to_dnsmasq_health(const DnsmasqStatus& status) {
    api::DnsmasqHealth health;
    health.mode = status.mode;
    switch (status.state) {
    case DnsmasqSyncState::Disabled: health.state = api::State::DISABLED; break;
    case DnsmasqSyncState::Ok: health.state = api::State::OK; break;
    case DnsmasqSyncState::Applying: health.state = api::State::APPLYING; break;
    case DnsmasqSyncState::Reconciling: health.state = api::State::RECONCILING; break;
    case DnsmasqSyncState::Error: health.state = api::State::ERROR; break;
    }
    if (!status.config_hash.empty()) health.config_hash = status.config_hash;
    switch (status.probe_state) {
    case DnsmasqProbeState::NotChecked: health.probe_status = api::ProbeStatus::NOT_CHECKED; break;
    case DnsmasqProbeState::Ok: health.probe_status = api::ProbeStatus::OK; break;
    case DnsmasqProbeState::Missing: health.probe_status = api::ProbeStatus::MISSING; break;
    case DnsmasqProbeState::Invalid: health.probe_status = api::ProbeStatus::INVALID; break;
    case DnsmasqProbeState::QueryFailed: health.probe_status = api::ProbeStatus::QUERY_FAILED; break;
    }
    if (!status.loaded_hash.empty()) health.loaded_hash = status.loaded_hash;
    health.loaded_boottime_ms = status.loaded_boottime_ms;
    health.loaded_ts = status.loaded_ts;
    health.last_check_ts = status.last_check_ts;
    health.last_external_reload_ts = status.last_external_reload_ts;
    health.last_apply_ts = status.last_apply_ts;
    if (!status.last_error.empty()) health.last_error = status.last_error;
    if (status.state != DnsmasqSyncState::Disabled) {
        health.repair_attempt = status.repair_attempt;
        health.repair_max_attempts = status.repair_max_attempts;
        health.repair_paused = status.repair_paused;
    }
    if (!status.repair_reason.empty()) health.repair_reason = status.repair_reason;
    health.next_repair_ts = status.next_repair_ts;
    if (status.dnsmasq_alive) {
        switch (*status.dnsmasq_alive) {
        case DnsmasqLiveness::Alive: health.dnsmasq_alive = api::DnsmasqAlive::ALIVE; break;
        case DnsmasqLiveness::Dead: health.dnsmasq_alive = api::DnsmasqAlive::DEAD; break;
        case DnsmasqLiveness::Unknown: health.dnsmasq_alive = api::DnsmasqAlive::UNKNOWN; break;
        }
    }
    health.rules = static_cast<std::int64_t>(status.rules);
    health.domains = static_cast<std::int64_t>(status.domains);
    if (status.error_code != DnsmasqErrorCode::None) {
        health.error_code = dnsmasq_error_code_name(status.error_code);
    }
    if (!status.selector.empty()) health.instance_selector = status.selector;
    switch (status.selection) {
    case DnsmasqSelectionKind::None: break;
    case DnsmasqSelectionKind::Explicit: health.instance_selection = api::InstanceSelection::EXPLICIT; break;
    case DnsmasqSelectionKind::Adopted: health.instance_selection = api::InstanceSelection::ADOPTED; break;
    case DnsmasqSelectionKind::Automatic: health.instance_selection = api::InstanceSelection::AUTOMATIC; break;
    }
    if (status.endpoint) health.endpoint = status.endpoint->str();
    switch (status.endpoint_evidence) {
    case EndpointEvidence::None: health.endpoint_evidence = api::EndpointEvidence::NONE; break;
    case EndpointEvidence::SocketOwned: health.endpoint_evidence = api::EndpointEvidence::SOCKET_OWNED; break;
    case EndpointEvidence::Responding: health.endpoint_evidence = api::EndpointEvidence::RESPONDING; break;
    case EndpointEvidence::StampVerified: health.endpoint_evidence = api::EndpointEvidence::STAMP_VERIFIED; break;
    }
    if (status.pid) health.pid = static_cast<std::int64_t>(*status.pid);
    switch (status.capability) {
    case DnsmasqCapabilityStatus::NotChecked: health.capability = api::Capability::NOT_CHECKED; break;
    case DnsmasqCapabilityStatus::Ok: health.capability = api::Capability::OK; break;
    case DnsmasqCapabilityStatus::Unsupported: health.capability = api::Capability::UNSUPPORTED; break;
    case DnsmasqCapabilityStatus::Failed: health.capability = api::Capability::FAILED; break;
    }
    if (status.preflight == "ok") health.preflight = api::Preflight::OK;
    else if (status.preflight == "failed") health.preflight = api::Preflight::FAILED;
    else if (status.preflight == "skipped") health.preflight = api::Preflight::SKIPPED;
    else if (status.preflight == "not_run") health.preflight = api::Preflight::NOT_RUN;
    health.instance_count = static_cast<std::int64_t>(status.instances);
    if (!status.warnings.empty()) health.warnings = status.warnings;
    return health;
}


} // namespace keen_pbr3
