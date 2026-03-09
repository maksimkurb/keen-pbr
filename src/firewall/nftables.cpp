#include "nftables.hpp"
#include "nft_batch_pipe.hpp"
#include "../log/logger.hpp"

#include <cstdio>
#include <cstdlib>
#include <format>
#include <nlohmann/json.hpp>
#include <string>
#include <sys/socket.h>

namespace keen_pbr3 {

NftablesFirewall::NftablesFirewall() = default;

NftablesFirewall::~NftablesFirewall() {
    try {
        cleanup();
    } catch (...) {
        // Best-effort cleanup in destructor
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

void NftablesFirewall::create_mark_rule(const std::string& set_name, uint32_t fwmark,
                                         const ProtoPortFilter& filter) {
    auto it = created_sets_.find(set_name);
    int family = (it != created_sets_.end()) ? it->second : AF_INET;

    if (filter.proto == "tcp/udp") {
        for (const char* p : {"tcp", "udp"}) {
            PendingRule pr;
            pr.set_name = set_name;
            pr.family = family;
            pr.action = PendingRule::Mark;
            pr.fwmark = fwmark;
            pr.filter = filter;
            pr.filter.proto = p;
            pending_rules_.push_back(std::move(pr));
        }
    } else {
        PendingRule pr;
        pr.set_name = set_name;
        pr.family = family;
        pr.action = PendingRule::Mark;
        pr.fwmark = fwmark;
        pr.filter = filter;
        pending_rules_.push_back(std::move(pr));
    }
}

void NftablesFirewall::create_drop_rule(const std::string& set_name,
                                         const ProtoPortFilter& filter) {
    auto it = created_sets_.find(set_name);
    int family = (it != created_sets_.end()) ? it->second : AF_INET;

    if (filter.proto == "tcp/udp") {
        for (const char* p : {"tcp", "udp"}) {
            PendingRule pr;
            pr.set_name = set_name;
            pr.family = family;
            pr.action = PendingRule::Drop;
            pr.fwmark = 0;
            pr.filter = filter;
            pr.filter.proto = p;
            pending_rules_.push_back(std::move(pr));
        }
    } else {
        PendingRule pr;
        pr.set_name = set_name;
        pr.family = family;
        pr.action = PendingRule::Drop;
        pr.fwmark = 0;
        pr.filter = filter;
        pending_rules_.push_back(std::move(pr));
    }
}

std::unique_ptr<ListEntryVisitor> NftablesFirewall::create_batch_loader(
    const std::string& set_name, int32_t entry_timeout) {
    // Ensure an entry exists in pending_elements_ for this set (as an empty array)
    auto& buf = pending_elements_[set_name];
    if (!buf.is_array()) {
        buf = nlohmann::json::array();
    }
    return std::make_unique<NftBatchVisitor>(buf, set_name, entry_timeout);
}

// --- Port spec helpers ---

// Parse a port spec into an nftables JSON right-hand side value.
// "443"       → 443  (integer)
// "8000-9000" → {"range": [8000, 9000]}
// "80,443"    → {"set": [80, 443]}
static nlohmann::json port_spec_to_nft_rhs(const std::string& spec) {
    if (spec.find(',') != std::string::npos) {
        nlohmann::json arr = nlohmann::json::array();
        std::string token;
        for (char c : spec) {
            if (c == ',') {
                arr.push_back(std::stoi(token));
                token.clear();
            } else {
                token += c;
            }
        }
        if (!token.empty()) arr.push_back(std::stoi(token));
        return {{"set", arr}};
    }
    auto dash = spec.find('-');
    if (dash != std::string::npos) {
        int lo = std::stoi(spec.substr(0, dash));
        int hi = std::stoi(spec.substr(dash + 1));
        return {{"range", nlohmann::json::array({lo, hi})}};
    }
    return std::stoi(spec);
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

nlohmann::json NftablesFirewall::build_port_match_exprs(const std::string& proto,
                                                          const std::string& src_port,
                                                          const std::string& dst_port,
                                                          bool negate_src_port,
                                                          bool negate_dst_port) {
    nlohmann::json exprs = nlohmann::json::array();
    if (proto.empty() && src_port.empty() && dst_port.empty()) {
        return exprs;
    }
    // proto match (next-header) — never negated
    if (!proto.empty()) {
        exprs.push_back({{"match", {{"op", "=="}, {"left", {{"meta", {{"key", "l4proto"}}}}}, {"right", proto}}}});
    }
    // src_port match
    if (!src_port.empty()) {
        std::string op = negate_src_port ? "!=" : "==";
        exprs.push_back({{"match", {{"op", op}, {"left", {{"payload", {{"protocol", proto}, {"field", "sport"}}}}}, {"right", port_spec_to_nft_rhs(src_port)}}}});
    }
    // dst_port match
    if (!dst_port.empty()) {
        std::string op = negate_dst_port ? "!=" : "==";
        exprs.push_back({{"match", {{"op", op}, {"left", {{"payload", {{"protocol", proto}, {"field", "dport"}}}}}, {"right", port_spec_to_nft_rhs(dst_port)}}}});
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
    expr.push_back({{"match", {{"op", "=="}, {"left", {{"payload", {{"protocol", ip_proto}, {"field", "daddr"}}}}}, {"right", "@" + pr.set_name}}}});
    // Append src/dst address constraints
    for (const auto& e : build_addr_match_exprs(ip_proto, pr.filter.src_addr, pr.filter.dst_addr,
                                                 pr.filter.negate_src_addr, pr.filter.negate_dst_addr)) {
        expr.push_back(e);
    }
    // Append proto/port match expressions
    for (const auto& e : build_port_match_exprs(pr.filter.proto, pr.filter.src_port, pr.filter.dst_port,
                                                  pr.filter.negate_src_port, pr.filter.negate_dst_port)) {
        expr.push_back(e);
    }
    expr.push_back({{"counter", nullptr}});
    expr.push_back({{"mangle", {{"key", {{"meta", {{"key", "mark"}}}}}, {"value", pr.fwmark}}}});
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
    expr.push_back({{"match", {{"op", "=="}, {"left", {{"payload", {{"protocol", ip_proto}, {"field", "daddr"}}}}}, {"right", "@" + pr.set_name}}}});
    // Append src/dst address constraints
    for (const auto& e : build_addr_match_exprs(ip_proto, pr.filter.src_addr, pr.filter.dst_addr,
                                                 pr.filter.negate_src_addr, pr.filter.negate_dst_addr)) {
        expr.push_back(e);
    }
    // Append proto/port match expressions
    for (const auto& e : build_port_match_exprs(pr.filter.proto, pr.filter.src_port, pr.filter.dst_port,
                                                  pr.filter.negate_src_port, pr.filter.negate_dst_port)) {
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

    // Ensure table exists (no-op if already present), then delete for clean slate
    arr.push_back(build_table_json());
    arr.push_back({{"delete", {{"table", {{"family", "inet"}, {"name", TABLE_NAME}}}}}});
    arr.push_back(build_table_json());

    // Sets
    for (const auto& ps : pending_sets_) {
        arr.push_back(build_set_json(ps));
    }

    // Chain with prerouting hook
    arr.push_back(build_chain_json());

    // Rules
    for (const auto& pr : pending_rules_) {
        if (pr.action == PendingRule::Mark) {
            arr.push_back(build_mark_rule_json(pr));
        } else {
            arr.push_back(build_drop_rule_json(pr));
        }
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
    FILE* pipe = popen("nft -j -f -", "w");
    if (!pipe) {
        throw FirewallError("Failed to open pipe to 'nft -j -f -'");
    }

    if (std::fwrite(json_str.data(), 1, json_str.size(), pipe) != json_str.size()) {
        pclose(pipe);
        throw FirewallError("Failed to write nft JSON to pipe");
    }

    int status = pclose(pipe);
    if (status != 0) {
        throw FirewallError(std::format("nft -j -f - exited with status {}", status));
    }

    // Clear pending buffers
    pending_sets_.clear();
    pending_elements_.clear();
    pending_rules_.clear();
    table_created_ = true;
}

void NftablesFirewall::cleanup() {
    if (table_created_) {
        Logger::instance().verbose("nft delete table inet {}", TABLE_NAME);
        std::system(std::format("nft delete table inet {} 2>/dev/null", TABLE_NAME).c_str());
        table_created_ = false;
    }

    created_sets_.clear();
    pending_sets_.clear();
    pending_elements_.clear();
    pending_rules_.clear();
}

FirewallBackend NftablesFirewall::backend() const {
    return FirewallBackend::nftables;
}

std::unique_ptr<Firewall> create_nftables_firewall() {
    return std::make_unique<NftablesFirewall>();
}

} // namespace keen_pbr3
