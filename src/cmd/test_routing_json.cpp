#include "test_routing_json.hpp"

#include "../api/generated/api_types.hpp"
#include "../util/format_compat.hpp"

#include <arpa/inet.h>

#include <cstdint>
#include <utility>

namespace keen_pbr3 {

namespace {

[[noreturn]] void invalid_field(const std::string& message) {
    throw TestRoutingRequestError(message);
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

constexpr std::pair<const char*, api::AStatus> kDnsStatusNames[] = {
    {"ok", api::AStatus::OK}, {"nodata", api::AStatus::NODATA},
    {"nxdomain", api::AStatus::NXDOMAIN}, {"servfail", api::AStatus::SERVFAIL},
    {"refused", api::AStatus::REFUSED}, {"rcode", api::AStatus::RCODE},
    {"timeout", api::AStatus::TIMEOUT}, {"network_error", api::AStatus::NETWORK_ERROR},
    {"malformed", api::AStatus::MALFORMED}};

std::optional<api::AStatus> dns_status_to_api(const std::optional<std::string>& name) {
    if (!name) return std::nullopt;
    for (const auto& [text, value] : kDnsStatusNames) {
        if (*name == text) return value;
    }
    return std::nullopt;
}

std::optional<std::string> dns_status_from_api(const std::optional<api::AStatus>& status) {
    if (!status) return std::nullopt;
    for (const auto& [text, value] : kDnsStatusNames) {
        if (*status == value) return std::string(text);
    }
    return std::nullopt;
}

api::ListMatch list_match_to_api(const ListMatchInfo& match) {
    api::ListMatch out;
    out.list = match.list_name;
    out.via = match.via;
    return out;
}

ListMatchInfo list_match_from_api(const api::ListMatch& match) {
    return ListMatchInfo{match.list, match.via};
}

} // namespace

TestRoutingRequest parse_test_routing_request(const nlohmann::json& body) {
    if (!body.is_object()) {
        invalid_field("Invalid request body");
    }

    validate_integer_field(body, "dest_port", 1, 65535);
    validate_integer_field(body, "src_port", 1, 65535);
    validate_integer_field(body, "dscp", 0, 63);

    api::RoutingTestRequest req;
    try {
        api::from_json(body, req);
    } catch (const std::exception&) {
        invalid_field("Invalid request body");
    }

    if (req.target.empty()) {
        invalid_field("Field 'target' must not be empty");
    }
    if (req.src_addr.has_value() && !is_ip_address(*req.src_addr)) {
        invalid_field("Field 'src_addr' must be a valid IPv4 or IPv6 address");
    }
    if (req.src_addr.has_value() && is_ip_address(req.target) &&
        is_ipv4_address(*req.src_addr) != is_ipv4_address(req.target)) {
        invalid_field("Field 'src_addr' must use the same address family as 'target'");
    }

    TestRoutingRequest request;
    request.target = req.target;
    auto& criteria = request.criteria;
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
    return request;
}

nlohmann::json test_routing_result_to_json(const TestRoutingResult& result) {
    api::RoutingTestResponse resp;
    resp.target       = result.target;
    resp.is_domain    = result.is_domain;
    resp.dns_error    = result.dns_error;
    resp.no_matching_rule = result.no_matching_rule;
    resp.resolved_ips = result.resolved_ips;
    resp.warnings     = result.warnings;
    if (result.dns) {
        api::RoutingTestDns d;
        if (result.dns->resolver == "dnsmasq") d.resolver = api::Resolver::DNSMASQ;
        else if (result.dns->resolver == "test") d.resolver = api::Resolver::TEST;
        else d.resolver = api::Resolver::SYSTEM;
        d.endpoint = result.dns->endpoint;
        d.instance = result.dns->instance;
        d.a_status = dns_status_to_api(result.dns->a_status);
        d.aaaa_status = dns_status_to_api(result.dns->aaaa_status);
        d.cname_chain = result.dns->cname_chain;
        resp.dns = std::move(d);
    }

    for (const auto& entry : result.entries) {
        api::RoutingTestEntry e;
        e.ip                = entry.ip;
        e.expected_outbound = entry.expected_outbound;
        e.actual_outbound   = entry.actual_outbound;
        e.matched_rule_index = entry.matched_rule_index;
        e.criteria_match = entry.criteria_match;
        e.ok                = entry.ok;
        if (entry.list_match) {
            e.list_match = list_match_to_api(*entry.list_match);
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
            rd.target_match = list_match_to_api(*rule_diag.target_match);
        }
        for (const auto& ip_diag : rule_diag.ip_rows) {
            api::RoutingTestRuleIpDiagnosticElement ipd;
            ipd.ip = ip_diag.ip;
            ipd.in_lists = ip_diag.in_lists;
            if (ip_diag.list_match) {
                ipd.list_match = list_match_to_api(*ip_diag.list_match);
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
    return out;
}

TestRoutingResult test_routing_result_from_json(const nlohmann::json& value) {
    api::RoutingTestResponse resp;
    api::from_json(value, resp);

    TestRoutingResult result;
    result.target = resp.target;
    result.is_domain = resp.is_domain;
    result.resolved_ips = resp.resolved_ips;
    result.no_matching_rule = resp.no_matching_rule;
    result.dns_error = resp.dns_error;
    result.warnings = resp.warnings;
    if (resp.dns) {
        TestRoutingDnsInfo info;
        switch (resp.dns->resolver) {
        case api::Resolver::DNSMASQ: info.resolver = "dnsmasq"; break;
        case api::Resolver::TEST: info.resolver = "test"; break;
        case api::Resolver::SYSTEM: info.resolver = "system"; break;
        }
        info.endpoint = resp.dns->endpoint;
        info.instance = resp.dns->instance;
        info.a_status = dns_status_from_api(resp.dns->a_status);
        info.aaaa_status = dns_status_from_api(resp.dns->aaaa_status);
        info.cname_chain = resp.dns->cname_chain.value_or(std::vector<std::string>{});
        result.dns = std::move(info);
    }
    for (const auto& item : resp.results) {
        TestRoutingEntry entry;
        entry.ip = item.ip;
        entry.expected_outbound = item.expected_outbound;
        entry.actual_outbound = item.actual_outbound;
        entry.ok = item.ok;
        entry.matched_rule_index = item.matched_rule_index
            ? std::optional<int>(static_cast<int>(*item.matched_rule_index))
            : std::nullopt;
        entry.criteria_match = item.criteria_match;
        if (item.list_match) {
            entry.list_match = list_match_from_api(*item.list_match);
        }
        result.entries.push_back(std::move(entry));
    }
    // Rule diagnostics are not rendered by the CLI; they stay in the API body.
    return result;
}

} // namespace keen_pbr3
