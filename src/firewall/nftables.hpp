#pragma once

#include "firewall.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace keen_pbr3 {

class NftablesFirewall : public Firewall {
public:
    NftablesFirewall();
    ~NftablesFirewall() override;

    void create_ipset(const std::string& set_name, int family,
                      uint32_t timeout = 0) override;
    void add_to_ipset(const std::string& set_name, const std::string& entry,
                      int32_t entry_timeout = -1) override;
    void delete_ipset(const std::string& set_name) override;

    void create_mark_rule(const std::string& set_name, uint32_t fwmark,
                          const std::string& chain = "PREROUTING") override;
    void delete_mark_rule(const std::string& set_name, uint32_t fwmark,
                          const std::string& chain = "PREROUTING") override;
    void create_drop_rule(const std::string& set_name,
                          const std::string& chain = "PREROUTING") override;

    std::unique_ptr<ListEntryVisitor> create_batch_loader(
        const std::string& set_name, int32_t entry_timeout = -1) override;
    void flush_ipset(const std::string& set_name) override;

    void apply() override;
    void cleanup() override;

private:
    static constexpr const char* TABLE_NAME = "keen_pbr3";

    // Execute a shell command and return exit code
    static int exec_cmd(const std::string& cmd);

    // Execute a shell command; throw FirewallError on failure
    static void exec_cmd_checked(const std::string& cmd);

    // Ensure nft table exists
    void ensure_table(const std::string& family_str);

    // Ensure nft chain exists within the table
    void ensure_chain(const std::string& family_str, const std::string& chain);

    // Track created nft sets: set_name -> family (AF_INET/AF_INET6)
    std::map<std::string, int> created_sets_;

    // Track created mark rules for cleanup
    struct MarkRule {
        std::string set_name;
        uint32_t fwmark;
        std::string chain;
    };
    std::vector<MarkRule> mark_rules_;

    // Track created DROP rules for cleanup
    struct DropRule {
        std::string set_name;
        std::string chain;
    };
    std::vector<DropRule> drop_rules_;

    // Track which tables have been created
    bool table_inet_created_ = false;

    // Track which chains have been created: "family:chain"
    std::map<std::string, bool> created_chains_;
};

// Factory function called from firewall.cpp
std::unique_ptr<Firewall> create_nftables_firewall();

} // namespace keen_pbr3
