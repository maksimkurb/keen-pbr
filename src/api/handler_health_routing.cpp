#ifdef WITH_API

#include "handler_health_routing.hpp"

#include <nlohmann/json.hpp>
#include <stdexcept>

#include "../health/routing_health_checker.hpp"

namespace keen_pbr3 {

void register_health_routing_handler(ApiServer& server, ApiContext& ctx) {
    // GET /api/health/routing - verify live routing and firewall state against expected config.
    // RoutingHealthChecker::check() catches all internal exceptions; if it still throws,
    // the server wrapper returns HTTP 500. The JSON body contains "overall":"ok"/"degraded"/"error".
    server.get("/api/health/routing", [&ctx]() -> std::string {
        try {
            auto report = ctx.routing_health_checker.check();
            return routing_health_report_to_json(report).dump();
        } catch (const std::exception& e) {
            nlohmann::json err;
            err["overall"] = "error";
            err["error"] = e.what();
            // Re-throw so the server wrapper sets HTTP 500
            throw std::runtime_error(err.dump());
        }
    });
}

} // namespace keen_pbr3

#endif // WITH_API
