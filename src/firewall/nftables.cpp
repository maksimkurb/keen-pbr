#include "nftables.hpp"

#include <algorithm>
#include <cstdlib>
#include <sstream>
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

int NftablesFirewall::exec_cmd(const std::string& cmd) {
    return std::system(cmd.c_str());
}

void NftablesFirewall::exec_cmd_checked(const std::string& cmd) {
    int ret = exec_cmd(cmd);
    if (ret != 0) {
        throw FirewallError("Command failed (exit " + std::to_string(ret) + "): " + cmd);
    }
}

void NftablesFirewall::ensure_table(const std::string& family_str) {
    if (table_inet_created_) {
        return;
    }
    // Use "inet" family for dual-stack (handles both IPv4 and IPv6)
    exec_cmd_checked("nft add table inet " + std::string(TABLE_NAME));
    table_inet_created_ = true;
}

void NftablesFirewall::ensure_chain(const std::string& family_str, const std::string& chain) {
    std::string key = family_str + ":" + chain;
    if (created_chains_.count(key)) {
        return;
    }
    ensure_table(family_str);
    // Create chain as a prerouting hook in the mangle-equivalent priority
    exec_cmd_checked("nft add chain inet " + std::string(TABLE_NAME) + " " + chain +
                     " '{ type filter hook prerouting priority mangle; policy accept; }'");
    created_chains_[key] = true;
}

void NftablesFirewall::create_ipset(const std::string& set_name, int family,
                                     uint32_t timeout) {
    ensure_table("inet");

    std::string type = (family == AF_INET6) ? "ipv6_addr" : "ipv4_addr";

    std::string flags = "interval";
    std::string extra;
    if (timeout > 0) {
        flags += ", timeout";
        extra = " timeout " + std::to_string(timeout) + "s;";
    }

    exec_cmd_checked("nft add set inet " + std::string(TABLE_NAME) + " " + set_name +
                     " '{ type " + type + "; flags " + flags + ";" + extra + " }'");

    created_sets_[set_name] = family;
}

void NftablesFirewall::add_to_ipset(const std::string& set_name, const std::string& entry,
                                     int32_t entry_timeout) {
    std::string element = entry;
    if (entry_timeout >= 0) {
        element += " timeout " + std::to_string(entry_timeout) + "s";
    }
    exec_cmd_checked("nft add element inet " + std::string(TABLE_NAME) + " " + set_name +
                     " '{ " + element + " }'");
}

void NftablesFirewall::delete_ipset(const std::string& set_name) {
    // Flush and delete the set; ignore errors if it doesn't exist
    exec_cmd("nft flush set inet " + std::string(TABLE_NAME) + " " + set_name + " 2>/dev/null");
    exec_cmd("nft delete set inet " + std::string(TABLE_NAME) + " " + set_name + " 2>/dev/null");

    created_sets_.erase(set_name);
}

void NftablesFirewall::create_mark_rule(const std::string& set_name, uint32_t fwmark,
                                         const std::string& chain) {
    ensure_chain("inet", chain);

    std::string mark_hex = "0x" + ([&]{
        std::ostringstream oss;
        oss << std::hex << fwmark;
        return oss.str();
    })();

    // nft rule: match destination IP against set, then set the mark
    exec_cmd_checked("nft add rule inet " + std::string(TABLE_NAME) + " " + chain +
                     " ip daddr @" + set_name + " meta mark set " + mark_hex);

    mark_rules_.push_back({set_name, fwmark, chain});
}

void NftablesFirewall::delete_mark_rule(const std::string& set_name, uint32_t fwmark,
                                         const std::string& chain) {
    std::string mark_hex = "0x" + ([&]{
        std::ostringstream oss;
        oss << std::hex << fwmark;
        return oss.str();
    })();

    // nft doesn't have a direct "delete rule by spec" like iptables -D.
    // We use nft -a to list rules with handles, find our rule, and delete by handle.
    // For simplicity, use nft delete rule with the exact rule specification via
    // a helper that finds the handle.
    std::string find_cmd = "nft -a list chain inet " + std::string(TABLE_NAME) + " " + chain +
                           " 2>/dev/null | grep '@" + set_name + "' | grep '" + mark_hex + "'" +
                           " | sed 's/.*# handle //' | head -1";

    // Read handle from command output
    std::string full_cmd = "handle=$(" + find_cmd + "); "
                           "if [ -n \"$handle\" ]; then "
                           "nft delete rule inet " + std::string(TABLE_NAME) + " " + chain +
                           " handle $handle; fi";
    exec_cmd(full_cmd);

    // Remove from tracking
    mark_rules_.erase(
        std::remove_if(mark_rules_.begin(), mark_rules_.end(),
                        [&](const MarkRule& r) {
                            return r.set_name == set_name &&
                                   r.fwmark == fwmark &&
                                   r.chain == chain;
                        }),
        mark_rules_.end());
}

void NftablesFirewall::apply() {
    // nft commands are applied immediately.
    // For future optimization, we could batch commands using nft -f with
    // an atomic ruleset file.
}

void NftablesFirewall::cleanup() {
    // The simplest cleanup: delete the entire table, which removes all
    // chains, rules, and sets within it.
    if (table_inet_created_) {
        exec_cmd("nft delete table inet " + std::string(TABLE_NAME) + " 2>/dev/null");
        table_inet_created_ = false;
    }

    mark_rules_.clear();
    created_sets_.clear();
    created_chains_.clear();
}

std::unique_ptr<Firewall> create_nftables_firewall() {
    return std::make_unique<NftablesFirewall>();
}

} // namespace keen_pbr3
