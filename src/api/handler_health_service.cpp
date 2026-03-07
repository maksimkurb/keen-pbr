#ifdef WITH_API

#include "handler_health_service.hpp"
#include "handler_helpers.hpp"
#include "generated/api_types.hpp"

#include <keen-pbr3/version.hpp>
#include <nlohmann/json.hpp>

namespace keen_pbr3 {

void register_health_service_handler(ApiServer& server, ApiContext& ctx) {
    // GET /api/health/service - daemon version/status + health for all outbounds
    server.get("/api/health/service", [&ctx]() -> std::string {
        api::HealthResponse resp;
        resp.version = KEEN_PBR3_VERSION_STRING;
        resp.status = "running";

        for (const auto& ob : ctx.outbounds) {
            api::HealthEntry entry;
            entry.tag = outbound_tag(ob);
            entry.type = outbound_type(ob);

            if (std::holds_alternative<UrltestOutbound>(ob)) {
                // Report urltest state: per-child latencies, circuit breaker states, selected outbound
                try {
                    const auto& state = ctx.urltest_manager.get_state(entry.tag);

                    std::vector<api::HealthChild> children;
                    for (const auto& [child_tag, result] : state.last_results) {
                        api::HealthChild child;
                        child.tag = child_tag;
                        child.success = result.success;
                        child.latency_ms = result.latency_ms;
                        if (!result.error.empty()) {
                            child.error = result.error;
                        }

                        auto cb_it = state.circuit_breakers.find(child_tag);
                        if (cb_it != state.circuit_breakers.end()) {
                            child.circuit_breaker = circuit_state_string(
                                cb_it->second.state(child_tag));
                        }

                        children.push_back(std::move(child));
                    }

                    entry.children = std::move(children);
                    entry.selected_outbound = state.selected_outbound;
                    entry.status = state.selected_outbound.empty() ? "degraded" : "healthy";
                } catch (const std::out_of_range&) {
                    entry.status = "unknown";
                }
            } else {
                // Interface/table outbounds are always considered healthy
                entry.status = "healthy";
            }

            resp.outbounds.push_back(std::move(entry));
        }

        return nlohmann::json(resp).dump();
    });
}

} // namespace keen_pbr3

#endif // WITH_API
