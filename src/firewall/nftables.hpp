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
    // Initialize the nftables backend; does not modify firewall state yet.
    NftablesFirewall();
    // Destructor; does not call cleanup() — caller must do so explicitly.
    ~NftablesFirewall() override;

    // Buffer an nftables named set (ipv4_addr/ipv6_addr, optional timeout).
    void create_ipset(const std::string& set_name, int family,
                      uint32_t timeout = 0) override;

    // Buffer a meta mark set rule that matches the given named set.
    void create_mark_rule(const std::string& set_name, uint32_t fwmark) override;
    // Buffer a drop verdict rule that matches the given named set.
    void create_drop_rule(const std::string& set_name) override;

    // Return an NftBatchVisitor that appends element JSON objects to the pending
    // element buffer for set_name; elements are flushed during apply().
    std::unique_ptr<ListEntryVisitor> create_batch_loader(
        const std::string& set_name, int32_t entry_timeout = -1) override;

    // Atomically apply all pending table/set/rule/element operations via
    // a single 'nft -j -f -' invocation with a JSON batch.
    void apply() override;
    // Delete the inet KeenPbrTable table, removing all sets and rules within it.
    void cleanup() override;
    // Returns FirewallBackend::nftables.
    FirewallBackend backend() const override;

private:
    static constexpr const char* TABLE_NAME = "KeenPbrTable";
    static constexpr const char* CHAIN_NAME = "prerouting";

    // Describes an nftables named set to be created.
    struct PendingSet {
        std::string name;
        std::string type;   // "ipv4_addr" or "ipv6_addr"
        uint32_t timeout;   // entry TTL in seconds (0 = no timeout)
    };

    // Describes a rule to be added to the prerouting chain.
    struct PendingRule {
        std::string set_name; // nftables set name to match (without '@' prefix)
        int family;  // AF_INET or AF_INET6
        enum Action { Mark, Drop } action; // meta mark or drop verdict
        uint32_t fwmark; // only for Mark
    };

    // Build the nftables JSON object for creating the inet KeenPbrTable table.
    static nlohmann::json build_table_json();
    // Build the JSON object for a named set with type and optional timeout.
    static nlohmann::json build_set_json(const PendingSet& ps);
    // Build the JSON object for the prerouting chain (type filter, hook prerouting).
    static nlohmann::json build_chain_json();
    // Build the JSON rule object for a meta mark set action matching a named set.
    static nlohmann::json build_mark_rule_json(const PendingRule& pr);
    // Build the JSON rule object for a drop verdict matching a named set.
    static nlohmann::json build_drop_rule_json(const PendingRule& pr);
    // Build the JSON element-add object for bulk-loading elems into a named set.
    static nlohmann::json build_elements_json(const std::string& set_name,
                                              const nlohmann::json& elems);

    // Sets queued for creation, flushed by apply().
    std::vector<PendingSet> pending_sets_;
    // Per-set element buffers (JSON arrays) for batch element loading, keyed by set name.
    std::map<std::string, nlohmann::json> pending_elements_;
    // Rules queued for insertion into the prerouting chain, flushed by apply().
    std::vector<PendingRule> pending_rules_;

    // Track created sets for family lookup: set_name -> family (AF_INET/AF_INET6)
    std::map<std::string, int> created_sets_;

    // True once the inet KeenPbrTable table has been created via apply().
    bool table_created_ = false;

#ifdef KEEN_PBR3_TESTING
    friend class NftablesBuilderTest;
#endif
};

// Factory function called from firewall.cpp
std::unique_ptr<Firewall> create_nftables_firewall();

} // namespace keen_pbr3
