#pragma once

#include "firewall.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace keen_pbr3 {

class IptablesFirewall : public Firewall {
public:
    IptablesFirewall();
    ~IptablesFirewall() override;

    void create_ipset(const std::string& set_name, int family,
                      uint32_t timeout = 0) override;

    void create_mark_rule(const std::string& set_name, uint32_t fwmark) override;
    void create_drop_rule(const std::string& set_name) override;

    std::unique_ptr<ListEntryVisitor> create_batch_loader(
        const std::string& set_name, int32_t entry_timeout = -1) override;

    void apply() override;
    void cleanup() override;

private:
    static constexpr const char* CHAIN_NAME = "KeenPbrTable";

    // Execute a shell command and return exit code
    static int exec_cmd(const std::string& cmd);

    struct PendingSet {
        std::string name;
        std::string family_str; // "inet" or "inet6"
        uint32_t timeout;
    };

    struct PendingRule {
        std::string set_name;
        bool ipv6;
        enum Action { Mark, Drop } action;
        uint32_t fwmark; // only for Mark
    };

    std::vector<PendingSet> pending_sets_;
    std::map<std::string, std::ostringstream> pending_elements_;
    std::vector<PendingRule> pending_rules_;

    // Track created ipsets: set_name -> family (AF_INET/AF_INET6)
    std::map<std::string, int> created_sets_;

    // Track whether chain + jump rule exist for each protocol
    bool chain_v4_created_ = false;
    bool chain_v6_created_ = false;
};

// Factory function called from firewall.cpp
std::unique_ptr<Firewall> create_iptables_firewall();

} // namespace keen_pbr3
