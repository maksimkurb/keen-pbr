#include "iptables.hpp"
#include "ipset_restore_pipe.hpp"

#include <algorithm>
#include <cstdlib>
#include <sstream>
#include <sys/socket.h>

namespace keen_pbr3 {

IptablesFirewall::IptablesFirewall() = default;

IptablesFirewall::~IptablesFirewall() {
    try {
        cleanup();
    } catch (...) {
        // Best-effort cleanup in destructor
    }
}

int IptablesFirewall::exec_cmd(const std::string& cmd) {
    return std::system(cmd.c_str());
}

void IptablesFirewall::exec_cmd_checked(const std::string& cmd) {
    int ret = exec_cmd(cmd);
    if (ret != 0) {
        throw FirewallError("Command failed (exit " + std::to_string(ret) + "): " + cmd);
    }
}

void IptablesFirewall::create_ipset(const std::string& set_name, int family,
                                     uint32_t timeout) {
    std::string family_str = (family == AF_INET6) ? "inet6" : "inet";

    // Use hash:net for all entries (supports both individual IPs and CIDRs)
    std::string cmd = "ipset create " + set_name + " hash:net family " +
                      family_str;

    if (timeout > 0) {
        cmd += " timeout " + std::to_string(timeout);
    }

    cmd += " -exist";
    exec_cmd_checked(cmd);

    created_sets_[set_name] = family;
}

void IptablesFirewall::add_to_ipset(const std::string& set_name, const std::string& entry,
                                     int32_t entry_timeout) {
    std::string cmd = "ipset add " + set_name + " " + entry;
    if (entry_timeout >= 0) {
        cmd += " timeout " + std::to_string(entry_timeout);
    }
    cmd += " -exist";
    exec_cmd_checked(cmd);
}

void IptablesFirewall::delete_ipset(const std::string& set_name) {
    exec_cmd("ipset flush " + set_name + " 2>/dev/null");
    exec_cmd("ipset destroy " + set_name + " 2>/dev/null");

    created_sets_.erase(set_name);
}

void IptablesFirewall::create_mark_rule(const std::string& set_name, uint32_t fwmark,
                                         const std::string& chain) {
    std::string mark_hex = "0x" + ([&]{
        std::ostringstream oss;
        oss << std::hex << fwmark;
        return oss.str();
    })();

    auto it = created_sets_.find(set_name);
    std::string ipt_cmd = "iptables";
    if (it != created_sets_.end() && it->second == AF_INET6) {
        ipt_cmd = "ip6tables";
    }

    exec_cmd_checked(ipt_cmd + " -t mangle -A " + chain +
                     " -m set --match-set " + set_name + " dst" +
                     " -j MARK --set-mark " + mark_hex);

    mark_rules_.push_back({set_name, fwmark, chain});
}

void IptablesFirewall::delete_mark_rule(const std::string& set_name, uint32_t fwmark,
                                         const std::string& chain) {
    std::string mark_hex = "0x" + ([&]{
        std::ostringstream oss;
        oss << std::hex << fwmark;
        return oss.str();
    })();

    auto it = created_sets_.find(set_name);
    std::string ipt_cmd = "iptables";
    if (it != created_sets_.end() && it->second == AF_INET6) {
        ipt_cmd = "ip6tables";
    }

    exec_cmd(ipt_cmd + " -t mangle -D " + chain +
             " -m set --match-set " + set_name + " dst" +
             " -j MARK --set-mark " + mark_hex + " 2>/dev/null");

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

void IptablesFirewall::create_drop_rule(const std::string& set_name,
                                         const std::string& chain) {
    auto it = created_sets_.find(set_name);
    std::string ipt_cmd = "iptables";
    if (it != created_sets_.end() && it->second == AF_INET6) {
        ipt_cmd = "ip6tables";
    }

    exec_cmd_checked(ipt_cmd + " -t mangle -A " + chain +
                     " -m set --match-set " + set_name + " dst" +
                     " -j DROP");

    drop_rules_.push_back({set_name, chain});
}

std::unique_ptr<ListEntryVisitor> IptablesFirewall::create_batch_loader(
    const std::string& set_name, int32_t entry_timeout) {
    return std::make_unique<IpsetRestoreVisitor>(set_name, entry_timeout);
}

void IptablesFirewall::flush_ipset(const std::string& set_name) {
    exec_cmd_checked("ipset flush " + set_name);
}

void IptablesFirewall::apply() {
    // iptables and ipset commands are applied immediately (no staging).
}

void IptablesFirewall::cleanup() {
    // Remove drop rules in reverse order
    for (auto it = drop_rules_.rbegin(); it != drop_rules_.rend(); ++it) {
        std::string ipt_cmd = "iptables";
        auto set_it = created_sets_.find(it->set_name);
        if (set_it != created_sets_.end() && set_it->second == AF_INET6) {
            ipt_cmd = "ip6tables";
        }

        exec_cmd(ipt_cmd + " -t mangle -D " + it->chain +
                 " -m set --match-set " + it->set_name + " dst" +
                 " -j DROP 2>/dev/null");
    }
    drop_rules_.clear();

    // Remove mark rules in reverse order
    for (auto it = mark_rules_.rbegin(); it != mark_rules_.rend(); ++it) {
        std::string mark_hex = "0x" + ([&]{
            std::ostringstream oss;
            oss << std::hex << it->fwmark;
            return oss.str();
        })();

        std::string ipt_cmd = "iptables";
        auto set_it = created_sets_.find(it->set_name);
        if (set_it != created_sets_.end() && set_it->second == AF_INET6) {
            ipt_cmd = "ip6tables";
        }

        exec_cmd(ipt_cmd + " -t mangle -D " + it->chain +
                 " -m set --match-set " + it->set_name + " dst" +
                 " -j MARK --set-mark " + mark_hex + " 2>/dev/null");
    }
    mark_rules_.clear();

    // Destroy all created ipsets
    std::vector<std::string> set_names;
    set_names.reserve(created_sets_.size());
    for (const auto& [name, _] : created_sets_) {
        set_names.push_back(name);
    }
    for (const auto& name : set_names) {
        delete_ipset(name);
    }
}

std::unique_ptr<Firewall> create_iptables_firewall() {
    return std::make_unique<IptablesFirewall>();
}

} // namespace keen_pbr3
