#include "iptables.hpp"
#include "ipset_restore_pipe.hpp"

#include <cstdio>
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

void IptablesFirewall::create_ipset(const std::string& set_name, int family,
                                     uint32_t timeout) {
    PendingSet ps;
    ps.name = set_name;
    ps.family_str = (family == AF_INET6) ? "inet6" : "inet";
    ps.timeout = timeout;
    pending_sets_.push_back(std::move(ps));
    created_sets_[set_name] = family;
}

void IptablesFirewall::create_mark_rule(const std::string& set_name, uint32_t fwmark) {
    auto it = created_sets_.find(set_name);
    bool ipv6 = (it != created_sets_.end() && it->second == AF_INET6);

    PendingRule pr;
    pr.set_name = set_name;
    pr.ipv6 = ipv6;
    pr.action = PendingRule::Mark;
    pr.fwmark = fwmark;
    pending_rules_.push_back(std::move(pr));
}

void IptablesFirewall::create_drop_rule(const std::string& set_name) {
    auto it = created_sets_.find(set_name);
    bool ipv6 = (it != created_sets_.end() && it->second == AF_INET6);

    PendingRule pr;
    pr.set_name = set_name;
    pr.ipv6 = ipv6;
    pr.action = PendingRule::Drop;
    pr.fwmark = 0;
    pending_rules_.push_back(std::move(pr));
}

std::unique_ptr<ListEntryVisitor> IptablesFirewall::create_batch_loader(
    const std::string& set_name, int32_t entry_timeout) {
    auto& buf = pending_elements_[set_name];
    return std::make_unique<IpsetRestoreVisitor>(buf, set_name, entry_timeout);
}

static void pipe_to_cmd(const std::string& cmd, const std::string& input) {
    FILE* pipe = popen(cmd.c_str(), "w");
    if (!pipe) {
        throw FirewallError("Failed to open pipe to '" + cmd + "'");
    }
    if (std::fwrite(input.data(), 1, input.size(), pipe) != input.size()) {
        pclose(pipe);
        throw FirewallError("Failed to write to pipe for '" + cmd + "'");
    }
    int status = pclose(pipe);
    if (status != 0) {
        throw FirewallError(cmd + " exited with status " + std::to_string(status));
    }
}

void IptablesFirewall::apply() {
    // Phase 1: ipsets via 'ipset restore -exist'
    {
        std::ostringstream ipset_script;
        for (const auto& ps : pending_sets_) {
            ipset_script << "create " << ps.name << " hash:net family "
                         << ps.family_str;
            if (ps.timeout > 0) {
                ipset_script << " timeout " << ps.timeout;
            }
            ipset_script << " -exist\n";
        }
        for (auto& [set_name, buf] : pending_elements_) {
            std::string elements = buf.str();
            if (!elements.empty()) {
                ipset_script << elements;
            }
        }
        std::string ipset_str = ipset_script.str();
        if (!ipset_str.empty()) {
            pipe_to_cmd("ipset restore -exist", ipset_str);
        }
    }

    // Phase 2: iptables rules via iptables-restore / ip6tables-restore
    // Collect rules by protocol
    bool has_v4 = false;
    bool has_v6 = false;
    for (const auto& pr : pending_rules_) {
        if (pr.ipv6) has_v6 = true;
        else has_v4 = true;
    }

    auto build_ipt_script = [&](bool ipv6) -> std::string {
        std::ostringstream s;
        s << "*mangle\n";
        s << ":" << CHAIN_NAME << " - [0:0]\n";
        s << "-A PREROUTING -j " << CHAIN_NAME << "\n";
        for (const auto& pr : pending_rules_) {
            if (pr.ipv6 != ipv6) continue;
            s << "-A " << CHAIN_NAME
              << " -m set --match-set " << pr.set_name << " dst";
            if (pr.action == PendingRule::Mark) {
                std::ostringstream hex;
                hex << "0x" << std::hex << pr.fwmark;
                s << " -j MARK --set-mark " << hex.str();
            } else {
                s << " -j DROP";
            }
            s << "\n";
        }
        s << "COMMIT\n";
        return s.str();
    };

    if (has_v4) {
        pipe_to_cmd("iptables-restore --noflush", build_ipt_script(false));
        chain_v4_created_ = true;
    }
    if (has_v6) {
        pipe_to_cmd("ip6tables-restore --noflush", build_ipt_script(true));
        chain_v6_created_ = true;
    }

    // Clear pending buffers
    pending_sets_.clear();
    pending_elements_.clear();
    pending_rules_.clear();
}

void IptablesFirewall::cleanup() {
    // Remove jump rules, flush and delete custom chain for IPv4
    if (chain_v4_created_) {
        exec_cmd("iptables -t mangle -D PREROUTING -j " + std::string(CHAIN_NAME) + " 2>/dev/null");
        exec_cmd("iptables -t mangle -F " + std::string(CHAIN_NAME) + " 2>/dev/null");
        exec_cmd("iptables -t mangle -X " + std::string(CHAIN_NAME) + " 2>/dev/null");
        chain_v4_created_ = false;
    }

    // Same for IPv6
    if (chain_v6_created_) {
        exec_cmd("ip6tables -t mangle -D PREROUTING -j " + std::string(CHAIN_NAME) + " 2>/dev/null");
        exec_cmd("ip6tables -t mangle -F " + std::string(CHAIN_NAME) + " 2>/dev/null");
        exec_cmd("ip6tables -t mangle -X " + std::string(CHAIN_NAME) + " 2>/dev/null");
        chain_v6_created_ = false;
    }

    // Destroy all created ipsets
    for (const auto& [name, _] : created_sets_) {
        exec_cmd("ipset flush " + name + " 2>/dev/null");
        exec_cmd("ipset destroy " + name + " 2>/dev/null");
    }
    created_sets_.clear();

    pending_sets_.clear();
    pending_elements_.clear();
    pending_rules_.clear();
}

std::unique_ptr<Firewall> create_iptables_firewall() {
    return std::make_unique<IptablesFirewall>();
}

} // namespace keen_pbr3
