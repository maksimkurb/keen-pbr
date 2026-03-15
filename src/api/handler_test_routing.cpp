#ifdef WITH_API

#include "handler_test_routing.hpp"
#include "../cmd/test_routing.hpp"
#include "generated/api_types.hpp"

#include <nlohmann/json.hpp>
#include <stdexcept>
#include <shared_mutex>

namespace keen_pbr3 {

void register_test_routing_handler(ApiServer& server, ApiContext& ctx) {
    server.post("/api/routing/test", [&ctx](const std::string& body) -> std::string {
        nlohmann::json j;
        try {
            j = nlohmann::json::parse(body);
        } catch (...) {
            throw std::runtime_error(
                nlohmann::json{{"error", "invalid JSON body"}}.dump());
        }

        api::RoutingTestRequest req;
        try {
            api::from_json(j, req);
        } catch (const std::exception& e) {
            throw std::runtime_error(
                nlohmann::json{{"error", std::string("invalid request: ") + e.what()}}.dump());
        }

        std::shared_lock<std::shared_mutex> lock(ctx.state_mutex);
        Config visible_config = ctx.visible_config_fn();
        auto result = compute_test_routing(visible_config, ctx.cache_manager, req.target);

        api::RoutingTestResponse resp;
        resp.target       = result.target;
        resp.is_domain    = result.is_domain;
        resp.resolved_ips = result.resolved_ips;
        resp.warnings     = result.warnings;

        for (const auto& entry : result.entries) {
            api::RoutingTestEntry e;
            e.ip                = entry.ip;
            e.expected_outbound = entry.expected_outbound;
            e.actual_outbound   = entry.actual_outbound;
            e.ok                = entry.ok;
            if (entry.list_match) {
                api::ListMatch lm;
                lm.list = entry.list_match->list_name;
                lm.via  = entry.list_match->via;
                e.list_match = std::move(lm);
            }
            resp.results.push_back(std::move(e));
        }

        nlohmann::json out;
        api::to_json(out, resp);
        return out.dump();
    });
}

} // namespace keen_pbr3

#endif // WITH_API
