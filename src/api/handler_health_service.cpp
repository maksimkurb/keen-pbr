#ifdef WITH_API

#include "handler_health_service.hpp"
#include "handler_helpers.hpp"

#include <keen-pbr3/version.hpp>
#include <nlohmann/json.hpp>

namespace keen_pbr3 {

void register_health_service_handler(ApiServer& server, ApiContext& ctx) {
    // GET /api/health/service - daemon version/status + health for all outbounds
    server.get("/api/health/service", [&ctx]() -> std::string {
        nlohmann::json j;
        j["version"] = KEEN_PBR3_VERSION_STRING;
        j["status"] = "running";

        nlohmann::json results = nlohmann::json::array();

        for (const auto& ob : ctx.outbounds) {
            nlohmann::json entry;
            std::string tag = outbound_tag(ob);
            entry["tag"] = tag;
            entry["type"] = outbound_type(ob);

            if (std::holds_alternative<UrltestOutbound>(ob)) {
                // Report urltest state: per-child latencies, circuit breaker states, selected outbound
                try {
                    const auto& state = ctx.urltest_manager.get_state(tag);
                    entry["selected_outbound"] = state.selected_outbound;

                    nlohmann::json children = nlohmann::json::array();
                    for (const auto& [child_tag, result] : state.last_results) {
                        nlohmann::json child_j;
                        child_j["tag"] = child_tag;
                        child_j["success"] = result.success;
                        child_j["latency_ms"] = result.latency_ms;
                        if (!result.error.empty()) {
                            child_j["error"] = result.error;
                        }

                        auto cb_it = state.circuit_breakers.find(child_tag);
                        if (cb_it != state.circuit_breakers.end()) {
                            child_j["circuit_breaker"] = circuit_state_string(
                                cb_it->second.state(child_tag));
                        }

                        children.push_back(child_j);
                    }
                    entry["children"] = children;
                    entry["status"] = state.selected_outbound.empty() ? "degraded" : "healthy";
                } catch (const std::out_of_range&) {
                    entry["status"] = "unknown";
                }
            } else {
                // Interface/table outbounds are always considered healthy
                entry["status"] = "healthy";
            }

            results.push_back(entry);
        }

        j["outbounds"] = results;
        return j.dump();
    });
}

} // namespace keen_pbr3

#endif // WITH_API
