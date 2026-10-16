#ifdef WITH_API

#include "handler_health_service.hpp"

#include <nlohmann/json.hpp>

namespace keen_pbr3 {

void register_health_service_handler(ApiServer& server, ApiContext& ctx) {
    // GET /api/health/service - daemon version/status + config summary
    server.get("/api/health/service", [&ctx]() -> std::string {
        return nlohmann::json(build_health_response(ctx.get_service_health())).dump();
    });
}

} // namespace keen_pbr3

#endif // WITH_API
