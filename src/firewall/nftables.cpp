#include "nftables.hpp"
#include "nft_batch_pipe.hpp"

#include <cstdio>
#include <cstdlib>
#include <format>
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
    ps.flags = "interval";
    ps.timeout = timeout;
    if (timeout > 0) {
        ps.flags += ", timeout";
    }
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
    // Ensure an entry exists in pending_elements_ for this set
    auto& buf = pending_elements_[set_name];
    return std::make_unique<NftBatchVisitor>(buf, set_name, entry_timeout);
}

void NftablesFirewall::apply() {
    std::string script;

    // Ensure table exists (no-op if already present), then delete for clean slate
    script += std::format("table inet {}\n", TABLE_NAME);
    script += std::format("delete table inet {}\n", TABLE_NAME);

    // Begin table definition
    script += std::format("table inet {} {{\n", TABLE_NAME);

    // Define all sets
    for (const auto& ps : pending_sets_) {
        if (ps.timeout > 0) {
            script += std::format("  set {} {{ type {}; flags {}; timeout {}s; }}\n",
                                  ps.name, ps.type, ps.flags, ps.timeout);
        } else {
            script += std::format("  set {} {{ type {}; flags {}; }}\n",
                                  ps.name, ps.type, ps.flags);
        }
    }

    // Define chain with all rules
    script += std::format("  chain {} {{\n", CHAIN_NAME);
    script += "    type filter hook prerouting priority mangle; policy accept;\n";

    for (const auto& pr : pending_rules_) {
        std::string addr_family = (pr.family == AF_INET6) ? "ip6" : "ip";

        if (pr.action == PendingRule::Mark) {
            script += std::format("    {} daddr @{} meta mark set {:#x}\n",
                                  addr_family, pr.set_name, pr.fwmark);
        } else {
            script += std::format("    {} daddr @{} drop\n",
                                  addr_family, pr.set_name);
        }
    }

    script += "  }\n";
    script += "}\n";

    // Append all buffered element additions (outside the table block)
    for (auto& [set_name, buf] : pending_elements_) {
        std::string elements = buf.str();
        if (!elements.empty()) {
            script += elements;
        }
    }

    // Apply atomically via nft -f -
    FILE* pipe = popen("nft -f -", "w");
    if (!pipe) {
        throw FirewallError("Failed to open pipe to 'nft -f -'");
    }

    if (std::fwrite(script.data(), 1, script.size(), pipe) != script.size()) {
        pclose(pipe);
        throw FirewallError("Failed to write nft script to pipe");
    }

    int status = pclose(pipe);
    if (status != 0) {
        throw FirewallError(std::format("nft -f - exited with status {}", status));
    }

    // Clear pending buffers
    pending_sets_.clear();
    pending_elements_.clear();
    pending_rules_.clear();
    table_created_ = true;
}

void NftablesFirewall::cleanup() {
    if (table_created_) {
        std::system(std::format("nft delete table inet {} 2>/dev/null", TABLE_NAME).c_str());
        table_created_ = false;
    }

    created_sets_.clear();
    pending_sets_.clear();
    pending_elements_.clear();
    pending_rules_.clear();
}

std::unique_ptr<Firewall> create_nftables_firewall() {
    return std::make_unique<NftablesFirewall>();
}

} // namespace keen_pbr3
