#ifdef WITH_API

#include "handler_health_routing.hpp"
#include "generated/api_types.hpp"

#include <nlohmann/json.hpp>

#include "../health/routing_health_checker.hpp"

namespace keen_pbr3 {

void register_health_routing_handler(ApiServer& server, ApiContext& ctx) {
    // GET /api/health/routing - return the daemon's cached canonical report.
    // A cold or stale cache is refreshed off-loop and reported as degraded/pending.
    server.get("/api/health/routing", [&ctx]() -> std::string {
        try {
            auto report = ctx.get_routing_health();
            return routing_health_report_to_json(report).dump();
        } catch (const std::exception& e) {
            api::RoutingHealthErrorResponse err;
            err.error = e.what();
            err.overall = api::RoutingHealthErrorResponseOverall::ERROR;
            throw ApiError("Routing health check failed", 500, nlohmann::json(err).dump());
        }
    });
}

} // namespace keen_pbr3

#endif // WITH_API
