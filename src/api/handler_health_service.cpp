#ifdef WITH_API

#include "handler_health_service.hpp"
#include "generated/api_types.hpp"

#include <keen-pbr/version.hpp>
#include <nlohmann/json.hpp>
#include <shared_mutex>

namespace keen_pbr3 {

void register_health_service_handler(ApiServer& server, ApiContext& ctx) {
    // GET /api/health/service - daemon version/status + resolver/config summary
    server.get("/api/health/service", [&ctx]() -> std::string {
        std::shared_lock<std::shared_mutex> lock(ctx.state_mutex);
        api::HealthResponse resp;
        resp.version = KEEN_PBR3_VERSION_STRING;
        resp.status = api::HealthResponseStatus::RUNNING;
        resp.resolver_config_hash = ctx.resolver_config_hash_fn();
        resp.resolver_config_hash_actual = ctx.resolver_config_hash_actual_fn();

        nlohmann::json response = resp;
        response["config_is_draft"] = ctx.config_is_draft_fn();
        return response.dump();
    });
}

} // namespace keen_pbr3

#endif // WITH_API
