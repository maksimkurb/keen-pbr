#include "nftables.hpp"
#include "firewall_lowering.hpp"
#include "firewall_plan.hpp"
#include "nft_batch_pipe.hpp"
#include "firewall_rule.hpp"
#include "port_spec_util.hpp"
#include "ip_family.hpp"
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
#include <vector>
#include <sys/socket.h>

namespace keen_pbr3 {

namespace {

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

nlohmann::json family_match_expr(int family) {
    return {{"match", {{"op", "=="},
                        {"left", {{"meta", {{"key", "nfproto"}}}}},
                        {"right", family == AF_INET6 ? "ipv6" : "ipv4"}}}};
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
    pending_ruleset_ = {};
    prepared_mode_ = mode;
    apply_prepared_ = true;
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
    const auto existing = std::find_if(
        pending_sets_.begin(), pending_sets_.end(),
        [&set_name](const PendingSet& pending) { return pending.name == set_name; });
    if (existing == pending_sets_.end()) {
        pending_sets_.push_back(std::move(ps));
    } else if (existing->type != ps.type || existing->timeout != ps.timeout) {
        throw FirewallError("conflicting nft set declaration for " + set_name);
    }
    created_sets_[set_name] = family;
}

void NftablesFirewall::set_owned_marks(const std::vector<uint32_t>& marks) {
    owned_marks_.clear();
    for (const uint32_t mark : marks) {
        if (mark != 0) owned_marks_.insert(mark);
    }
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

// --- Private static helpers ---

nlohmann::json NftablesFirewall::build_table_json() {
    return {{"add", {{"table", {{"family", "inet"}, {"name", TABLE_NAME}}}}}};
}

nlohmann::json NftablesFirewall::build_set_json(const PendingSet& ps) {
    nlohmann::json set;
    if (is_dynamic_set_name(ps.name)) {
        // Dynamic sets hold host addresses written by the daemon with a
        // per-element timeout, so they are plain (non-interval) timeout sets.
        // The set-level default timeout is only emitted when configured.
        set = {
            {"family", "inet"},
            {"table", TABLE_NAME},
            {"name", ps.name},
            {"type", ps.type},
            {"flags", nlohmann::json::array({"timeout"})}
        };
        if (ps.timeout > 0) {
            set["timeout"] = ps.timeout;
        }
        return {{"add", {{"set", set}}}};
    }
    nlohmann::json flags = nlohmann::json::array({"interval"});
    if (ps.timeout > 0) {
        flags.push_back("timeout");
    }
    set = {
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

nlohmann::json NftablesFirewall::build_base_chain_json(
    const PhysicalChainId& id, const PhysicalBaseChain& base) {
    const char* hook = "prerouting";
    switch (base.hook) {
    case PhysicalBaseChain::Hook::prerouting: hook = "prerouting"; break;
    case PhysicalBaseChain::Hook::output: hook = "output"; break;
    case PhysicalBaseChain::Hook::forward: hook = "forward"; break;
    case PhysicalBaseChain::Hook::postrouting: hook = "postrouting"; break;
    case PhysicalBaseChain::Hook::other:
        throw FirewallError("nft base chain has an unsupported hook");
    }
    return {{"add", {{"chain", {
        {"family", "inet"}, {"table", TABLE_NAME}, {"name", id.name},
        {"type", base.type == PhysicalBaseChain::Type::route ? "route" : "filter"},
        {"hook", hook}, {"prio", base.priority},
        {"policy", base.policy_accept ? "accept" : "drop"}
    }}}}};
}

nlohmann::json NftablesFirewall::build_delete_named_chain_json(
    const std::string& name) {
    return {{"delete", {{"chain", {
        {"family", "inet"}, {"table", TABLE_NAME}, {"name", name}
    }}}}};
}

bool NftablesFirewall::is_intercept_chain_name(const std::string& name) {
    return name == "dns_hold" || name == "sniff_fwd" || name == "sniff_out";
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

nlohmann::json NftablesFirewall::build_setter_chain_json(uint32_t fwmark) {
    return {{"add", {{"chain", {
        {"family", "inet"}, {"table", TABLE_NAME}, {"name", nft_setter_chain_name(fwmark)}
    }}}}};
}

nlohmann::json NftablesFirewall::build_delete_setter_chain_json(uint32_t fwmark) {
    return {{"delete", {{"chain", {
        {"family", "inet"}, {"table", TABLE_NAME}, {"name", nft_setter_chain_name(fwmark)}
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
    std::string key = set.type + ":" + std::to_string(set.timeout);
    if (is_dynamic_set_name(set.name)) {
        // Dynamic sets must be plain per-element-timeout sets (see build_set_json).
        key += ":timeout";
    }
    return key;
}

// --- PhysicalRule -> nft JSON ---

namespace {

using json = nlohmann::json;

json meta_key(const char* key) { return {{"meta", {{"key", key}}}}; }

json mark_key(PhysicalMarkKind kind) {
    return kind == PhysicalMarkKind::conntrack
        ? json{{"ct", {{"key", "mark"}}}} : meta_key("mark");
}

json payload(const std::string& protocol, const char* field) {
    return {{"payload", {{"protocol", protocol}, {"field", field}}}};
}

// Single CIDR -> prefix object.  Several -> {"set": [prefix...]}.
json cidr_list_rhs(const std::vector<std::string>& cidrs) {
    const auto to_prefix = [](const std::string& cidr) -> json {
        const auto family = ip_family_of(cidr);
        if (!family.has_value()) {
            throw FirewallError("invalid address in nft rule: " + cidr);
        }
        const auto slash = cidr.find('/');
        if (slash != std::string::npos) {
            return {{"prefix", {{"addr", cidr.substr(0, slash)},
                                 {"len", std::stoi(cidr.substr(slash + 1))}}}};
        }
        const bool ipv6 = *family == FirewallFamily::ipv6;
        return {{"prefix", {{"addr", cidr}, {"len", ipv6 ? 128 : 32}}}};
    };
    if (cidrs.size() == 1) return to_prefix(cidrs.front());
    json set = json::array();
    for (const auto& cidr : cidrs) set.push_back(to_prefix(cidr));
    return {{"set", std::move(set)}};
}

json port_ranges_rhs(const std::vector<PortRange>& ranges) {
    const auto to_value = [](const PortRange& range) -> json {
        return range.from == range.to
            ? json(range.from)
            : json{{"range", json::array({range.from, range.to})}};
    };
    if (ranges.size() == 1) return to_value(ranges.front());
    json set = json::array();
    for (const auto& range : ranges) set.push_back(to_value(range));
    return {{"set", std::move(set)}};
}

json match_expr(const char* op, json left, json right) {
    return {{"match", {{"op", op}, {"left", std::move(left)},
                        {"right", std::move(right)}}}};
}

// `positive_set`: render a positive mark list as an `in` set even when it has
// a single entry (the ct-restore re-check).
json mark_match_expr(const MarkMatch& mark, bool positive_set) {
    json left = mark_key(mark.kind);
    // The bare `meta mark != 0` form is the skip-marked prefilter; every other
    // test names its mask explicitly.
    if (mark.mask != 0xFFFFFFFFu || mark.kind == PhysicalMarkKind::conntrack ||
        !mark.negate) {
        left = {{"&", json::array({std::move(left), mark.mask})}};
    }
    if (mark.negate) {
        return match_expr("!=", std::move(left), mark.values.front());
    }
    if (positive_set || mark.values.size() > 1) {
        json set = json::array();
        for (const uint32_t value : mark.values) set.push_back(value);
        return match_expr("in", std::move(left), {{"set", std::move(set)}});
    }
    return match_expr("==", std::move(left), mark.values.front());
}

void append_ct_state_exprs(json& expr, const CtStateMatch& match) {
    const struct { uint8_t bit; const char* name; bool status; } kBits[] = {
        {ct_new, "new", false}, {ct_established, "established", false},
        {ct_related, "related", false}, {ct_invalid, "invalid", false},
        {ct_untracked, "untracked", false}, {ct_snat, "snat", true},
        {ct_dnat, "dnat", true}};
    for (const bool status : {false, true}) {
        json names = json::array();
        for (const auto& entry : kBits) {
            if (entry.status == status && (match.states & entry.bit) != 0) {
                names.push_back(entry.name);
            }
        }
        if (names.empty()) continue;
        json right = names.size() == 1 ? names.front() : json{{"set", names}};
        expr.push_back(match_expr(match.negate ? "!=" : "in",
                                  {{"ct", {{"key", status ? "status" : "state"}}}},
                                  std::move(right)));
    }
}

void append_match_exprs(json& expr, const PhysicalMatch& match,
                        const std::string& ip_proto, bool late) {
    if (const auto* set = std::get_if<SetMatch>(&match)) {
        expr.push_back(match_expr(
            set->negate ? "!=" : "==",
            payload(ip_proto, set->dir == PhysicalDir::src ? "saddr" : "daddr"),
            "@" + set->name));
    } else if (const auto* dscp = std::get_if<DscpMatch>(&match)) {
        expr.push_back(match_expr("==", payload(ip_proto, "dscp"),
                                  static_cast<int>(dscp->value)));
    } else if (const auto* addr = std::get_if<AddrMatch>(&match)) {
        expr.push_back(match_expr(
            addr->negate ? "!=" : "==",
            payload(ip_proto, addr->dir == PhysicalDir::src ? "saddr" : "daddr"),
            cidr_list_rhs(addr->cidrs)));
    } else if (const auto* proto = std::get_if<ProtoMatch>(&match)) {
        expr.push_back(match_expr("==", meta_key("l4proto"),
                                  l4_proto_name(proto->proto)));
    } else if (const auto* port = std::get_if<PortMatch>(&match)) {
        const char* protocol = port->transport == PhysicalTransport::tcp ? "tcp"
            : port->transport == PhysicalTransport::udp ? "udp" : "th";
        expr.push_back(match_expr(
            port->negate ? "!=" : "==",
            payload(protocol, port->dir == PhysicalDir::src ? "sport" : "dport"),
            port_ranges_rhs(port->ranges)));
    } else if (const auto* iif = std::get_if<IifMatch>(&match)) {
        json right;
        if (iif->names.size() == 1) {
            right = iif->names.front();
        } else {
            right = {{"set", json(iif->names)}};
        }
        expr.push_back(match_expr(iif->negate ? "!=" : "==", meta_key("iifname"),
                                  std::move(right)));
    } else if (const auto* dir = std::get_if<CtDirMatch>(&match)) {
        expr.push_back(match_expr("==", {{"ct", {{"key", "direction"}}}},
                                  dir->original ? 0 : 1));
    } else if (const auto* state = std::get_if<CtStateMatch>(&match)) {
        append_ct_state_exprs(expr, *state);
    } else if (const auto* bytes = std::get_if<ConnbytesMatch>(&match)) {
        json ct = {{"key", bytes->mode == ConnbytesMode::bytes ? "bytes"
                                                               : "packets"}};
        if (bytes->dir != ConnbytesDir::both) {
            ct["dir"] = bytes->dir == ConnbytesDir::original ? "original"
                                                             : "reply";
        }
        // nft prints `ct original packets 1-6` back as one == against a range.
        json right = bytes->from == bytes->to
            ? json(bytes->from)
            : json{{"range", json::array({bytes->from, bytes->to})}};
        expr.push_back(match_expr("==", {{"ct", std::move(ct)}},
                                  std::move(right)));
    } else if (const auto* mark = std::get_if<MarkMatch>(&match)) {
        expr.push_back(mark_match_expr(*mark, late && !mark->negate));
    } else {
        throw FirewallError("cannot render an unknown nft match");
    }
}

// Emission order of the matches of one rule.  Rules are held in canonical kind
// order; the order written to nft keeps the cheapest/most selective tests
// first and puts the conntrack direction before the conntrack mark.
int nft_match_rank(const PhysicalMatch& match) {
    if (std::holds_alternative<SetMatch>(match)) return 0;
    if (std::holds_alternative<DscpMatch>(match)) return 1;
    if (std::holds_alternative<AddrMatch>(match)) return 2;
    if (std::holds_alternative<ProtoMatch>(match)) return 3;
    if (std::holds_alternative<PortMatch>(match)) return 4;
    if (std::holds_alternative<IifMatch>(match)) return 5;
    if (std::holds_alternative<CtDirMatch>(match)) return 6;
    if (std::holds_alternative<CtStateMatch>(match)) return 6;
    if (std::holds_alternative<MarkMatch>(match)) return 7;
    return 8;
}

json mark_value_expr(const SetMarkStmt& stmt) {
    if (stmt.mask == 0xFFFFFFFFu) {
        return stmt.value;
    }
    return {{"|", json::array({
        {{"&", json::array({mark_key(stmt.kind),
                            static_cast<uint32_t>(~stmt.mask)})}},
        stmt.value})}};
}

json setter_target(const PhysicalChainId& target) {
    return {{"jump", {{"target", target.name}}}};
}

void append_statement_exprs(json& expr, const PhysicalStatement& statement,
                            const std::string& ip_proto) {
    if (const auto* mark = std::get_if<SetMarkStmt>(&statement)) {
        expr.push_back({{"mangle", {{"key", mark_key(mark->kind)},
                                     {"value", mark_value_expr(*mark)}}}});
    } else if (const auto* jump = std::get_if<JumpStmt>(&statement)) {
        expr.push_back({{jump->is_goto ? "goto" : "jump",
                         {{"target", jump->target.name}}}});
    } else if (const auto* verdict = std::get_if<VerdictStmt>(&statement)) {
        switch (verdict->verdict) {
        case PhysicalVerdict::accept: expr.push_back({{"accept", nullptr}}); break;
        case PhysicalVerdict::drop: expr.push_back({{"drop", nullptr}}); break;
        case PhysicalVerdict::return_: expr.push_back({{"return", nullptr}}); break;
        }
    } else if (const auto* queue = std::get_if<QueueStmt>(&statement)) {
        json body = {{"num", queue->num}};
        if (queue->bypass) body["flags"] = json::array({"bypass"});
        expr.push_back({{"queue", std::move(body)}});
    } else if (const auto* log = std::get_if<LogStmt>(&statement)) {
        // NFLOG form: group, optional snaplen and queue threshold (kernel
        // default 1 is not written).
        json body = {{"group", log->group}};
        if (log->snaplen != 0) body["snaplen"] = log->snaplen;
        if (log->threshold != 1) body["queue-threshold"] = log->threshold;
        expr.push_back({{"log", std::move(body)}});
    } else if (const auto* vmap = std::get_if<VmapStmt>(&statement)) {
        json key = vmap->key == PhysicalVmapKey::numgen_inc
            ? json{{"numgen", {{"mode", "inc"}, {"mod", vmap->param}}}}
            : json{{"&", json::array({json{{"ct", {{"key", "mark"}}}},
                                       vmap->param})}};
        json targets = json::array();
        for (const auto& [value, chain] : vmap->entries) {
            targets.push_back(json::array({value, setter_target(chain)}));
        }
        expr.push_back({{"vmap", {{"key", std::move(key)},
                                   {"data", {{"set", std::move(targets)}}}}}});
    } else if (const auto* late = std::get_if<LateMatchStmt>(&statement)) {
        append_match_exprs(expr, late->match, ip_proto, /*late=*/true);
    } else {
        throw FirewallError("cannot render an unsupported nft statement");
    }
}

} // namespace

nlohmann::json render_nft_rule(const PhysicalChainId& chain,
                               const PhysicalRule& rule) {
    const std::string ip_proto =
        rule.family == FirewallFamily::ipv6 ? "ip6" : "ip";
    json expr = json::array();

    // A payload match on the address family selects it implicitly; any other
    // family-specific rule needs the explicit guard.
    bool implies_family = false;
    bool restore_vmap = false;
    for (const auto& match : rule.matches) {
        implies_family = implies_family ||
            std::holds_alternative<SetMatch>(match) ||
            std::holds_alternative<DscpMatch>(match) ||
            std::holds_alternative<AddrMatch>(match);
    }
    for (const auto& statement : rule.statements) {
        const auto* vmap = std::get_if<VmapStmt>(&statement);
        restore_vmap = restore_vmap || (vmap != nullptr &&
            vmap->key == PhysicalVmapKey::conntrack_mark_and);
    }
    if (rule.family != FirewallFamily::any && !implies_family) {
        expr.push_back(family_match_expr(
            rule.family == FirewallFamily::ipv6 ? AF_INET6 : AF_INET));
    }
    for (int rank = 0; rank <= 8; ++rank) {
        for (const auto& match : rule.matches) {
            if (nft_match_rank(match) == rank) {
                append_match_exprs(expr, match, ip_proto, /*late=*/false);
            }
        }
    }
    // Counters everywhere except the ct-restore hot path and setter chains.
    if (!restore_vmap && chain.role != PhysicalChainRole::nft_setter) {
        expr.push_back({{"counter", nullptr}});
    }
    for (const auto& statement : rule.statements) {
        append_statement_exprs(expr, statement, ip_proto);
    }

    json command = {{"add", {{"rule", {
        {"family", "inet"}, {"table", "KeenPbrTable"}, {"chain", chain.name},
        {"expr", std::move(expr)}}}}}};
    if (rule.key.has_value()) {
        command["add"]["rule"]["comment"] = rule.key->comment();
    }
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
                if (is_intercept_chain_name(name)) {
                    state.intercept_chains.insert(name);
                } else if (name.rfind("setmark_", 0) == 0) {
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
                    std::string key = type + ":" + std::to_string(timeout);
                    if (is_dynamic_set_name(name)) {
                        // Mirror set_schema_key(): record the live flags so a
                        // legacy interval set is detected as incompatible.
                        std::vector<std::string> flags;
                        if (const auto flags_it = set.find("flags");
                            flags_it != set.end() && flags_it->is_array()) {
                            for (const auto& flag : *flags_it) {
                                if (flag.is_string()) {
                                    flags.push_back(flag.get<std::string>());
                                }
                            }
                        }
                        std::sort(flags.begin(), flags.end());
                        for (const auto& flag : flags) {
                            key += ":" + flag;
                        }
                    }
                    state.set_schemas[name] = std::move(key);
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

    // Live chains whose rules may reference a set must be removed before that
    // set is deleted in the same transaction, otherwise the kernel answers
    // EBUSY and the whole batch rolls back (legacy interval dynamic sets).
    if (!static_sets_only) {
        if (!emit_full_table && live_state.chain_exists) {
            arr.push_back(build_delete_chain_json());
        }
        if (!emit_full_table && live_state.output_chain_exists) {
            arr.push_back(build_delete_output_chain_json());
        }
        for (const uint32_t mark : live_state.setter_chain_marks) {
            arr.push_back(build_delete_setter_chain_json(mark));
        }
        // Interception base chains are replaced like the classification
        // ones; a live one the plan no longer carries is just removed.
        if (!emit_full_table) {
            for (const auto& name : live_state.intercept_chains) {
                arr.push_back(build_delete_named_chain_json(name));
            }
        }
    }

    // Sets. Dynamic sets keep their learned elements during normal
    // re-apply; static sets are refreshed in this same nft transaction.
    if (!rules_only) {
      for (const auto& ps : pending_sets_) {
        const bool existing = !emit_full_table &&
            live_state.set_names.find(ps.name) != live_state.set_names.end();
        if (existing && is_dynamic_set_name(ps.name) &&
            live_state.set_schemas.at(ps.name) == set_schema_key(ps)) {
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
        arr.push_back(build_chain_json());
        arr.push_back(build_output_chain_json());
        for (const auto role : {PhysicalChainRole::nft_dns_hold,
                                PhysicalChainRole::nft_sniff_forward,
                                PhysicalChainRole::nft_sniff_output}) {
            for (const auto& chain : pending_ruleset_.chains) {
                if (chain.id.role == role && chain.base.has_value()) {
                    arr.push_back(build_base_chain_json(chain.id, *chain.base));
                }
            }
        }

        // Setter chains must exist before the rules that jump to them.
        for (const auto& chain : pending_ruleset_.chains) {
            if (chain.id.role != PhysicalChainRole::nft_setter) {
                continue;
            }
            arr.push_back(build_setter_chain_json(chain.id.setter_mark));
            for (const auto& rule : chain.rules) {
                arr.push_back(render_nft_rule(chain.id, rule));
            }
        }

        // Rules, in chain order and plan order inside each chain.
        for (const auto role : {PhysicalChainRole::nft_prerouting,
                                PhysicalChainRole::nft_output,
                                PhysicalChainRole::nft_dns_hold,
                                PhysicalChainRole::nft_sniff_forward,
                                PhysicalChainRole::nft_sniff_output}) {
            for (const auto& chain : pending_ruleset_.chains) {
                if (chain.id.role != role) {
                    continue;
                }
                for (const auto& rule : chain.rules) {
                    arr.push_back(render_nft_rule(chain.id, rule));
                }
            }
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
    for (const auto& chain : pending_ruleset_.chains) {
        for (const auto& rule : chain.rules) {
            for (const auto& match : rule.matches) {
                const auto* set_match = std::get_if<SetMatch>(&match);
                if (set_match == nullptr) {
                    continue;
                }
                const bool ipv6 = rule.family == FirewallFamily::ipv6;
                const auto expected = std::find_if(
                    pending_sets_.begin(), pending_sets_.end(),
                    [&](const PendingSet& set) {
                        return set.name == set_match->name &&
                               set.type == (ipv6 ? "ipv6_addr" : "ipv4_addr");
                    });
                if (expected == pending_sets_.end()) {
                    throw FirewallRulesOnlyError(
                        "required reused nft set " + set_match->name +
                        " has no compatible declaration for the packet family");
                }
            }
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

void NftablesFirewall::clear_pending() {
    pending_sets_.clear();
    pending_elements_.clear();
    pending_ruleset_ = {};
    apply_prepared_ = false;
}

void NftablesFirewall::compile_plan(const FirewallPlan& plan,
                                    FirewallApplyMode mode) {
    (void)mode;
    set_fwmark_mask(plan.fwmark_mask);
    for (const auto& declaration : plan.sets) {
        const int family = declaration.family == FirewallFamily::ipv6 ? AF_INET6
                                                                       : AF_INET;
        create_ipset(physical_set_name(declaration.name), family,
                     declaration.timeout);
    }

    pending_ruleset_ =
        lower_firewall_plan(plan, lowering_context(plan.fwmark_mask));
}

FirewallLoweringContext NftablesFirewall::lowering_context(
    uint32_t fwmark_mask) const {
    FirewallLoweringContext context;
    context.backend = FirewallBackend::nftables;
    context.ipv6_enabled = ipv6_enabled();
    context.fwmark_mask = fwmark_mask;
    context.owned_marks.assign(owned_marks_.begin(), owned_marks_.end());
    context.physical_set_name = [this](const std::string& name) {
        return physical_set_name(name);
    };
    return context;
}

PhysicalRuleset NftablesFirewall::expected_ruleset(
    const FirewallPlan& plan) const {
    return lower_firewall_plan(plan, lowering_context(plan.fwmark_mask));
}

void NftablesFirewall::apply(const FirewallPlan& plan, FirewallApplyMode mode) {
    try {
        validate_firewall_plan_backend(plan, backend());
        if (!apply_prepared_) {
            throw FirewallError("nftables apply was not prepared");
        }
        if (prepared_mode_ != mode) {
            throw FirewallError("nftables apply mode differs from prepare_apply");
        }
        compile_plan(plan, mode);
        apply_prepared(mode);
    } catch (...) {
        clear_pending();
        throw;
    }
}

void NftablesFirewall::apply_prepared(FirewallApplyMode mode) {
    if (!apply_prepared_) {
        throw FirewallError("nftables apply was not prepared");
    }
    apply_prepared_ = false;
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
    pending_ruleset_ = {};
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
    pending_ruleset_ = {};
    apply_prepared_ = false;
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
