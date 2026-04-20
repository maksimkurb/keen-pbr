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
static nlohmann::json port_spec_to_nft_rhs(const std::string& spec) {
    PortSpecKind kind = classify_port_spec(spec);
    if (kind == PortSpecKind::List) {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& token : split_port_spec_tokens(spec)) {
            PortSpecKind token_kind = classify_port_spec(token);
            if (token_kind == PortSpecKind::Range) {
                int lo = 0;
                int hi = 0;
                if (!parse_port_range(token, lo, hi)) {
                    throw std::invalid_argument(keen_pbr3::format("Invalid port token '{}' in port spec '{}'", token, spec));
                }
                arr.push_back({{"range", nlohmann::json::array({lo, hi})}});
            } else {
                int port = 0;
                if (!parse_port_value(token, port)) {
                    throw std::invalid_argument(keen_pbr3::format("Invalid port token '{}' in port spec '{}'", token, spec));
                }
                arr.push_back(port);
            }
        }
        return {{"set", arr}};
    }

    if (kind == PortSpecKind::Range) {
        int lo = 0;
        int hi = 0;
        if (!parse_port_range(spec, lo, hi)) {
            throw std::invalid_argument(keen_pbr3::format("Invalid port range '{}'", spec));
        }
        return {{"range", nlohmann::json::array({lo, hi})}};
    }

    int port = 0;
    if (!parse_port_value(spec, port)) {
        throw std::invalid_argument(keen_pbr3::format("Invalid port '{}'", spec));
    }
    return port;
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
                                                          const std::string& src_port,
                                                          const std::string& dst_port,
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
    if (addrs.size() == 1) return addrs[0];
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& a : addrs) arr.push_back(a);
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
    expr.push_back({{"mangle", {{"key", {{"meta", {{"key", "mark"}}}}}, {"value", pr.fwmark}}}});
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

void NftablesFirewall::apply() {
    nlohmann::json doc;
    auto& arr = doc["nftables"];
    arr = nlohmann::json::array();

    // metainfo
    arr.push_back({{"metainfo", {{"json_schema_version", 1}}}});

    // Ensure table exists, then flush for a clean slate.
    arr.push_back(build_table_json());
    arr.push_back({{"flush", {{"table", {{"family", "inet"}, {"name", TABLE_NAME}}}}}});

    // Sets
    for (const auto& ps : pending_sets_) {
        arr.push_back(build_set_json(ps));
    }

    // Chain with prerouting hook
    arr.push_back(build_chain_json());

    // Rules
    for (const auto& cmd : build_rule_add_commands(global_prefilter_, pending_rules_)) {
        arr.push_back(cmd);
    }

    // Elements
    for (const auto& [set_name, elems] : pending_elements_) {
        if (!elems.empty()) {
            arr.push_back(build_elements_json(set_name, elems));
        }
    }

    std::string json_str = doc.dump();
    Logger::instance().verbose("nft json:\n{}", json_str);

    // Apply atomically via nft -j -f -
    int status = safe_exec_pipe_stdin({"nft", "-j", "-f", "-"}, json_str);
    if (status != 0) {
        throw FirewallError(keen_pbr3::format("nft -j -f - exited with status {}", status));
    }

    // Clear pending buffers
    pending_sets_.clear();
    pending_elements_.clear();
    pending_rules_.clear();
    table_created_ = true;
}

void NftablesFirewall::cleanup_impl() {
    if (table_created_) {
        Logger::instance().verbose("nft delete table inet {}", TABLE_NAME);
        safe_exec({"nft", "delete", "table", "inet", std::string(TABLE_NAME)}, /*suppress_output=*/true);
        table_created_ = false;
    }

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

std::optional<bool> NftablesFirewall::test_ip_in_set(const std::string& set_name,
                                                       const std::string& ip) const {
    int exit_code = safe_exec({"nft", "get", "element", "inet", std::string(TABLE_NAME),
                               set_name, "{", ip, "}"}, /*suppress_output=*/true);
    if (exit_code == 127) return std::nullopt; // nft not installed
    return exit_code == 0;
}

std::unique_ptr<Firewall> create_nftables_firewall() {
    return std::make_unique<NftablesFirewall>();
}

} // namespace keen_pbr3
