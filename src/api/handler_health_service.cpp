#ifdef WITH_API

#include "handler_health_service.hpp"
#include "generated/api_types.hpp"

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
        resp.resolver_config_hash = service_health.resolver_config_hash;
        resp.resolver_config_hash_actual = service_health.resolver_config_hash_actual;
        resp.resolver_config_hash_actual_ts = service_health.resolver_config_hash_actual_ts;
        resp.resolver_live_status = service_health.resolver_live_status;
        resp.resolver_config_probe_status = service_health.resolver_config_probe_status;
        resp.resolver_last_probe_ts = service_health.resolver_last_probe_ts;
        resp.apply_started_ts = service_health.apply_started_ts;
        resp.resolver_config_sync_state = service_health.resolver_config_sync_state;

        resp.config_is_draft = service_health.config_is_draft;
        return resp;
}

void register_health_service_handler(ApiServer& server, ApiContext& ctx) {
    // GET /api/health/service - daemon version/status + resolver/config summary
    server.get("/api/health/service", [&ctx]() -> std::string {
        const ServiceHealthState health = ctx.get_service_health();
        nlohmann::json response = build_health_response(health);
        if (health.lifecycle_operation) {
            response["lifecycle_operation"] = lifecycle_operation_json(*health.lifecycle_operation);
        }
        return response.dump();
    });
}

} // namespace keen_pbr3

#endif // WITH_API
