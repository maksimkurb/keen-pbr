#ifdef WITH_API

#include "handler_health_service.hpp"
#include "generated/api_types.hpp"

#include <keen-pbr/version.hpp>
#include <nlohmann/json.hpp>

namespace keen_pbr3 {

void register_health_service_handler(ApiServer& server, ApiContext& ctx) {
    // GET /api/health/service - daemon version/status + resolver/config summary
    server.get("/api/health/service", [&ctx]() -> std::string {
        const auto service_health = ctx.get_service_health();
        api::HealthResponse resp;
        resp.version = KEEN_PBR3_VERSION_STRING;
        resp.build = KEEN_PBR3_VERSION_RELEASE_STRING;
        resp.status = service_health.status;
        resp.os_type = service_health.os_type;
        resp.os_version = service_health.os_version;
        resp.build_variant = service_health.build_variant;
        resp.resolver_config_hash = service_health.resolver_config_hash;
        resp.resolver_config_hash_actual = service_health.resolver_config_hash_actual;
        resp.resolver_config_hash_actual_ts = service_health.resolver_config_hash_actual_ts;
        resp.resolver_live_status = service_health.resolver_live_status;
        resp.resolver_last_probe_ts = service_health.resolver_last_probe_ts;
        resp.apply_started_ts = service_health.apply_started_ts;
        resp.resolver_config_sync_state = service_health.resolver_config_sync_state;

        nlohmann::json response = resp;
        response["config_is_draft"] = service_health.config_is_draft;
        return response.dump();
    });
}

} // namespace keen_pbr3

#endif // WITH_API
