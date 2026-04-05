#ifdef WITH_API

#include "handler_test_routing.hpp"
#include "../cmd/test_routing.hpp"
#include "generated/api_types.hpp"

#include <nlohmann/json.hpp>
#include <stdexcept>

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

        auto result = ctx.compute_test_routing(req.target);

        api::RoutingTestResponse resp;
        resp.target       = result.target;
        resp.is_domain    = result.is_domain;
        resp.dns_error    = result.dns_error;
        resp.no_matching_rule = result.no_matching_rule;
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

        for (const auto& rule_diag : result.rule_diagnostics) {
            api::RoutingTestRuleDiagnosticElement rd;
            rd.rule_index = rule_diag.rule_index;
            rd.outbound = rule_diag.outbound;
            rd.interface_name = rule_diag.interface_name;
            rd.target_in_lists = rule_diag.target_in_lists;
            if (rule_diag.target_match) {
                api::ListMatch lm;
                lm.list = rule_diag.target_match->list_name;
                lm.via  = rule_diag.target_match->via;
                rd.target_match = std::move(lm);
            }
            for (const auto& ip_diag : rule_diag.ip_rows) {
                api::RoutingTestRuleIpDiagnosticElement ipd;
                ipd.ip = ip_diag.ip;
                ipd.in_ipset = ip_diag.in_ipset;
                rd.ip_rows.push_back(std::move(ipd));
            }
            resp.rule_diagnostics.push_back(std::move(rd));
        }

        nlohmann::json out;
        api::to_json(out, resp);
        return out.dump();
    });
}

} // namespace keen_pbr3

#endif // WITH_API
