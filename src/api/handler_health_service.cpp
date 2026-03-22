#ifdef WITH_API

#include "handler_health_service.hpp"
#include "generated/api_types.hpp"

#include <keen-pbr/version.hpp>
#include <nlohmann/json.hpp>
#include <shared_mutex>

namespace keen_pbr3 {

void register_health_service_handler(ApiServer& server, ApiContext& ctx) {
    // GET /api/health/service - daemon version/status + health for all outbounds
    server.get("/api/health/service", [&ctx]() -> std::string {
        std::shared_lock<std::shared_mutex> lock(ctx.state_mutex);
        api::HealthResponse resp;
        resp.version = KEEN_PBR3_VERSION_STRING;
        resp.status = api::HealthResponseStatus::RUNNING;
        resp.resolver_config_hash = ctx.resolver_config_hash_fn();
        resp.resolver_config_hash_actual = ctx.resolver_config_hash_actual_fn();

        for (const auto& ob : ctx.outbounds_fn()) {
            api::HealthEntry entry;
            entry.tag = ob.tag;
            entry.type = ob.type;

            if (ob.type == OutboundType::URLTEST) {
                // Report urltest state: per-child latencies, circuit breaker states, selected outbound
                try {
                    const auto state_opt = ctx.urltest_state_fn(entry.tag);
                    if (!state_opt.has_value()) {
                        entry.status = api::HealthEntryStatus::UNKNOWN;
                        resp.outbounds.push_back(std::move(entry));
                        continue;
                    }
                    const auto& state = *state_opt;

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
                            switch (cb_it->second.state(child_tag)) {
                                case CircuitState::closed:
                                    child.circuit_breaker = api::CircuitBreaker::CLOSED; break;
                                case CircuitState::open:
                                    child.circuit_breaker = api::CircuitBreaker::OPEN; break;
                                case CircuitState::half_open:
                                    child.circuit_breaker = api::CircuitBreaker::HALF_OPEN; break;
                            }
                        }

                        children.push_back(std::move(child));
                    }

                    entry.children = std::move(children);
                    entry.selected_outbound = state.selected_outbound;
                    entry.status = state.selected_outbound.empty()
                        ? api::HealthEntryStatus::DEGRADED
                        : api::HealthEntryStatus::HEALTHY;
                } catch (const std::exception&) {
                    entry.status = api::HealthEntryStatus::UNKNOWN;
                }
            } else {
                // Interface/table outbounds are always considered healthy
                entry.status = api::HealthEntryStatus::HEALTHY;
            }

            resp.outbounds.push_back(std::move(entry));
        }

        nlohmann::json response = resp;
        response["config_is_draft"] = ctx.config_is_draft_fn();
        return response.dump();
    });
}

} // namespace keen_pbr3

#endif // WITH_API
