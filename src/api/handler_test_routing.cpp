#ifdef WITH_API

#include "handler_test_routing.hpp"
#include "../cmd/test_routing.hpp"

#include <nlohmann/json.hpp>
#include <stdexcept>

namespace keen_pbr3 {

void register_test_routing_handler(ApiServer& server, ApiContext& ctx) {
    server.post("/api/routing/test", [&ctx](const std::string& body) -> std::string {
        nlohmann::json req;
        try {
            req = nlohmann::json::parse(body);
        } catch (...) {
            throw std::runtime_error(
                nlohmann::json{{"error", "invalid JSON body"}}.dump());
        }

        if (!req.contains("target") || !req["target"].is_string()) {
            throw std::runtime_error(
                nlohmann::json{{"error", "missing or invalid 'target' field"}}.dump());
        }
        const std::string target = req["target"].get<std::string>();

        auto result = compute_test_routing(ctx.config, ctx.cache_manager, target);

        nlohmann::json resp;
        resp["target"]       = result.target;
        resp["is_domain"]    = result.is_domain;
        resp["resolved_ips"] = result.resolved_ips;
        resp["warnings"]     = result.warnings;

        nlohmann::json entries = nlohmann::json::array();
        for (const auto& entry : result.entries) {
            nlohmann::json e;
            e["ip"] = entry.ip;
            if (entry.list_match) {
                e["list_match"] = {{"list", entry.list_match->list_name},
                                   {"via",  entry.list_match->via}};
            } else {
                e["list_match"] = nullptr;
            }
            e["expected_outbound"] = entry.expected_outbound;
            e["actual_outbound"]   = entry.actual_outbound;
            e["ok"]                = entry.ok;
            entries.push_back(std::move(e));
        }
        resp["results"] = std::move(entries);

        return resp.dump();
    });
}

} // namespace keen_pbr3

#endif // WITH_API
