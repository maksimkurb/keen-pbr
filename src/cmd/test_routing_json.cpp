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

constexpr std::pair<const char*, api::RoutingTestDnsAddressStatus> kDnsStatusNames[] = {
    {"ok", api::RoutingTestDnsAddressStatus::OK}, {"nodata", api::RoutingTestDnsAddressStatus::NODATA},
    {"nxdomain", api::RoutingTestDnsAddressStatus::NXDOMAIN}, {"servfail", api::RoutingTestDnsAddressStatus::SERVFAIL},
    {"refused", api::RoutingTestDnsAddressStatus::REFUSED}, {"rcode", api::RoutingTestDnsAddressStatus::RCODE},
    {"timeout", api::RoutingTestDnsAddressStatus::TIMEOUT}, {"network_error", api::RoutingTestDnsAddressStatus::NETWORK_ERROR},
    {"malformed", api::RoutingTestDnsAddressStatus::MALFORMED}};

std::optional<api::RoutingTestDnsAddressStatus> dns_status_to_api(const std::optional<std::string>& name) {
    if (!name) return std::nullopt;
    for (const auto& [text, value] : kDnsStatusNames) {
        if (*name == text) return value;
    }
    return std::nullopt;
}

std::optional<std::string> dns_status_from_api(const std::optional<api::RoutingTestDnsAddressStatus>& status) {
    if (!status) return std::nullopt;
    for (const auto& [text, value] : kDnsStatusNames) {
        if (*status == value) return std::string(text);
    }
    return std::nullopt;
}

api::RoutingTestDiagnosis diagnosis_to_api(RoutingDiagnosis in) {
    switch (in) {
    case RoutingDiagnosis::Dns: return api::RoutingTestDiagnosis::DNS;
    case RoutingDiagnosis::Criteria: return api::RoutingTestDiagnosis::CRITERIA;
    case RoutingDiagnosis::NetworkState: return api::RoutingTestDiagnosis::NETWORK_STATE;
    case RoutingDiagnosis::MissingIpset: return api::RoutingTestDiagnosis::MISSING_IPSET;
    case RoutingDiagnosis::Firewall: return api::RoutingTestDiagnosis::FIREWALL;
    case RoutingDiagnosis::Ok: return api::RoutingTestDiagnosis::OK;
    case RoutingDiagnosis::OtherIpset: return api::RoutingTestDiagnosis::OTHER_IPSET;
    case RoutingDiagnosis::ConflictingIpsets: return api::RoutingTestDiagnosis::CONFLICTING_IPSETS;
    case RoutingDiagnosis::StaleIpset: return api::RoutingTestDiagnosis::STALE_IPSET;
    case RoutingDiagnosis::Mismatch: return api::RoutingTestDiagnosis::MISMATCH;
    }
    return api::RoutingTestDiagnosis::MISMATCH;
}

RoutingDiagnosis diagnosis_from_api(api::RoutingTestDiagnosis in) {
    switch (in) {
    case api::RoutingTestDiagnosis::DNS: return RoutingDiagnosis::Dns;
    case api::RoutingTestDiagnosis::CRITERIA: return RoutingDiagnosis::Criteria;
    case api::RoutingTestDiagnosis::NETWORK_STATE: return RoutingDiagnosis::NetworkState;
    case api::RoutingTestDiagnosis::MISSING_IPSET: return RoutingDiagnosis::MissingIpset;
    case api::RoutingTestDiagnosis::FIREWALL: return RoutingDiagnosis::Firewall;
    case api::RoutingTestDiagnosis::OK: return RoutingDiagnosis::Ok;
    case api::RoutingTestDiagnosis::OTHER_IPSET: return RoutingDiagnosis::OtherIpset;
    case api::RoutingTestDiagnosis::CONFLICTING_IPSETS: return RoutingDiagnosis::ConflictingIpsets;
    case api::RoutingTestDiagnosis::STALE_IPSET: return RoutingDiagnosis::StaleIpset;
    case api::RoutingTestDiagnosis::MISMATCH: return RoutingDiagnosis::Mismatch;
    }
    return RoutingDiagnosis::Mismatch;
}

api::RoutingTestListMatch list_match_to_api(const ListMatchInfo& match) {
    api::RoutingTestListMatch out;
    out.list = match.list_name;
    out.via = match.via;
    return out;
}

ListMatchInfo list_match_from_api(const api::RoutingTestListMatch& match) {
    return ListMatchInfo{match.list, match.via};
}

api::RoutingTestDefaultGateway default_gateway_to_api(const DefaultGatewayDecision& in) {
    api::RoutingTestDefaultGateway out;
    out.rule_index = in.rule_index;
    out.uses_default_gateway = in.uses_default_gateway;
    out.applied_uses_default_gateway = in.applied_uses_default_gateway;
    if (in.bypass) {
        api::RoutingTestDefaultGatewayBypass bypass;
        bypass.cidr = in.bypass->cidr;
        bypass.interface = in.bypass->interface;
        switch (in.bypass->source) {
        case DefaultGatewayBypassSource::Reserved:
            bypass.source = api::RoutingTestDefaultGatewayBypassSource::RESERVED; break;
        case DefaultGatewayBypassSource::Route:
            bypass.source = api::RoutingTestDefaultGatewayBypassSource::ROUTE; break;
        case DefaultGatewayBypassSource::Address:
            bypass.source = api::RoutingTestDefaultGatewayBypassSource::ADDRESS; break;
        }
        out.bypass = std::move(bypass);
    }
    return out;
}

DefaultGatewayDecision default_gateway_from_api(const api::RoutingTestDefaultGateway& in) {
    DefaultGatewayDecision out;
    out.rule_index = static_cast<int>(in.rule_index);
    out.uses_default_gateway = in.uses_default_gateway;
    out.applied_uses_default_gateway = in.applied_uses_default_gateway;
    if (in.bypass) {
        DefaultGatewayBypassEntry bypass;
        bypass.cidr = in.bypass->cidr;
        bypass.interface = in.bypass->interface;
        switch (in.bypass->source) {
        case api::RoutingTestDefaultGatewayBypassSource::RESERVED:
            bypass.source = DefaultGatewayBypassSource::Reserved; break;
        case api::RoutingTestDefaultGatewayBypassSource::ROUTE:
            bypass.source = DefaultGatewayBypassSource::Route; break;
        case api::RoutingTestDefaultGatewayBypassSource::ADDRESS:
            bypass.source = DefaultGatewayBypassSource::Address; break;
        }
        out.bypass = std::move(bypass);
    }
    return out;
}

api::RoutingTestOutboundGroup outbound_group_to_api(const OutboundGroupBreakdown& in) {
    api::RoutingTestOutboundGroup out;
    out.tag = in.tag;
    out.strategy = in.strategy == "balance" ? api::TestGroupStrategy::BALANCE : api::TestGroupStrategy::PRIORITY;
    for (const auto& member : in.members) {
        api::RoutingTestOutboundGroupMember m;
        m.tag = member.tag;
        m.interface = member.interface_name;
        if (member.weight) m.weight = static_cast<int64_t>(*member.weight);
        if (member.share_percent) m.share_percent = static_cast<int64_t>(*member.share_percent);
        m.in_use = member.in_use;
        out.members.push_back(std::move(m));
    }
    return out;
}

OutboundGroupBreakdown outbound_group_from_api(const api::RoutingTestOutboundGroup& in) {
    OutboundGroupBreakdown out;
    out.tag = in.tag;
    out.strategy = in.strategy == api::TestGroupStrategy::BALANCE ? "balance" : "priority";
    for (const auto& m : in.members) {
        OutboundGroupMember member;
        member.tag = m.tag;
        member.interface_name = m.interface;
        if (m.weight) member.weight = static_cast<uint32_t>(*m.weight);
        if (m.share_percent) member.share_percent = static_cast<uint32_t>(*m.share_percent);
        member.in_use = m.in_use;
        out.members.push_back(std::move(member));
    }
    return out;
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
        case api::RoutingTestRequestProto::TCP: criteria.proto = "tcp"; break;
        case api::RoutingTestRequestProto::UDP: criteria.proto = "udp"; break;
        case api::RoutingTestRequestProto::OTHER: criteria.proto = "other"; break;
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
    resp.ipv6_enabled = result.ipv6_enabled;
    resp.resolved_ips = result.resolved_ips;
    resp.warnings     = result.warnings;
    if (result.dns) {
        api::RoutingTestDns d;
        if (result.dns->resolver == "dnsmasq") d.resolver = api::RoutingTestDnsResolver::DNSMASQ;
        else if (result.dns->resolver == "test") d.resolver = api::RoutingTestDnsResolver::TEST;
        else d.resolver = api::RoutingTestDnsResolver::SYSTEM;
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
        e.diagnosis = diagnosis_to_api(entry.diagnosis);
        if (entry.undetermined_rule_index) {
            e.undetermined_rule_index = *entry.undetermined_rule_index;
            if (entry.default_gateway_directness) {
                e.undetermined_reason = api::RoutingTestEntryUndeterminedReason::DEFAULT_GATEWAY_DIRECTNESS;
            } else if (!entry.missing_criteria.empty()) {
                e.undetermined_reason = api::RoutingTestEntryUndeterminedReason::MISSING_CRITERIA;
            }
            std::vector<api::RoutingTestEntryMissingCriteriaItem> missing;
            for (const auto& field : entry.missing_criteria) {
                if (field == "src_addr") missing.push_back(api::RoutingTestEntryMissingCriteriaItem::SRC_ADDR);
                else if (field == "src_port") missing.push_back(api::RoutingTestEntryMissingCriteriaItem::SRC_PORT);
                else if (field == "proto") missing.push_back(api::RoutingTestEntryMissingCriteriaItem::PROTO);
                else if (field == "dest_port") missing.push_back(api::RoutingTestEntryMissingCriteriaItem::DEST_PORT);
            }
            e.missing_criteria = std::move(missing);
        }
        e.ok                = entry.ok;
        if (entry.default_gateway) {
            e.default_gateway = default_gateway_to_api(*entry.default_gateway);
        }
        if (entry.outbound_group) {
            e.outbound_group = outbound_group_to_api(*entry.outbound_group);
        }
        if (entry.list_match) {
            e.list_match = list_match_to_api(*entry.list_match);
        }
        resp.results.push_back(std::move(e));
    }

    for (const auto& rule_diag : result.rule_diagnostics) {
        api::RoutingTestRuleDiagnostic rd;
        rd.rule_index = rule_diag.rule_index;
        rd.rule = rule_diag.rule;
        rd.outbound = rule_diag.outbound;
        rd.interface_name = rule_diag.interface_name;
        rd.target_in_lists = rule_diag.target_in_lists;
        if (rule_diag.target_match) {
            rd.target_match = list_match_to_api(*rule_diag.target_match);
        }
        for (const auto& ip_diag : rule_diag.ip_rows) {
            api::RoutingTestRuleIpDiagnostic ipd;
            ipd.ip = ip_diag.ip;
            ipd.in_lists = ip_diag.in_lists;
            if (ip_diag.list_match) {
                ipd.list_match = list_match_to_api(*ip_diag.list_match);
            }
            ipd.criteria_match = ip_diag.criteria_match;
            ipd.in_ipset = ip_diag.in_ipset;
            if (ip_diag.set_write_evidence.has_value()) {
                api::RoutingTestSetWriteEvidence evidence;
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
    result.ipv6_enabled = resp.ipv6_enabled;
    result.dns_error = resp.dns_error;
    result.warnings = resp.warnings;
    if (resp.dns) {
        TestRoutingDnsInfo info;
        switch (resp.dns->resolver) {
        case api::RoutingTestDnsResolver::DNSMASQ: info.resolver = "dnsmasq"; break;
        case api::RoutingTestDnsResolver::TEST: info.resolver = "test"; break;
        case api::RoutingTestDnsResolver::SYSTEM: info.resolver = "system"; break;
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
        entry.diagnosis = diagnosis_from_api(item.diagnosis);
        if (item.undetermined_rule_index) {
            entry.undetermined_rule_index = static_cast<int>(*item.undetermined_rule_index);
        }
        entry.default_gateway_directness =
            item.undetermined_reason == api::RoutingTestEntryUndeterminedReason::DEFAULT_GATEWAY_DIRECTNESS;
        for (const auto field : item.missing_criteria.value_or(std::vector<api::RoutingTestEntryMissingCriteriaItem>{})) {
            switch (field) {
            case api::RoutingTestEntryMissingCriteriaItem::SRC_ADDR: entry.missing_criteria.emplace_back("src_addr"); break;
            case api::RoutingTestEntryMissingCriteriaItem::SRC_PORT: entry.missing_criteria.emplace_back("src_port"); break;
            case api::RoutingTestEntryMissingCriteriaItem::PROTO: entry.missing_criteria.emplace_back("proto"); break;
            case api::RoutingTestEntryMissingCriteriaItem::DEST_PORT: entry.missing_criteria.emplace_back("dest_port"); break;
            }
        }
        if (item.default_gateway) {
            entry.default_gateway = default_gateway_from_api(*item.default_gateway);
        }
        if (item.outbound_group) {
            entry.outbound_group = outbound_group_from_api(*item.outbound_group);
        }
        if (item.list_match) {
            entry.list_match = list_match_from_api(*item.list_match);
        }
        result.entries.push_back(std::move(entry));
    }
    for (const auto& item : resp.rule_diagnostics) {
        RuleDiagnostic rule_diag;
        rule_diag.rule_index = static_cast<int>(item.rule_index);
        rule_diag.rule = item.rule;
        rule_diag.outbound = item.outbound;
        rule_diag.interface_name = item.interface_name;
        rule_diag.target_in_lists = item.target_in_lists;
        if (item.target_match) {
            rule_diag.target_match = list_match_from_api(*item.target_match);
        }
        for (const auto& row : item.ip_rows) {
            RuleIpDiagnostic ip_diag;
            ip_diag.ip = row.ip;
            ip_diag.in_lists = row.in_lists;
            if (row.list_match) {
                ip_diag.list_match = list_match_from_api(*row.list_match);
            }
            ip_diag.in_ipset = row.in_ipset;
            ip_diag.criteria_match = row.criteria_match;
            if (row.set_write_evidence) {
                SetWriteEvidence evidence;
                switch (row.set_write_evidence->status) {
                case api::RoutingTestSetWriteEvidenceStatus::RECORDED:
                    evidence.status = SetWriteEvidenceStatus::Recorded;
                    break;
                case api::RoutingTestSetWriteEvidenceStatus::NO_RECORD:
                    evidence.status = SetWriteEvidenceStatus::NoRecord;
                    break;
                case api::RoutingTestSetWriteEvidenceStatus::NOT_TRACKED:
                    evidence.status = SetWriteEvidenceStatus::NotTracked;
                    break;
                }
                if (row.set_write_evidence->age_seconds) {
                    evidence.age_seconds = static_cast<uint64_t>(*row.set_write_evidence->age_seconds);
                }
                ip_diag.set_write_evidence = evidence;
            }
            rule_diag.ip_rows.push_back(std::move(ip_diag));
        }
        result.rule_diagnostics.push_back(std::move(rule_diag));
    }
    return result;
}

} // namespace keen_pbr3
