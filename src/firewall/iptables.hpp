#pragma once

#include "firewall.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace keen_pbr3 {

class IptablesFirewall : public Firewall {
public:
    IptablesFirewall();
    ~IptablesFirewall() override;

    void create_ipset(const std::string& set_name, int family) override;
    void add_to_ipset(const std::string& set_name, const std::string& entry) override;
    void delete_ipset(const std::string& set_name) override;

    void create_mark_rule(const std::string& set_name, uint32_t fwmark,
                          const std::string& chain = "PREROUTING") override;
    void delete_mark_rule(const std::string& set_name, uint32_t fwmark,
                          const std::string& chain = "PREROUTING") override;

    void apply() override;
    void cleanup() override;

private:
    // Determine ipset type based on whether entry contains '/' (CIDR) or not
    static std::string ipset_type_for_entry(const std::string& entry);

    // Execute a shell command and return exit code
    static int exec_cmd(const std::string& cmd);

    // Execute a shell command; throw FirewallError on failure
    static void exec_cmd_checked(const std::string& cmd);

    // Track created ipsets: set_name -> family (AF_INET/AF_INET6)
    std::map<std::string, int> created_sets_;

    // Track created iptables mark rules for cleanup
    struct MarkRule {
        std::string set_name;
        uint32_t fwmark;
        std::string chain;
    };
    std::vector<MarkRule> mark_rules_;
};

// Factory function called from firewall.cpp
std::unique_ptr<Firewall> create_iptables_firewall();

} // namespace keen_pbr3
