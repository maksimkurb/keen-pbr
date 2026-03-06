#pragma once

#include "firewall.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace keen_pbr3 {

class NftablesFirewall : public Firewall {
public:
    NftablesFirewall();
    ~NftablesFirewall() override;

    void create_ipset(const std::string& set_name, int family,
                      uint32_t timeout = 0) override;

    void create_mark_rule(const std::string& set_name, uint32_t fwmark) override;
    void create_drop_rule(const std::string& set_name) override;

    std::unique_ptr<ListEntryVisitor> create_batch_loader(
        const std::string& set_name, int32_t entry_timeout = -1) override;

    void apply() override;
    void cleanup() override;
    FirewallBackend backend() const override;

private:
    static constexpr const char* TABLE_NAME = "KeenPbrTable";
    static constexpr const char* CHAIN_NAME = "prerouting";

    struct PendingSet {
        std::string name;
        std::string type;   // "ipv4_addr" or "ipv6_addr"
        uint32_t timeout;
    };

    struct PendingRule {
        std::string set_name;
        int family;  // AF_INET or AF_INET6
        enum Action { Mark, Drop } action;
        uint32_t fwmark; // only for Mark
    };

    static nlohmann::json build_table_json();
    static nlohmann::json build_set_json(const PendingSet& ps);
    static nlohmann::json build_chain_json();
    static nlohmann::json build_mark_rule_json(const PendingRule& pr);
    static nlohmann::json build_drop_rule_json(const PendingRule& pr);
    static nlohmann::json build_elements_json(const std::string& set_name,
                                              const nlohmann::json& elems);

    std::vector<PendingSet> pending_sets_;
    std::map<std::string, nlohmann::json> pending_elements_;
    std::vector<PendingRule> pending_rules_;

    // Track created sets for family lookup: set_name -> family (AF_INET/AF_INET6)
    std::map<std::string, int> created_sets_;

    bool table_created_ = false;

#ifdef KEEN_PBR3_TESTING
    friend class NftablesBuilderTest;
#endif
};

// Factory function called from firewall.cpp
std::unique_ptr<Firewall> create_nftables_firewall();

} // namespace keen_pbr3
