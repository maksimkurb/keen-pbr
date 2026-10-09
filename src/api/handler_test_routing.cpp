#ifdef WITH_API

#include "handler_test_routing.hpp"
#include "../cmd/test_routing.hpp"
#include "generated/api_types.hpp"
#include "../util/format_compat.hpp"

#include <arpa/inet.h>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace keen_pbr3 {

namespace {

[[noreturn]] void invalid_field(const std::string& message) {
    nlohmann::json payload = {{"error", message}};
    throw ApiError(message, 400, payload.dump());
}

bool is_ip_address(const std::string& value) {
    in_addr addr4{};
    in6_addr addr6{};
    return inet_pton(AF_INET, value.c_str(), &addr4) == 1 ||
           inet_pton(AF_INET6, value.c_str(), &addr6) == 1;
}

bool is_ipv4_address(const std::string& value) {
    in_addr addr4{};
    return inet_pton(AF_INET, value.c_str(), &addr4) == 1;
}

void validate_port(const std::optional<std::int64_t>& value, const char* field) {
    if (!value.has_value()) return;
    if (*value < 1 || *value > 65535) {
        invalid_field(keen_pbr3::format("Field '{}' must be between 1 and 65535", field));
    }
}

void validate_integer_field(const nlohmann::json& body,
                            const char* field,
                            std::int64_t minimum,
                            std::int64_t maximum) {
    const auto it = body.find(field);
    if (it == body.end() || it->is_null()) return;
    if (!it->is_number_integer() && !it->is_number_unsigned()) {
        invalid_field(keen_pbr3::format("Field '{}' must be an integer", field));
    }
    if (it->is_number_unsigned()) {
        const auto value = it->get<std::uint64_t>();
        if (value < static_cast<std::uint64_t>(minimum) ||
            value > static_cast<std::uint64_t>(maximum)) {
            invalid_field(keen_pbr3::format(
                "Field '{}' must be between {} and {}", field, minimum, maximum));
        }
        return;
    }
    const auto value = it->get<std::int64_t>();
    if (value < minimum || value > maximum) {
        invalid_field(keen_pbr3::format(
            "Field '{}' must be between {} and {}", field, minimum, maximum));
    }
}

} // namespace

void register_test_routing_handler(ApiServer& server, ApiContext& ctx) {
    server.post("/api/routing/test", [&ctx](const std::string& body) -> std::string {
        nlohmann::json j;
        try {
            j = nlohmann::json::parse(body);
        } catch (const nlohmann::json::exception&) {
            nlohmann::json payload = {{"error", "Invalid request body"}};
            throw ApiError("Invalid request body", 400, payload.dump());
        }
        if (!j.is_object()) {
            invalid_field("Invalid request body");
        }

        validate_integer_field(j, "dest_port", 1, 65535);
        validate_integer_field(j, "src_port", 1, 65535);
        validate_integer_field(j, "dscp", 0, 63);

        api::RoutingTestRequest req;
        try {
            api::from_json(j, req);
        } catch (const std::exception&) {
            nlohmann::json payload = {{"error", "Invalid request body"}};
            throw ApiError("Invalid request body", 400, payload.dump());
        }

        if (req.target.empty()) {
            invalid_field("Field 'target' must not be empty");
        }

        validate_port(req.dest_port, "dest_port");
        validate_port(req.src_port, "src_port");
        if (req.dscp.has_value() && (*req.dscp < 0 || *req.dscp > 63)) {
            invalid_field("Field 'dscp' must be between 0 and 63");
        }
        if (req.src_addr.has_value() && !is_ip_address(*req.src_addr)) {
            invalid_field("Field 'src_addr' must be a valid IPv4 or IPv6 address");
        }
        if (req.src_addr.has_value() && is_ip_address(req.target) &&
            is_ipv4_address(*req.src_addr) != is_ipv4_address(req.target)) {
            invalid_field("Field 'src_addr' must use the same address family as 'target'");
        }

        TestRoutingCriteria criteria;
        if (req.proto.has_value()) {
            switch (*req.proto) {
            case api::Proto::TCP: criteria.proto = "tcp"; break;
            case api::Proto::UDP: criteria.proto = "udp"; break;
            case api::Proto::OTHER: criteria.proto = "other"; break;
            }
        }
        criteria.src_addr = req.src_addr;
        if (req.dest_port.has_value()) {
            criteria.dest_port = static_cast<std::uint16_t>(*req.dest_port);
        }
        if (req.src_port.has_value()) {
            criteria.src_port = static_cast<std::uint16_t>(*req.src_port);
        }
        if (req.dscp.has_value()) {
            criteria.dscp = static_cast<std::uint8_t>(*req.dscp);
        }

        auto result = ctx.compute_test_routing(req.target, criteria);

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
            e.matched_rule_index = entry.matched_rule_index;
            e.criteria_match = entry.criteria_match;
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
            rd.rule = rule_diag.rule;
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
                ipd.in_lists = ip_diag.in_lists;
                if (ip_diag.list_match) {
                    api::ListMatch lm;
                    lm.list = ip_diag.list_match->list_name;
                    lm.via = ip_diag.list_match->via;
                    ipd.list_match = std::move(lm);
                }
                ipd.criteria_match = ip_diag.criteria_match;
                ipd.in_ipset = ip_diag.in_ipset;
                if (ip_diag.set_write_evidence.has_value()) {
                    api::SetWriteEvidence evidence;
                    switch (ip_diag.set_write_evidence->status) {
                    case SetWriteEvidenceStatus::Recorded:
                        evidence.status = api::RoutingTestSetWriteEvidenceStatus::RECORDED;
                        break;
                    case SetWriteEvidenceStatus::NoRecord:
                        evidence.status = api::RoutingTestSetWriteEvidenceStatus::NO_RECORD;
                        break;
                    case SetWriteEvidenceStatus::NotTracked:
                        evidence.status = api::RoutingTestSetWriteEvidenceStatus::NOT_TRACKED;
                        break;
                    }
                    evidence.age_seconds = ip_diag.set_write_evidence->age_seconds
                        ? std::optional<int64_t>(static_cast<int64_t>(
                              *ip_diag.set_write_evidence->age_seconds))
                        : std::nullopt;
                    ipd.set_write_evidence = std::move(evidence);
                }
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
