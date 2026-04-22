#include "nftables.hpp"
#include "nft_batch_pipe.hpp"
#include "port_spec_util.hpp"
#include "../log/logger.hpp"
#include "../util/format_compat.hpp"
#include "../util/safe_exec.hpp"

#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <sys/socket.h>

namespace keen_pbr3 {

namespace {

bool is_ipv6_addr(const std::string& addr) {
    return addr.find(':') != std::string::npos;
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
        || !criteria.src_addr.empty()
        || !criteria.dst_addr.empty();
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

void NftablesFirewall::create_ipset(const std::string& set_name, int family,
                                     uint32_t timeout) {
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
                                               const FirewallRuleCriteria& criteria) {
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
        pr.criteria = criteria;
        pr.criteria.proto = proto;
        if (!criteria.src_addr.empty()) {
            pr.criteria.src_addr = filtered_src_addrs;
        }
        if (!criteria.dst_addr.empty()) {
            pr.criteria.dst_addr = filtered_dst_addrs;
        }
        pending_rules_.push_back(std::move(pr));
    }
}

void NftablesFirewall::create_mark_rule(uint32_t fwmark,
                                        const FirewallRuleCriteria& criteria) {
    if (criteria.dst_set_name.has_value()) {
        auto it = created_sets_.find(*criteria.dst_set_name);
        int family = (it != created_sets_.end()) ? it->second : AF_INET;
        append_rules_for_family(family, PendingRule::Mark, fwmark, criteria);
        return;
    }
    if (!needs_family_specific_rule(criteria)) {
        append_rules_for_family(AF_INET, PendingRule::Mark, fwmark, criteria);
        return;
    }
    append_rules_for_family(AF_INET, PendingRule::Mark, fwmark, criteria);
    append_rules_for_family(AF_INET6, PendingRule::Mark, fwmark, criteria);
}

void NftablesFirewall::create_drop_rule(const FirewallRuleCriteria& criteria) {
    if (criteria.dst_set_name.has_value()) {
        auto it = created_sets_.find(*criteria.dst_set_name);
        int family = (it != created_sets_.end()) ? it->second : AF_INET;
        append_rules_for_family(family, PendingRule::Drop, 0, criteria);
        return;
    }
    if (!needs_family_specific_rule(criteria)) {
        append_rules_for_family(AF_INET, PendingRule::Drop, 0, criteria);
        return;
    }
    append_rules_for_family(AF_INET, PendingRule::Drop, 0, criteria);
    append_rules_for_family(AF_INET6, PendingRule::Drop, 0, criteria);
}

void NftablesFirewall::create_pass_rule(const FirewallRuleCriteria& criteria) {
    if (criteria.dst_set_name.has_value()) {
        auto it = created_sets_.find(*criteria.dst_set_name);
        int family = (it != created_sets_.end()) ? it->second : AF_INET;
        append_rules_for_family(family, PendingRule::Pass, 0, criteria);
        return;
    }
    if (!needs_family_specific_rule(criteria)) {
        append_rules_for_family(AF_INET, PendingRule::Pass, 0, criteria);
        return;
    }
    append_rules_for_family(AF_INET, PendingRule::Pass, 0, criteria);
    append_rules_for_family(AF_INET6, PendingRule::Pass, 0, criteria);
}

std::unique_ptr<ListEntryVisitor> NftablesFirewall::create_batch_loader(
    const std::string& set_name) {
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

nlohmann::json NftablesFirewall::build_delete_chain_json() {
    return {{"delete", {{"chain", {
        {"family", "inet"},
        {"table", TABLE_NAME},
        {"name", CHAIN_NAME}
    }}}}};
}

nlohmann::json NftablesFirewall::build_rule_add_commands(
    const FirewallGlobalPrefilter& prefilter,
    const std::vector<PendingRule>& rules) {
    nlohmann::json commands = nlohmann::json::array();

    if (prefilter.skip_established_or_dnat) {
        nlohmann::json dnat_expr = nlohmann::json::array();
        dnat_expr.push_back({{"match", {
            {"op", "=="},
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
        commands.push_back({{"add", {{"rule", {
            {"family", "inet"},
            {"table", TABLE_NAME},
            {"chain", CHAIN_NAME},
            {"expr", marked_expr}
        }}}}});
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

nlohmann::json NftablesFirewall::build_mark_rule_json(const PendingRule& pr) {
    std::string ip_proto = (pr.family == AF_INET6) ? "ip6" : "ip";
    nlohmann::json expr = nlohmann::json::array();
    if (pr.criteria.dst_set_name.has_value()) {
        // set-membership match
        expr.push_back({{"match", {{"op", "=="}, {"left", {{"payload", {{"protocol", ip_proto}, {"field", "daddr"}}}}}, {"right", "@" + *pr.criteria.dst_set_name}}}});
    }
    // Append src/dst address constraints
    for (const auto& e : build_addr_match_exprs(ip_proto, pr.criteria.src_addr, pr.criteria.dst_addr,
                                                 pr.criteria.negate_src_addr, pr.criteria.negate_dst_addr)) {
        expr.push_back(e);
    }
    // Append proto/port match expressions
    for (const auto& e : build_port_match_exprs(pr.criteria.proto, pr.criteria.src_port, pr.criteria.dst_port,
                                                  pr.criteria.negate_src_port, pr.criteria.negate_dst_port)) {
        expr.push_back(e);
    }
    expr.push_back({{"counter", nullptr}});
    if (pr.fwmark_mask == 0xFFFFFFFFu) {
        expr.push_back({{"mangle", {
            {"key", {{"meta", {{"key", "mark"}}}}},
            {"value", pr.fwmark}
        }}});
    } else {
        expr.push_back({{"mangle", {
            {"key", {{"meta", {{"key", "mark"}}}}},
            {"value", {{"|", nlohmann::json::array({
                {{"&", nlohmann::json::array({
                    {{"meta", {{"key", "mark"}}}},
                    static_cast<uint32_t>(~pr.fwmark_mask)
                })}},
                pr.fwmark
            })}}}
        }}});
    }
    expr.push_back({{"accept", nullptr}});
    return {{"add", {{"rule", {
        {"family", "inet"},
        {"table", TABLE_NAME},
        {"chain", CHAIN_NAME},
        {"expr", expr}
    }}}}};
}

nlohmann::json NftablesFirewall::build_drop_rule_json(const PendingRule& pr) {
    std::string ip_proto = (pr.family == AF_INET6) ? "ip6" : "ip";
    nlohmann::json expr = nlohmann::json::array();
    if (pr.criteria.dst_set_name.has_value()) {
        expr.push_back({{"match", {{"op", "=="}, {"left", {{"payload", {{"protocol", ip_proto}, {"field", "daddr"}}}}}, {"right", "@" + *pr.criteria.dst_set_name}}}});
    }
    // Append src/dst address constraints
    for (const auto& e : build_addr_match_exprs(ip_proto, pr.criteria.src_addr, pr.criteria.dst_addr,
                                                 pr.criteria.negate_src_addr, pr.criteria.negate_dst_addr)) {
        expr.push_back(e);
    }
    // Append proto/port match expressions
    for (const auto& e : build_port_match_exprs(pr.criteria.proto, pr.criteria.src_port, pr.criteria.dst_port,
                                                  pr.criteria.negate_src_port, pr.criteria.negate_dst_port)) {
        expr.push_back(e);
    }
    expr.push_back({{"counter", nullptr}});
    expr.push_back({{"drop", nullptr}});
    return {{"add", {{"rule", {
        {"family", "inet"},
        {"table", TABLE_NAME},
        {"chain", CHAIN_NAME},
        {"expr", expr}
    }}}}};
}

nlohmann::json NftablesFirewall::build_pass_rule_json(const PendingRule& pr) {
    std::string ip_proto = (pr.family == AF_INET6) ? "ip6" : "ip";
    nlohmann::json expr = nlohmann::json::array();
    if (pr.criteria.dst_set_name.has_value()) {
        expr.push_back({{"match", {{"op", "=="}, {"left", {{"payload", {{"protocol", ip_proto}, {"field", "daddr"}}}}}, {"right", "@" + *pr.criteria.dst_set_name}}}});
    }
    for (const auto& e : build_addr_match_exprs(ip_proto, pr.criteria.src_addr, pr.criteria.dst_addr,
                                                 pr.criteria.negate_src_addr, pr.criteria.negate_dst_addr)) {
        expr.push_back(e);
    }
    for (const auto& e : build_port_match_exprs(pr.criteria.proto, pr.criteria.src_port, pr.criteria.dst_port,
                                                  pr.criteria.negate_src_port, pr.criteria.negate_dst_port)) {
        expr.push_back(e);
    }
    expr.push_back({{"counter", nullptr}});
    expr.push_back({{"accept", nullptr}});
    return {{"add", {{"rule", {
        {"family", "inet"},
        {"table", TABLE_NAME},
        {"chain", CHAIN_NAME},
        {"expr", expr}
    }}}}};
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
    return safe_exec({"nft", "list", "table", "inet", std::string(TABLE_NAME)},
                     /*suppress_output=*/true) == 0;
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
                }
            }
        }
    }

    return state;
}

nlohmann::json NftablesFirewall::build_apply_document(const LiveTableState& live_state,
                                                      bool emit_full_table) {
    nlohmann::json doc;
    auto& arr = doc["nftables"];
    arr = nlohmann::json::array();

    // metainfo
    arr.push_back({{"metainfo", {{"json_schema_version", 1}}}});

    if (emit_full_table) {
        arr.push_back(build_table_json());
    }

    // Sets
    for (const auto& ps : pending_sets_) {
        if (!emit_full_table && live_state.set_names.find(ps.name) != live_state.set_names.end()) {
            continue;
        }
        arr.push_back(build_set_json(ps));
    }

    // Chain with prerouting hook
    if (!emit_full_table && live_state.chain_exists) {
        arr.push_back(build_delete_chain_json());
    }
    arr.push_back(build_chain_json());

    // Rules
    for (const auto& cmd : build_rule_add_commands(global_prefilter_, pending_rules_)) {
        arr.push_back(cmd);
    }

    // Elements
    for (const auto& [set_name, elems] : pending_elements_) {
        if (!emit_full_table && live_state.set_names.find(set_name) != live_state.set_names.end()) {
            continue;
        }
        if (!elems.empty()) {
            arr.push_back(build_elements_json(set_name, elems));
        }
    }

    return doc;
}

void NftablesFirewall::apply(FirewallApplyMode mode) {
    const bool preserve_sets = mode == FirewallApplyMode::PreserveSets;
    const LiveTableState live_state = preserve_sets ? read_live_table_state() : LiveTableState{};

    if (mode == FirewallApplyMode::Destructive) {
        cleanup_live_impl();
    }

    const bool emit_full_table = !preserve_sets || !live_state.table_exists;
    nlohmann::json doc = build_apply_document(live_state, emit_full_table);

    std::string json_str = doc.dump();
    Logger::instance().verbose("nft json:\n{}", json_str);

    // Apply atomically via nft -j -f -
    int status = safe_exec_pipe_stdin({"nft", "-j", "-f", "-"}, json_str);
    if (status != 0 && preserve_sets && !emit_full_table && !table_exists()) {
        Logger::instance().warn(
            "nft preserve apply failed after KeenPbrTable disappeared; retrying full table restore");
        cleanup_live_impl();
        doc = build_apply_document(LiveTableState{}, /*emit_full_table=*/true);
        json_str = doc.dump();
        Logger::instance().verbose("nft recovery json:\n{}", json_str);
        status = safe_exec_pipe_stdin({"nft", "-j", "-f", "-"}, json_str);
    }
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
        safe_exec({"nft", "delete", "table", "inet", std::string(TABLE_NAME)}, /*suppress_output=*/true);
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
