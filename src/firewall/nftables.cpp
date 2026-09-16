#include "nftables.hpp"
#include "nft_batch_pipe.hpp"
#include "firewall_rule.hpp"
#include "port_spec_util.hpp"
#include "../log/logger.hpp"
#include "../util/format_compat.hpp"
#include "../util/safe_exec.hpp"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <sys/socket.h>

namespace keen_pbr3 {

namespace {

bool is_ipv6_addr(const std::string& addr) {
    return addr.find(':') != std::string::npos;
}

bool cleanup_command_reports_absence(const ExecCaptureResult& result) {
    if (result.exit_code == 0 && !result.truncated && !result.timed_out) {
        return true;
    }
    if (result.truncated || result.timed_out) {
        return false;
    }
    std::string output = result.stdout_output;
    std::transform(output.begin(), output.end(), output.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return output.find("no such") != std::string::npos ||
           output.find("does not exist") != std::string::npos ||
           output.find("cannot be found") != std::string::npos;
}

ExecCaptureResult run_cleanup_command(const std::vector<std::string>& args) {
    const auto result = safe_exec_capture(args, /*suppress_stderr=*/false,
                                          /*max_bytes=*/0,
                                          /*merge_stderr=*/true);
    if (!cleanup_command_reports_absence(result)) {
        throw FirewallError(keen_pbr3::format(
            "firewall cleanup command failed: {} (status {})",
            safe_exec_command_string(args), result.exit_code));
    }
    return result;
}

std::vector<std::string> filter_addrs_by_family(const std::vector<std::string>& addrs,
                                                 bool ipv6) {
    std::vector<std::string> filtered;
    for (const auto& addr : addrs) {
        if (is_ipv6_addr(addr) == ipv6) {
            filtered.push_back(addr);
        }
    }
    return filtered;
}

std::vector<L4Proto> expand_l4_protos(L4Proto proto) {
    if (proto == L4Proto::TcpUdp) {
        return {L4Proto::Tcp, L4Proto::Udp};
    }
    return {proto};
}

bool needs_family_specific_rule(const FirewallRuleCriteria& criteria) {
    return criteria.dst_set_name.has_value()
        || criteria.dscp.has_value()
        || !criteria.src_addr.empty()
        || !criteria.dst_addr.empty()
        || !criteria.src_port.empty()
        || !criteria.dst_port.empty()
        || criteria.default_gateway != DefaultGatewayFamily::None;
}

nlohmann::json family_match_expr(int family) {
    return {{"match", {{"op", "=="},
                        {"left", {{"meta", {{"key", "nfproto"}}}}},
                        {"right", family == AF_INET6 ? "ipv6" : "ipv4"}}}};
}

void add_rule_comment(nlohmann::json& command, const FirewallRuleKey& key) {
    if (key.module_id.empty() && key.instance_id.empty()) {
        return;
    }
    command["add"]["rule"]["comment"] = key.comment();
}

} // namespace

NftablesFirewall::NftablesFirewall() = default;

NftablesFirewall::~NftablesFirewall() {
    try {
        cleanup_impl();
    } catch (const std::exception& e) {
        Logger::instance().error("NftablesFirewall cleanup failed during destruction: {}",
                                 e.what());
    } catch (...) {
        Logger::instance().error(
            "NftablesFirewall cleanup failed during destruction: unknown error");
    }
}

void NftablesFirewall::prepare_apply(FirewallApplyMode mode) {
    pending_sets_.clear();
    pending_elements_.clear();
    pending_rules_.clear();
    prepared_mode_ = mode;
}

void NftablesFirewall::create_ipset(const std::string& set_name, int family,
                                     uint32_t timeout) {
    if (family == AF_INET6 && !ipv6_enabled()) {
        return;
    }

    PendingSet ps;
    ps.name = set_name;
    ps.type = (family == AF_INET6) ? "ipv6_addr" : "ipv4_addr";
    ps.timeout = timeout;
    pending_sets_.push_back(std::move(ps));
    created_sets_[set_name] = family;
}

void NftablesFirewall::append_rules_for_family(int family,
                                               PendingRule::Action action,
                                               uint32_t fwmark,
                                               const FirewallRuleCriteria& criteria,
                                               const FirewallRuleKey& key) {
    if (family == AF_INET6 && !ipv6_enabled()) {
        return;
    }
    if ((criteria.default_gateway == DefaultGatewayFamily::Ipv4 && family != AF_INET) ||
        (criteria.default_gateway == DefaultGatewayFamily::Ipv6 && family != AF_INET6)) {
        return;
    }

    const bool ipv6 = family == AF_INET6;
    const auto filtered_src_addrs = criteria.src_addr.empty()
        ? std::vector<std::string>{}
        : filter_addrs_by_family(criteria.src_addr, ipv6);
    const auto filtered_dst_addrs = criteria.dst_addr.empty()
        ? std::vector<std::string>{}
        : filter_addrs_by_family(criteria.dst_addr, ipv6);
    if ((!criteria.src_addr.empty() && filtered_src_addrs.empty())
        || (!criteria.dst_addr.empty() && filtered_dst_addrs.empty())) {
        return;
    }

    for (const auto proto : expand_l4_protos(criteria.proto)) {
        PendingRule pr;
        pr.family = family;
        pr.action = action;
        pr.fwmark = fwmark;
        pr.fwmark_mask = fwmark_mask();
        pr.key = key;
        pr.criteria = criteria;
        pr.criteria.proto = proto;
        if (!criteria.src_addr.empty()) {
            pr.criteria.src_addr = filtered_src_addrs;
        }
        if (!criteria.dst_addr.empty()) {
            pr.criteria.dst_addr = filtered_dst_addrs;
        }
        pending_rules_.push_back(std::move(pr));
        if (criteria.apply_output &&
            criteria.default_gateway != DefaultGatewayFamily::None) {
            PendingRule prerouting_rule = pending_rules_.back();
            prerouting_rule.criteria.apply_output = false;
            pending_rules_.push_back(std::move(prerouting_rule));
        }
    }
}

void NftablesFirewall::append_balance_rules_for_family(
    int family,
    uint32_t fallback_fwmark,
    const std::vector<FirewallBalanceCandidate>& candidates,
    const FirewallRuleCriteria& criteria,
    const FirewallRuleKey& key) {
    if (family == AF_INET6 && !ipv6_enabled()) {
        return;
    }
    if ((criteria.default_gateway == DefaultGatewayFamily::Ipv4 && family != AF_INET) ||
        (criteria.default_gateway == DefaultGatewayFamily::Ipv6 && family != AF_INET6)) {
        return;
    }

    std::vector<uint32_t> marks;
    for (const auto& candidate : candidates) {
        if ((family == AF_INET && candidate.ipv4) ||
            (family == AF_INET6 && candidate.ipv6)) {
            marks.push_back(candidate.fwmark);
        }
    }
    if (marks.empty()) {
        const auto before = pending_rules_.size();
        append_rules_for_family(family, PendingRule::Mark, fallback_fwmark,
                                criteria, key);
        for (std::size_t index = before; index < pending_rules_.size(); ++index) {
            pending_rules_[index].family_guard = true;
        }
        return;
    }
    if (marks.size() == 1) {
        const auto before = pending_rules_.size();
        append_rules_for_family(family, PendingRule::Mark, marks.front(), criteria,
                                key);
        for (std::size_t index = before; index < pending_rules_.size(); ++index) {
            pending_rules_[index].family_guard = true;
        }
        return;
    }

    PendingRule pr;
    pr.family = family;
    pr.action = PendingRule::Balance;
    pr.fwmark = fallback_fwmark;
    pr.fwmark_mask = fwmark_mask();
    pr.family_guard = true;
    pr.balance_marks = std::move(marks);
    pr.key = key;
    pr.criteria = criteria;
    pending_rules_.push_back(std::move(pr));
    if (criteria.apply_output &&
        criteria.default_gateway != DefaultGatewayFamily::None) {
        PendingRule prerouting_rule = pending_rules_.back();
        prerouting_rule.criteria.apply_output = false;
        pending_rules_.push_back(std::move(prerouting_rule));
    }
}

void NftablesFirewall::create_mark_rule(uint32_t fwmark,
                                        const FirewallRuleCriteria& criteria) {
    create_mark_rule(FirewallRuleKey{}, fwmark, criteria);
}

void NftablesFirewall::create_mark_rule(const FirewallRuleKey& key,
                                        uint32_t fwmark,
                                        const FirewallRuleCriteria& criteria) {
    if (criteria.dst_set_name.has_value()) {
        auto it = created_sets_.find(*criteria.dst_set_name);
        int family = (it != created_sets_.end()) ? it->second : AF_INET;
        append_rules_for_family(family, PendingRule::Mark, fwmark, criteria, key);
        return;
    }
    if (!needs_family_specific_rule(criteria)) {
        append_rules_for_family(AF_INET, PendingRule::Mark, fwmark, criteria, key);
        return;
    }
    append_rules_for_family(AF_INET, PendingRule::Mark, fwmark, criteria, key);
    append_rules_for_family(AF_INET6, PendingRule::Mark, fwmark, criteria, key);
}

void NftablesFirewall::create_balance_rule(
    uint32_t fallback_fwmark,
    const std::vector<FirewallBalanceCandidate>& candidates,
    const FirewallRuleCriteria& criteria) {
    create_balance_rule(FirewallRuleKey{}, fallback_fwmark, candidates, criteria);
}

void NftablesFirewall::create_balance_rule(
    const FirewallRuleKey& key, uint32_t fallback_fwmark,
    const std::vector<FirewallBalanceCandidate>& candidates,
    const FirewallRuleCriteria& criteria) {
    if (criteria.dst_set_name.has_value()) {
        const auto it = created_sets_.find(*criteria.dst_set_name);
        const int family = it != created_sets_.end() ? it->second : AF_INET;
        append_balance_rules_for_family(family, fallback_fwmark, candidates, criteria,
                                        key);
        return;
    }
    if (!needs_family_specific_rule(criteria)) {
        append_balance_rules_for_family(AF_INET, fallback_fwmark, candidates, criteria,
                                        key);
        append_balance_rules_for_family(AF_INET6, fallback_fwmark, candidates, criteria,
                                        key);
        return;
    }
    append_balance_rules_for_family(AF_INET, fallback_fwmark, candidates, criteria, key);
    append_balance_rules_for_family(AF_INET6, fallback_fwmark, candidates, criteria, key);
}

void NftablesFirewall::set_owned_marks(const std::vector<uint32_t>& marks) {
    owned_marks_.clear();
    for (const uint32_t mark : marks) {
        if (mark != 0) owned_marks_.insert(mark);
    }
}

void NftablesFirewall::create_drop_rule(const FirewallRuleCriteria& criteria) {
    create_drop_rule(FirewallRuleKey{}, criteria);
}

void NftablesFirewall::create_drop_rule(const FirewallRuleKey& key,
                                        const FirewallRuleCriteria& criteria) {
    if (criteria.dst_set_name.has_value()) {
        auto it = created_sets_.find(*criteria.dst_set_name);
        int family = (it != created_sets_.end()) ? it->second : AF_INET;
        append_rules_for_family(family, PendingRule::Drop, 0, criteria, key);
        return;
    }
    if (!needs_family_specific_rule(criteria)) {
        append_rules_for_family(AF_INET, PendingRule::Drop, 0, criteria, key);
        return;
    }
    append_rules_for_family(AF_INET, PendingRule::Drop, 0, criteria, key);
    append_rules_for_family(AF_INET6, PendingRule::Drop, 0, criteria, key);
}

void NftablesFirewall::create_pass_rule(const FirewallRuleCriteria& criteria) {
    create_pass_rule(FirewallRuleKey{}, criteria);
}

void NftablesFirewall::create_pass_rule(const FirewallRuleKey& key,
                                        const FirewallRuleCriteria& criteria) {
    if (criteria.dst_set_name.has_value()) {
        auto it = created_sets_.find(*criteria.dst_set_name);
        int family = (it != created_sets_.end()) ? it->second : AF_INET;
        append_rules_for_family(family, PendingRule::Pass, 0, criteria, key);
        return;
    }
    if (!needs_family_specific_rule(criteria)) {
        append_rules_for_family(AF_INET, PendingRule::Pass, 0, criteria, key);
        return;
    }
    append_rules_for_family(AF_INET, PendingRule::Pass, 0, criteria, key);
    append_rules_for_family(AF_INET6, PendingRule::Pass, 0, criteria, key);
}

std::unique_ptr<ListEntryVisitor> NftablesFirewall::create_batch_loader(
    const std::string& set_name) {
    if (prepared_mode_ == FirewallApplyMode::RulesOnly) {
        throw FirewallRulesOnlyError(
            "RulesOnly cannot stream or modify set " + set_name);
    }
    // Ensure an entry exists in pending_elements_ for this set (as an empty array)
    auto& buf = pending_elements_[set_name];
    if (!buf.is_array()) {
        buf = nlohmann::json::array();
    }
    return std::make_unique<NftBatchVisitor>(buf, set_name);
}

// --- Port spec helpers ---

// Parse a port spec into an nftables JSON right-hand side value.
// "443"       → 443  (integer)
// "8000-9000" → {"range": [8000, 9000]}
// "80,443"    → {"set": [80, 443]}
static nlohmann::json port_spec_to_nft_rhs(const PortSpec& spec) {
    PortSpecKind kind = classify_port_spec(spec);
    if (kind == PortSpecKind::List) {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& range : spec.ranges) {
            if (range.from != range.to) {
                arr.push_back({{"range", nlohmann::json::array({range.from, range.to})}});
            } else {
                arr.push_back(range.from);
            }
        }
        return {{"set", arr}};
    }

    if (kind == PortSpecKind::Range) {
        return {{"range", nlohmann::json::array({spec.ranges[0].from, spec.ranges[0].to})}};
    }

    return spec.ranges[0].from;
}

// --- Private static helpers ---

nlohmann::json NftablesFirewall::build_table_json() {
    return {{"add", {{"table", {{"family", "inet"}, {"name", TABLE_NAME}}}}}};
}

nlohmann::json NftablesFirewall::build_set_json(const PendingSet& ps) {
    nlohmann::json flags = nlohmann::json::array({"interval"});
    if (ps.timeout > 0) {
        flags.push_back("timeout");
    }
    nlohmann::json set = {
        {"family", "inet"},
        {"table", TABLE_NAME},
        {"name", ps.name},
        {"type", ps.type},
        {"flags", flags},
        {"auto-merge", true}
    };
    if (ps.timeout > 0) {
        set["timeout"] = ps.timeout;
    }
    return {{"add", {{"set", set}}}};
}

nlohmann::json NftablesFirewall::build_chain_json() {
    return {{"add", {{"chain", {
        {"family", "inet"},
        {"table", TABLE_NAME},
        {"name", CHAIN_NAME},
        {"type", "filter"},
        {"hook", "prerouting"},
        {"prio", -150},
        {"policy", "accept"}
    }}}}};
}

nlohmann::json NftablesFirewall::build_output_chain_json() {
    return {{"add", {{"chain", {
        {"family", "inet"}, {"table", TABLE_NAME}, {"name", OUTPUT_CHAIN_NAME},
        {"type", "route"}, {"hook", "output"}, {"prio", -150}, {"policy", "accept"}
    }}}}};
}

nlohmann::json NftablesFirewall::build_delete_chain_json() {
    return {{"delete", {{"chain", {
        {"family", "inet"},
        {"table", TABLE_NAME},
        {"name", CHAIN_NAME}
    }}}}};
}

nlohmann::json NftablesFirewall::build_delete_output_chain_json() {
    return {{"delete", {{"chain", {
        {"family", "inet"}, {"table", TABLE_NAME}, {"name", OUTPUT_CHAIN_NAME}
    }}}}};
}

std::string NftablesFirewall::setter_chain_name(uint32_t fwmark) {
    std::ostringstream name;
    name << "setmark_" << std::hex << std::setw(8) << std::setfill('0') << fwmark;
    return name.str();
}

nlohmann::json NftablesFirewall::build_setter_chain_json(uint32_t fwmark) {
    return {{"add", {{"chain", {
        {"family", "inet"}, {"table", TABLE_NAME}, {"name", setter_chain_name(fwmark)}
    }}}}};
}

nlohmann::json NftablesFirewall::build_delete_setter_chain_json(uint32_t fwmark) {
    return {{"delete", {{"chain", {
        {"family", "inet"}, {"table", TABLE_NAME}, {"name", setter_chain_name(fwmark)}
    }}}}};
}

nlohmann::json NftablesFirewall::build_setter_rule_json(uint32_t fwmark,
                                                          uint32_t fwmark_mask) {
    const uint32_t preserved = ~fwmark_mask;
    const nlohmann::json set_mark = {{"|", nlohmann::json::array({
        {{"&", nlohmann::json::array({{{"meta", {{"key", "mark"}}}}, preserved})}},
        fwmark
    })}};
    const nlohmann::json set_ctmark = {{"|", nlohmann::json::array({
        {{"&", nlohmann::json::array({{{"ct", {{"key", "mark"}}}}, preserved})}},
        fwmark
    })}};
    nlohmann::json expr = nlohmann::json::array();
    expr.push_back({{"mangle", {
        {"key", {{"meta", {{"key", "mark"}}}}},
        {"value", set_mark}
    }}});
    expr.push_back({{"mangle", {
        {"key", {{"ct", {{"key", "mark"}}}}},
        {"value", set_ctmark}
    }}});
    expr.push_back({{"accept", nullptr}});
    return {{"add", {{"rule", {
        {"family", "inet"}, {"table", TABLE_NAME}, {"chain", setter_chain_name(fwmark)},
        {"expr", expr}
    }}}}};
}

nlohmann::json NftablesFirewall::build_flush_set_json(const std::string& set_name) {
    return {{"flush", {{"set", {{"family", "inet"}, {"table", TABLE_NAME},
                                {"name", set_name}}}}}};
}

nlohmann::json NftablesFirewall::build_delete_set_json(const std::string& set_name) {
    return {{"delete", {{"set", {{"family", "inet"}, {"table", TABLE_NAME},
                                 {"name", set_name}}}}}};
}

bool NftablesFirewall::is_dynamic_set_name(const std::string& set_name) {
    return set_name.rfind("kpbr4d_", 0) == 0 || set_name.rfind("kpbr6d_", 0) == 0;
}

std::string NftablesFirewall::set_schema_key(const PendingSet& set) {
    return set.type + ":" + std::to_string(set.timeout);
}

nlohmann::json NftablesFirewall::build_rule_add_commands(
    const FirewallGlobalPrefilter& prefilter,
    const std::vector<PendingRule>& rules,
    const std::set<uint32_t>& owned_marks) {
    nlohmann::json commands = nlohmann::json::array();

    std::set<uint32_t> setter_marks = owned_marks;
    for (const auto& rule : rules) {
        if (rule.action == PendingRule::Mark && rule.fwmark != 0) {
            setter_marks.insert(rule.fwmark);
        }
        if (rule.action == PendingRule::Balance) {
            setter_marks.insert(rule.balance_marks.begin(), rule.balance_marks.end());
        }
    }

    if (prefilter.restore_conntrack_mark && prefilter.conntrack_mark_mask != 0 &&
        !setter_marks.empty()) {
        const uint32_t mask = prefilter.conntrack_mark_mask;
        nlohmann::json restore_expr = nlohmann::json::array();
        restore_expr.push_back({{"match", {{"op", "=="},
            {"left", {{"ct", {{"key", "direction"}}}}}, {"right", 0}}}});
        restore_expr.push_back({{"match", {{"op", "!="},
            {"left", {{"&", nlohmann::json::array({
                {{"ct", {{"key", "mark"}}}}, mask
            })}}}, {"right", 0}}}});
        nlohmann::json targets = nlohmann::json::array();
        for (const uint32_t mark : setter_marks) {
            targets.push_back(nlohmann::json::array({
                mark, {{"jump", {{"target", setter_chain_name(mark)}}}}
            }));
        }
        nlohmann::json restore_vmap;
        restore_vmap["key"] = {{"&", nlohmann::json::array({
            {{"ct", {{"key", "mark"}}}}, mask
        })}};
        restore_vmap["data"] = {{"set", targets}};
        restore_expr.push_back({{"vmap", restore_vmap}});
        nlohmann::json known_marks = nlohmann::json::array();
        for (const uint32_t mark : setter_marks) {
            known_marks.push_back(mark);
        }
        restore_expr.push_back({{"match", {{"op", "in"},
            {"left", {{"&", nlohmann::json::array({
                {{"ct", {{"key", "mark"}}}}, mask
            })}}}, {"right", {{"set", known_marks}}}}}});
        restore_expr.push_back({{"accept", nullptr}});
        const nlohmann::json restore_rule = {{"family", "inet"},
                                             {"table", TABLE_NAME},
                                             {"chain", CHAIN_NAME},
                                             {"expr", restore_expr}};
        commands.push_back({{"add", {{"rule", restore_rule}}}});
        nlohmann::json output_restore_rule = restore_rule;
        output_restore_rule["chain"] = OUTPUT_CHAIN_NAME;
        commands.push_back({{"add", {{"rule", output_restore_rule}}}});
    }

    if (prefilter.skip_established_or_dnat) {
        nlohmann::json dnat_expr = nlohmann::json::array();
        dnat_expr.push_back({{"match", {
            {"op", "in"},
            {"left", {{"ct", {{"key", "status"}}}}},
            {"right", "dnat"}
        }}});
        dnat_expr.push_back({{"counter", nullptr}});
        dnat_expr.push_back({{"accept", nullptr}});
        commands.push_back({{"add", {{"rule", {
            {"family", "inet"},
            {"table", TABLE_NAME},
            {"chain", CHAIN_NAME},
            {"expr", dnat_expr}
        }}}}});
    }

    if (prefilter.skip_marked_packets) {
        nlohmann::json marked_expr = nlohmann::json::array();
        marked_expr.push_back({{"match", {
            {"op", "!="},
            {"left", {{"meta", {{"key", "mark"}}}}},
            {"right", 0}
        }}});
        marked_expr.push_back({{"counter", nullptr}});
        marked_expr.push_back({{"accept", nullptr}});
        for (const auto* chain : {CHAIN_NAME, OUTPUT_CHAIN_NAME}) {
            commands.push_back({{"add", {{"rule", {
                {"family", "inet"},
                {"table", TABLE_NAME},
                {"chain", chain},
                {"expr", marked_expr}
            }}}}});
        }
    }

    if (prefilter.has_inbound_interfaces()
        && prefilter.inbound_interfaces.has_value()) {
        nlohmann::json iface_rhs;
        if (prefilter.inbound_interfaces->size() == 1) {
            iface_rhs = prefilter.inbound_interfaces->front();
        } else {
            iface_rhs = {{"set", nlohmann::json::array()}};
            for (const auto& iface : *prefilter.inbound_interfaces) {
                iface_rhs["set"].push_back(iface);
            }
        }

        nlohmann::json iface_expr = nlohmann::json::array();
        iface_expr.push_back({{"match", {
            {"op", "!="},
            {"left", {{"meta", {{"key", "iifname"}}}}},
            {"right", iface_rhs}
        }}});
        iface_expr.push_back({{"counter", nullptr}});
        iface_expr.push_back({{"accept", nullptr}});
        commands.push_back({{"add", {{"rule", {
            {"family", "inet"},
            {"table", TABLE_NAME},
            {"chain", CHAIN_NAME},
            {"expr", iface_expr}
        }}}}});
    }

    for (const auto& pr : rules) {
        if (pr.action == PendingRule::Mark) {
            commands.push_back(build_mark_rule_json(pr));
        } else if (pr.action == PendingRule::Balance) {
            commands.push_back(build_balance_rule_json(pr));
        } else if (pr.action == PendingRule::Drop) {
            commands.push_back(build_drop_rule_json(pr));
        } else {
            commands.push_back(build_pass_rule_json(pr));
        }
    }

    return commands;
}

nlohmann::json NftablesFirewall::build_port_match_exprs(L4Proto proto,
                                                          const PortSpec& src_port,
                                                          const PortSpec& dst_port,
                                                          bool negate_src_port,
                                                          bool negate_dst_port) {
    nlohmann::json exprs = nlohmann::json::array();
    if (proto == L4Proto::Any && src_port.empty() && dst_port.empty()) {
        return exprs;
    }
    // proto match (next-header) — never negated
    if (proto != L4Proto::Any) {
        exprs.push_back({{"match", {{"op", "=="}, {"left", {{"meta", {{"key", "l4proto"}}}}}, {"right", l4_proto_name(proto)}}}});
    }
    // For port payload fields, nft expects a transport-header payload protocol.
    // When proto is unspecified, use "th" (transport header) so expressions like
    // dport/sport are still valid.
    const std::string payload_proto = proto == L4Proto::Any ? "th" : l4_proto_name(proto);
    // src_port match
    if (!src_port.empty()) {
        std::string op = negate_src_port ? "!=" : "==";
        exprs.push_back({{"match", {{"op", op}, {"left", {{"payload", {{"protocol", payload_proto}, {"field", "sport"}}}}}, {"right", port_spec_to_nft_rhs(src_port)}}}});
    }
    // dst_port match
    if (!dst_port.empty()) {
        std::string op = negate_dst_port ? "!=" : "==";
        exprs.push_back({{"match", {{"op", op}, {"left", {{"payload", {{"protocol", payload_proto}, {"field", "dport"}}}}}, {"right", port_spec_to_nft_rhs(dst_port)}}}});
    }
    return exprs;
}

// Convert a CIDR list to an nftables JSON right-hand side value.
// Single CIDR → plain string.  Multiple CIDRs → {"set": ["cidr1", "cidr2"]}.
static nlohmann::json cidr_list_to_nft_rhs(const std::vector<std::string>& addrs) {
    auto addr_to_rhs = [](const std::string& addr) -> nlohmann::json {
        const auto slash = addr.find('/');
        if (slash != std::string::npos) {
            const std::string base = addr.substr(0, slash);
            const int len = std::stoi(addr.substr(slash + 1));
            return {{"prefix", {{"addr", base}, {"len", len}}}};
        }

        const bool ipv6 = addr.find(':') != std::string::npos;
        return {{"prefix", {{"addr", addr}, {"len", ipv6 ? 128 : 32}}}};
    };

    if (addrs.size() == 1) return addr_to_rhs(addrs[0]);
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& a : addrs) arr.push_back(addr_to_rhs(a));
    return {{"set", arr}};
}

static nlohmann::json default_gateway_match_exprs(
    const std::string& ip_proto, const FirewallRuleCriteria& criteria) {
    nlohmann::json exprs = nlohmann::json::array();
    if (criteria.default_gateway == DefaultGatewayFamily::None) {
        return exprs;
    }
    exprs.push_back({{"match", {{"op", "!="},
        {"left", {{"payload", {{"protocol", ip_proto}, {"field", "daddr"}}}}},
        {"right", cidr_list_to_nft_rhs(criteria.default_gateway_bypass)}}}});
    return exprs;
}

nlohmann::json NftablesFirewall::build_addr_match_exprs(const std::string& ip_proto,
                                                         const std::vector<std::string>& src_addr,
                                                         const std::vector<std::string>& dst_addr,
                                                         bool negate_src_addr,
                                                         bool negate_dst_addr) {
    nlohmann::json exprs = nlohmann::json::array();
    if (!src_addr.empty()) {
        std::string op = negate_src_addr ? "!=" : "==";
        exprs.push_back({{"match", {{"op", op}, {"left", {{"payload", {{"protocol", ip_proto}, {"field", "saddr"}}}}}, {"right", cidr_list_to_nft_rhs(src_addr)}}}});
    }
    if (!dst_addr.empty()) {
        std::string op = negate_dst_addr ? "!=" : "==";
        exprs.push_back({{"match", {{"op", op}, {"left", {{"payload", {{"protocol", ip_proto}, {"field", "daddr"}}}}}, {"right", cidr_list_to_nft_rhs(dst_addr)}}}});
    }
    return exprs;
}

nlohmann::json NftablesFirewall::build_dscp_match_exprs(const std::string& ip_proto,
                                                        std::optional<uint8_t> dscp) {
    nlohmann::json exprs = nlohmann::json::array();
    if (!dscp.has_value()) {
        return exprs;
    }

    exprs.push_back({{"match", {
        {"op", "=="},
        {"left", {{"payload", {{"protocol", ip_proto}, {"field", "dscp"}}}}},
        {"right", static_cast<int>(*dscp)}
    }}});
    return exprs;
}

nlohmann::json NftablesFirewall::build_mark_rule_json(const PendingRule& pr) {
    std::string ip_proto = (pr.family == AF_INET6) ? "ip6" : "ip";
    nlohmann::json expr = nlohmann::json::array();
    if (pr.family_guard) {
        expr.push_back(family_match_expr(pr.family));
    }
    if (pr.criteria.dst_set_name.has_value()) {
        // set-membership match
        expr.push_back({{"match", {{"op", "=="}, {"left", {{"payload", {{"protocol", ip_proto}, {"field", "daddr"}}}}}, {"right", "@" + *pr.criteria.dst_set_name}}}});
    }
    for (const auto& e : build_dscp_match_exprs(ip_proto, pr.criteria.dscp)) {
        expr.push_back(e);
    }
    // Append src/dst address constraints
    for (const auto& e : build_addr_match_exprs(ip_proto, pr.criteria.src_addr, pr.criteria.dst_addr,
                                                 pr.criteria.negate_src_addr, pr.criteria.negate_dst_addr)) {
        expr.push_back(e);
    }
    for (const auto& e : default_gateway_match_exprs(ip_proto, pr.criteria)) {
        expr.push_back(e);
    }
    // Append proto/port match expressions
    for (const auto& e : build_port_match_exprs(pr.criteria.proto, pr.criteria.src_port, pr.criteria.dst_port,
                                                  pr.criteria.negate_src_port, pr.criteria.negate_dst_port)) {
        expr.push_back(e);
    }
    expr.push_back({{"counter", nullptr}});
    if (pr.save_conntrack_mark) {
        expr.push_back({{"jump", {{"target", setter_chain_name(pr.fwmark)}}}});
    } else if (pr.fwmark_mask == 0xFFFFFFFFu) {
        expr.push_back({{"mangle", {
            {"key", {{"meta", {{"key", "mark"}}}}}, {"value", pr.fwmark}
        }}});
    } else {
        expr.push_back({{"mangle", {
            {"key", {{"meta", {{"key", "mark"}}}}},
            {"value", {{"|", nlohmann::json::array({
                {{"&", nlohmann::json::array({{{"meta", {{"key", "mark"}}}},
                                                  static_cast<uint32_t>(~pr.fwmark_mask)})}},
                pr.fwmark
            })}}}
        }}});
    }
    if (!pr.save_conntrack_mark) {
        expr.push_back({{"accept", nullptr}});
    }
    nlohmann::json command = {{"add", {{"rule", {
        {"family", "inet"},
        {"table", TABLE_NAME},
        {"chain", pr.criteria.apply_output ? OUTPUT_CHAIN_NAME : CHAIN_NAME},
        {"expr", expr}
    }}}}};
    add_rule_comment(command, pr.key);
    return command;
}

nlohmann::json NftablesFirewall::build_balance_rule_json(const PendingRule& pr) {
    const std::string ip_proto = pr.family == AF_INET6 ? "ip6" : "ip";
    nlohmann::json expr = nlohmann::json::array();
    // Balance candidates are filtered independently for each physical family.
    // Keep the two classifiers disjoint even when their packet criteria are
    // otherwise family-neutral (for example, a port-only rule).
    expr.push_back(family_match_expr(pr.family));
    if (pr.criteria.dst_set_name.has_value()) {
        expr.push_back({{"match", {{"op", "=="}, {"left", {{"payload", {{"protocol", ip_proto}, {"field", "daddr"}}}}}, {"right", "@" + *pr.criteria.dst_set_name}}}});
    }
    for (const auto& e : build_dscp_match_exprs(ip_proto, pr.criteria.dscp)) expr.push_back(e);
    for (const auto& e : build_addr_match_exprs(ip_proto, pr.criteria.src_addr, pr.criteria.dst_addr,
                                                 pr.criteria.negate_src_addr, pr.criteria.negate_dst_addr)) expr.push_back(e);
    for (const auto& e : default_gateway_match_exprs(ip_proto, pr.criteria)) expr.push_back(e);
    for (const auto& e : build_port_match_exprs(pr.criteria.proto, pr.criteria.src_port, pr.criteria.dst_port,
                                                 pr.criteria.negate_src_port, pr.criteria.negate_dst_port)) expr.push_back(e);
    const nlohmann::json mark_is_empty = {{"&", nlohmann::json::array({
        {{"meta", {{"key", "mark"}}}}, pr.fwmark_mask
    })}};
    expr.push_back({{"match", {{"op", "=="}, {"left", mark_is_empty}, {"right", 0}}}});
    nlohmann::json targets = nlohmann::json::array();
    for (std::size_t index = 0; index < pr.balance_marks.size(); ++index) {
        targets.push_back(nlohmann::json::array({
            index, {{"jump", {{"target", setter_chain_name(pr.balance_marks[index])}}}}
        }));
    }
    expr.push_back({{"counter", nullptr}});
    nlohmann::json balance_vmap;
    balance_vmap["key"] = {{"numgen", {{"mode", "inc"},
                                           {"mod", pr.balance_marks.size()}}}};
    balance_vmap["data"] = {{"set", targets}};
    expr.push_back({{"vmap", balance_vmap}});
    expr.push_back({{"accept", nullptr}});
    nlohmann::json command = {{"add", {{"rule", {
        {"family", "inet"}, {"table", TABLE_NAME},
        {"chain", pr.criteria.apply_output ? OUTPUT_CHAIN_NAME : CHAIN_NAME}, {"expr", expr}
    }}}}};
    add_rule_comment(command, pr.key);
    return command;
}

nlohmann::json NftablesFirewall::build_drop_rule_json(const PendingRule& pr) {
    std::string ip_proto = (pr.family == AF_INET6) ? "ip6" : "ip";
    nlohmann::json expr = nlohmann::json::array();
    if (pr.criteria.dst_set_name.has_value()) {
        expr.push_back({{"match", {{"op", "=="}, {"left", {{"payload", {{"protocol", ip_proto}, {"field", "daddr"}}}}}, {"right", "@" + *pr.criteria.dst_set_name}}}});
    }
    for (const auto& e : build_dscp_match_exprs(ip_proto, pr.criteria.dscp)) {
        expr.push_back(e);
    }
    // Append src/dst address constraints
    for (const auto& e : build_addr_match_exprs(ip_proto, pr.criteria.src_addr, pr.criteria.dst_addr,
                                                 pr.criteria.negate_src_addr, pr.criteria.negate_dst_addr)) {
        expr.push_back(e);
    }
    for (const auto& e : default_gateway_match_exprs(ip_proto, pr.criteria)) {
        expr.push_back(e);
    }
    // Append proto/port match expressions
    for (const auto& e : build_port_match_exprs(pr.criteria.proto, pr.criteria.src_port, pr.criteria.dst_port,
                                                  pr.criteria.negate_src_port, pr.criteria.negate_dst_port)) {
        expr.push_back(e);
    }
    expr.push_back({{"counter", nullptr}});
    expr.push_back({{"drop", nullptr}});
    nlohmann::json command = {{"add", {{"rule", {
        {"family", "inet"},
        {"table", TABLE_NAME},
        {"chain", pr.criteria.apply_output ? OUTPUT_CHAIN_NAME : CHAIN_NAME},
        {"expr", expr}
    }}}}};
    add_rule_comment(command, pr.key);
    return command;
}

nlohmann::json NftablesFirewall::build_pass_rule_json(const PendingRule& pr) {
    std::string ip_proto = (pr.family == AF_INET6) ? "ip6" : "ip";
    nlohmann::json expr = nlohmann::json::array();
    if (pr.criteria.dst_set_name.has_value()) {
        expr.push_back({{"match", {{"op", "=="}, {"left", {{"payload", {{"protocol", ip_proto}, {"field", "daddr"}}}}}, {"right", "@" + *pr.criteria.dst_set_name}}}});
    }
    for (const auto& e : build_dscp_match_exprs(ip_proto, pr.criteria.dscp)) {
        expr.push_back(e);
    }
    for (const auto& e : build_addr_match_exprs(ip_proto, pr.criteria.src_addr, pr.criteria.dst_addr,
                                                 pr.criteria.negate_src_addr, pr.criteria.negate_dst_addr)) {
        expr.push_back(e);
    }
    for (const auto& e : default_gateway_match_exprs(ip_proto, pr.criteria)) {
        expr.push_back(e);
    }
    for (const auto& e : build_port_match_exprs(pr.criteria.proto, pr.criteria.src_port, pr.criteria.dst_port,
                                                  pr.criteria.negate_src_port, pr.criteria.negate_dst_port)) {
        expr.push_back(e);
    }
    expr.push_back({{"counter", nullptr}});
    expr.push_back({{"accept", nullptr}});
    nlohmann::json command = {{"add", {{"rule", {
        {"family", "inet"},
        {"table", TABLE_NAME},
        {"chain", pr.criteria.apply_output ? OUTPUT_CHAIN_NAME : CHAIN_NAME},
        {"expr", expr}
    }}}}};
    add_rule_comment(command, pr.key);
    return command;
}

nlohmann::json NftablesFirewall::build_elements_json(const std::string& set_name,
                                                      const nlohmann::json& elems) {
    return {{"add", {{"element", {
        {"family", "inet"},
        {"table", TABLE_NAME},
        {"name", set_name},
        {"elem", elems}
    }}}}};
}

// --- apply / cleanup ---

bool NftablesFirewall::table_exists() const {
    return run_cleanup_command({"nft", "list", "table", "inet",
                                std::string(TABLE_NAME)})
               .exit_code == 0;
}

NftablesFirewall::LiveTableState NftablesFirewall::read_live_table_state() const {
    LiveTableState state;
    const auto result = safe_exec_capture(
        {"nft", "-j", "-t", "list", "table", "inet", std::string(TABLE_NAME)},
        /*suppress_stderr=*/true);
    if (result.exit_code != 0 || result.stdout_output.empty()) {
        return state;
    }

    nlohmann::json doc;
    try {
        doc = nlohmann::json::parse(result.stdout_output);
    } catch (const nlohmann::json::parse_error& e) {
        Logger::instance().warn("Failed to parse nft table state: {}", e.what());
        return state;
    }

    const auto nftables_it = doc.find("nftables");
    if (nftables_it == doc.end() || !nftables_it->is_array()) {
        return state;
    }

    for (const auto& item : *nftables_it) {
        if (!item.is_object()) {
            continue;
        }

        if (const auto table_it = item.find("table");
            table_it != item.end() && table_it->is_object()) {
            const auto& table = *table_it;
            if (table.value("family", "") == "inet"
                && table.value("name", "") == TABLE_NAME) {
                state.table_exists = true;
            }
            continue;
        }

        if (const auto chain_it = item.find("chain");
            chain_it != item.end() && chain_it->is_object()) {
            const auto& chain = *chain_it;
            if (chain.value("family", "") == "inet"
                && chain.value("table", "") == TABLE_NAME
                && chain.value("name", "") == CHAIN_NAME) {
                state.chain_exists = true;
            } else if (chain.value("family", "") == "inet"
                       && chain.value("table", "") == TABLE_NAME
                       && chain.value("name", "") == OUTPUT_CHAIN_NAME) {
                state.output_chain_exists = true;
            } else if (chain.value("family", "") == "inet"
                       && chain.value("table", "") == TABLE_NAME) {
                const std::string name = chain.value("name", "");
                if (name.rfind("setmark_", 0) == 0) {
                    try {
                        state.setter_chain_marks.insert(static_cast<uint32_t>(
                            std::stoul(name.substr(8), nullptr, 16)));
                    } catch (const std::exception&) {
                    }
                }
            }
            continue;
        }

        if (const auto set_it = item.find("set");
            set_it != item.end() && set_it->is_object()) {
            const auto& set = *set_it;
            if (set.value("family", "") == "inet"
                && set.value("table", "") == TABLE_NAME) {
                const std::string name = set.value("name", "");
                if (!name.empty()) {
                    state.set_names.insert(name);
                    const std::string type = set.value("type", "");
                    const uint32_t timeout = set.value("timeout", 0U);
                    state.set_schemas[name] = type + ":" + std::to_string(timeout);
                }
            }
        }
    }

    return state;
}

nlohmann::json NftablesFirewall::build_apply_document(const LiveTableState& live_state,
                                                      bool emit_full_table,
                                                      bool static_sets_only,
                                                      bool clear_dynamic_sets,
                                                      bool rules_only) {
    nlohmann::json doc;
    auto& arr = doc["nftables"];
    arr = nlohmann::json::array();

    // metainfo
    arr.push_back({{"metainfo", {{"json_schema_version", 1}}}});

    if (emit_full_table) {
        arr.push_back(build_table_json());
    }

    // Sets. Dynamic dnsmasq sets keep their learned elements during normal
    // re-apply; static sets are refreshed in this same nft transaction.
    if (!rules_only) {
      for (const auto& ps : pending_sets_) {
        const bool existing = !emit_full_table &&
            live_state.set_names.find(ps.name) != live_state.set_names.end();
        if (existing && is_dynamic_set_name(ps.name)) {
            if (clear_dynamic_sets) {
                arr.push_back(build_flush_set_json(ps.name));
            }
            continue;
        }
        if (existing && live_state.set_schemas.at(ps.name) == set_schema_key(ps)) {
            continue;
        }
        if (existing) {
            // Chain removal, old-schema deletion, replacement set creation and
            // new rules are one nft transaction, so no standalone delete can
            // publish an unenforced intermediate state.
            arr.push_back(build_delete_set_json(ps.name));
        }
        arr.push_back(build_set_json(ps));
      }
    }

    if (!static_sets_only) {
        // Chain with prerouting hook
        if (!emit_full_table && live_state.chain_exists) {
            arr.push_back(build_delete_chain_json());
        }
        if (!emit_full_table && live_state.output_chain_exists) {
            arr.push_back(build_delete_output_chain_json());
        }
        for (const uint32_t mark : live_state.setter_chain_marks) {
            arr.push_back(build_delete_setter_chain_json(mark));
        }
        arr.push_back(build_chain_json());
        arr.push_back(build_output_chain_json());

        std::set<uint32_t> setter_marks = owned_marks_;
        for (const auto& rule : pending_rules_) {
            if (rule.action == PendingRule::Mark && rule.fwmark != 0) {
                setter_marks.insert(rule.fwmark);
            }
            if (rule.action == PendingRule::Balance) {
                setter_marks.insert(rule.balance_marks.begin(), rule.balance_marks.end());
            }
        }
        for (const uint32_t mark : setter_marks) {
            arr.push_back(build_setter_chain_json(mark));
            arr.push_back(build_setter_rule_json(mark, fwmark_mask()));
        }

        // Rules
        for (const auto& cmd : build_rule_add_commands(
                 global_prefilter_, pending_rules_, owned_marks_)) {
            arr.push_back(cmd);
        }
    }

    // Elements
    if (!rules_only) {
        for (const auto& [set_name, elems] : pending_elements_) {
            const bool existing = !emit_full_table &&
                live_state.set_names.find(set_name) != live_state.set_names.end();
            if (existing && is_dynamic_set_name(set_name)) {
                continue;
            }
            if (existing) {
                arr.push_back(build_flush_set_json(set_name));
            }
            if (!elems.empty()) {
                arr.push_back(build_elements_json(set_name, elems));
            }
        }
    }

    return doc;
}

void NftablesFirewall::preflight_reused_set_schemas(
    const LiveTableState& live_state) const {
    if (!live_state.table_exists || !live_state.chain_exists ||
        !live_state.output_chain_exists) {
        throw FirewallRulesOnlyError(
            "required nft firewall table or classification chain is missing");
    }
    for (const auto& rule : pending_rules_) {
        if (!rule.criteria.dst_set_name.has_value()) {
            continue;
        }
        const auto expected = std::find_if(
            pending_sets_.begin(), pending_sets_.end(),
            [&](const PendingSet& set) {
                return set.name == *rule.criteria.dst_set_name &&
                       ((rule.family == AF_INET6 && set.type == "ipv6_addr") ||
                        (rule.family == AF_INET && set.type == "ipv4_addr"));
            });
        if (expected == pending_sets_.end()) {
            throw FirewallRulesOnlyError(
                "required reused nft set " + *rule.criteria.dst_set_name +
                " has no compatible declaration for the packet family");
        }
    }
    for (const auto& expected : pending_sets_) {
        const auto it = live_state.set_schemas.find(expected.name);
        if (it == live_state.set_schemas.end()) {
            throw FirewallRulesOnlyError(
                "required reused nft set " + expected.name + " is missing");
        }
        if (it->second != set_schema_key(expected)) {
            throw FirewallRulesOnlyError(
                "required reused nft set " + expected.name +
                " has incompatible family, type, or timeout schema");
        }
    }
}

void NftablesFirewall::apply(FirewallApplyMode mode) {
    // Never delete the live table before publishing a replacement. nft applies
    // this JSON batch atomically, including chain replacement and set refresh.
    const LiveTableState live_state = read_live_table_state();
    const bool static_sets_only = mode == FirewallApplyMode::StaticSetsOnly;
    const bool rules_only = mode == FirewallApplyMode::RulesOnly;
    if (rules_only) {
        preflight_reused_set_schemas(live_state);
        if (!pending_elements_.empty()) {
            throw FirewallRulesOnlyError(
                "RulesOnly received buffered set elements; refusing to modify sets");
        }
    }
    if (static_sets_only && !live_state.table_exists) {
        throw FirewallError("cannot refresh list sets before nft firewall state exists");
    }
    const bool emit_full_table = !live_state.table_exists;
    for (auto& rule : pending_rules_) {
        rule.save_conntrack_mark = global_prefilter_.restore_conntrack_mark &&
                                   global_prefilter_.conntrack_mark_mask != 0;
    }
    const bool clear_dynamic_sets = mode == FirewallApplyMode::Destructive
        && clear_dynamic_sets_on_apply();
    nlohmann::json doc = build_apply_document(
        live_state, emit_full_table, static_sets_only, clear_dynamic_sets,
        rules_only);

    std::string json_str = doc.dump();
    Logger::instance().verbose("nft json:\n{}", json_str);

    // Apply atomically via nft -j -f -
    const int status = safe_exec_pipe_stdin({"nft", "-j", "-f", "-"}, json_str);
    if (status != 0) {
        throw FirewallError(keen_pbr3::format("nft -j -f - exited with status {}", status));
    }

    // Clear pending buffers
    pending_sets_.clear();
    pending_elements_.clear();
    pending_rules_.clear();
    table_created_ = true;
}

void NftablesFirewall::cleanup_live_impl() {
    if (table_created_ || table_exists()) {
        Logger::instance().verbose("nft delete table inet {}", TABLE_NAME);
        run_cleanup_command({"nft", "delete", "table", "inet",
                             std::string(TABLE_NAME)});
        table_created_ = false;
    }
}

void NftablesFirewall::cleanup_impl() {
    cleanup_live_impl();

    created_sets_.clear();
    pending_sets_.clear();
    pending_elements_.clear();
    pending_rules_.clear();
}

void NftablesFirewall::cleanup() {
    cleanup_impl();
}

FirewallBackend NftablesFirewall::backend() const {
    return FirewallBackend::nftables;
}

std::unique_ptr<Firewall> create_nftables_firewall() {
    return std::make_unique<NftablesFirewall>();
}

} // namespace keen_pbr3
