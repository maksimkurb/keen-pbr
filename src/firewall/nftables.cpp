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

void NftablesFirewall::create_mark_rule(const std::string& set_name, uint32_t fwmark) {
    auto it = created_sets_.find(set_name);
    int family = (it != created_sets_.end()) ? it->second : AF_INET;

    PendingRule pr;
    pr.set_name = set_name;
    pr.family = family;
    pr.action = PendingRule::Mark;
    pr.fwmark = fwmark;
    pending_rules_.push_back(std::move(pr));
}

void NftablesFirewall::create_drop_rule(const std::string& set_name) {
    auto it = created_sets_.find(set_name);
    int family = (it != created_sets_.end()) ? it->second : AF_INET;

    PendingRule pr;
    pr.set_name = set_name;
    pr.family = family;
    pr.action = PendingRule::Drop;
    pr.fwmark = 0;
    pending_rules_.push_back(std::move(pr));
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

nlohmann::json NftablesFirewall::build_mark_rule_json(const PendingRule& pr) {
    std::string proto = (pr.family == AF_INET6) ? "ip6" : "ip";
    return {{"add", {{"rule", {
        {"family", "inet"},
        {"table", TABLE_NAME},
        {"chain", CHAIN_NAME},
        {"expr", nlohmann::json::array({
            {{"match", {{"op", "=="}, {"left", {{"payload", {{"protocol", proto}, {"field", "daddr"}}}}}, {"right", "@" + pr.set_name}}}},
            {{"counter", nullptr}},
            {{"mangle", {{"key", {{"meta", {{"key", "mark"}}}}}, {"value", pr.fwmark}}}}
        })}
    }}}}};
}

nlohmann::json NftablesFirewall::build_drop_rule_json(const PendingRule& pr) {
    std::string proto = (pr.family == AF_INET6) ? "ip6" : "ip";
    return {{"add", {{"rule", {
        {"family", "inet"},
        {"table", TABLE_NAME},
        {"chain", CHAIN_NAME},
        {"expr", nlohmann::json::array({
            {{"match", {{"op", "=="}, {"left", {{"payload", {{"protocol", proto}, {"field", "daddr"}}}}}, {"right", "@" + pr.set_name}}}},
            {{"counter", nullptr}},
            {{"drop", nullptr}}
        })}
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
