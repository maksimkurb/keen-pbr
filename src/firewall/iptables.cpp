#include "iptables.hpp"

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

void IptablesFirewall::create_ipset(const std::string& set_name, int family) {
    // Create two ipset sets per logical set: one for individual IPs (hash:ip)
    // and one for CIDR subnets (hash:net). This is because ipset hash:ip
    // doesn't support CIDR entries and hash:net has different performance
    // characteristics for single IPs.
    std::string family_str = (family == AF_INET6) ? "inet6" : "inet";

    // hash:ip set for individual addresses
    exec_cmd_checked("ipset create " + set_name + "_ip hash:ip family " +
                     family_str + " -exist");

    // hash:net set for CIDR subnets
    exec_cmd_checked("ipset create " + set_name + "_net hash:net family " +
                     family_str + " -exist");

    created_sets_[set_name] = family;
}

std::string IptablesFirewall::ipset_type_for_entry(const std::string& entry) {
    if (entry.find('/') != std::string::npos) {
        return "_net";
    }
    return "_ip";
}

void IptablesFirewall::add_to_ipset(const std::string& set_name, const std::string& entry) {
    std::string suffix = ipset_type_for_entry(entry);
    exec_cmd_checked("ipset add " + set_name + suffix + " " + entry + " -exist");
}

void IptablesFirewall::delete_ipset(const std::string& set_name) {
    // Flush and destroy both sub-sets; ignore errors if they don't exist
    exec_cmd("ipset flush " + set_name + "_ip 2>/dev/null");
    exec_cmd("ipset destroy " + set_name + "_ip 2>/dev/null");
    exec_cmd("ipset flush " + set_name + "_net 2>/dev/null");
    exec_cmd("ipset destroy " + set_name + "_net 2>/dev/null");

    created_sets_.erase(set_name);
}

void IptablesFirewall::create_mark_rule(const std::string& set_name, uint32_t fwmark,
                                         const std::string& chain) {
    std::string mark_hex = "0x" + ([&]{
        std::ostringstream oss;
        oss << std::hex << fwmark;
        return oss.str();
    })();

    // Determine iptables command based on set family
    auto it = created_sets_.find(set_name);
    std::string ipt_cmd = "iptables";
    if (it != created_sets_.end() && it->second == AF_INET6) {
        ipt_cmd = "ip6tables";
    }

    // Create mark rules for both the _ip and _net sub-sets
    // Using -t mangle for packet marking
    // -m set --match-set matches against the ipset
    // -j MARK --set-mark sets the fwmark on matching packets
    exec_cmd_checked(ipt_cmd + " -t mangle -A " + chain +
                     " -m set --match-set " + set_name + "_ip dst" +
                     " -j MARK --set-mark " + mark_hex);

    exec_cmd_checked(ipt_cmd + " -t mangle -A " + chain +
                     " -m set --match-set " + set_name + "_net dst" +
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

    // Delete rules (use -D instead of -A); ignore errors if rule doesn't exist
    exec_cmd(ipt_cmd + " -t mangle -D " + chain +
             " -m set --match-set " + set_name + "_ip dst" +
             " -j MARK --set-mark " + mark_hex + " 2>/dev/null");

    exec_cmd(ipt_cmd + " -t mangle -D " + chain +
             " -m set --match-set " + set_name + "_net dst" +
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

void IptablesFirewall::apply() {
    // iptables and ipset commands are applied immediately (no staging).
    // This method exists for interface compatibility.
    // In a future enhancement, we could batch ipset operations using
    // ipset restore for atomicity.
}

void IptablesFirewall::cleanup() {
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
                 " -m set --match-set " + it->set_name + "_ip dst" +
                 " -j MARK --set-mark " + mark_hex + " 2>/dev/null");

        exec_cmd(ipt_cmd + " -t mangle -D " + it->chain +
                 " -m set --match-set " + it->set_name + "_net dst" +
                 " -j MARK --set-mark " + mark_hex + " 2>/dev/null");
    }
    mark_rules_.clear();

    // Destroy all created ipsets
    // Copy keys since delete_ipset modifies the map
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
