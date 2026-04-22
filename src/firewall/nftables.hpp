#pragma once

#include "firewall.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <vector>

namespace keen_pbr3 {

class NftablesFirewall : public Firewall {
public:
    // Initialize the nftables backend; does not modify firewall state yet.
    NftablesFirewall();
    // Destructor performs best-effort cleanup without virtual dispatch.
    ~NftablesFirewall() override;

    // Buffer an nftables named set (ipv4_addr/ipv6_addr, optional timeout).
    void create_ipset(const std::string& set_name, int family,
                      uint32_t timeout = 0) override;

    // Buffer a meta mark set rule that matches the given criteria.
    void create_mark_rule(uint32_t fwmark,
                          const FirewallRuleCriteria& criteria = {}) override;
    // Buffer a drop verdict rule that matches the given criteria.
    void create_drop_rule(const FirewallRuleCriteria& criteria = {}) override;
    // Buffer a pass-through verdict rule that matches the given criteria.
    void create_pass_rule(const FirewallRuleCriteria& criteria = {}) override;

    // Return an NftBatchVisitor that appends element values to the pending
    // element buffer for set_name; elements are flushed during apply().
    std::unique_ptr<ListEntryVisitor> create_batch_loader(
        const std::string& set_name) override;

    // Atomically apply all pending table/set/rule/element operations via
    // a single 'nft -j -f -' invocation with a JSON batch.
    void apply(FirewallApplyMode mode = FirewallApplyMode::Destructive) override;
    // Delete the inet KeenPbrTable table, removing all sets and rules within it.
    void cleanup() override;
    // Returns FirewallBackend::nftables.
    FirewallBackend backend() const override;

    // Tests membership via `nft get element inet KeenPbrTable <set_name> { <ip> }`.
    std::optional<bool> test_ip_in_set(const std::string& set_name,
                                        const std::string& ip) const override;

private:
    static constexpr const char* TABLE_NAME = "KeenPbrTable";
    static constexpr const char* CHAIN_NAME = "prerouting";
    void cleanup_live_impl();
    void cleanup_impl();
    bool table_exists() const;

    struct LiveTableState {
        bool table_exists{false};
        bool chain_exists{false};
        std::set<std::string> set_names;
    };

    LiveTableState read_live_table_state() const;
    nlohmann::json build_apply_document(const LiveTableState& live_state,
                                        bool emit_full_table);

    // Describes an nftables named set to be created.
    struct PendingSet {
        std::string name;
        std::string type;   // "ipv4_addr" or "ipv6_addr"
        uint32_t timeout;   // entry TTL in seconds (0 = no timeout)
    };

    // Describes a rule to be added to the prerouting chain.
    struct PendingRule {
        int family;  // AF_INET or AF_INET6
        enum Action { Mark, Drop, Pass } action; // meta mark, drop, or accept verdict
        uint32_t fwmark; // only for Mark
        uint32_t fwmark_mask{0xFFFFFFFFu}; // only for Mark
        FirewallRuleCriteria criteria; // optional packet match criteria
    };

    // Build the nftables JSON object for creating the inet KeenPbrTable table.
    static nlohmann::json build_table_json();
    // Build the JSON object for a named set with type and optional timeout.
    static nlohmann::json build_set_json(const PendingSet& ps);
    // Build the JSON object for the prerouting chain (type filter, hook prerouting).
    static nlohmann::json build_chain_json();
    // Build the JSON object for deleting the prerouting chain.
    static nlohmann::json build_delete_chain_json();
    // Build all prerouting rule add-commands, including global prefilter rules.
    static nlohmann::json build_rule_add_commands(
        const FirewallGlobalPrefilter& prefilter,
        const std::vector<PendingRule>& rules);
    // Build the JSON rule object for a meta mark set action matching a named set.
    static nlohmann::json build_mark_rule_json(const PendingRule& pr);
    // Build the JSON rule object for a drop verdict matching a named set.
    static nlohmann::json build_drop_rule_json(const PendingRule& pr);
    // Build the JSON rule object for a pass-through verdict matching a named set.
    static nlohmann::json build_pass_rule_json(const PendingRule& pr);
    // Build nftables match expression(s) for proto/port filter.
    // Returns a (possibly empty) array of JSON match expressions.
    static nlohmann::json build_port_match_exprs(L4Proto proto,
                                                  const PortSpec& src_port,
                                                  const PortSpec& dst_port,
                                                  bool negate_src_port = false,
                                                  bool negate_dst_port = false);
    // Build nftables match expression(s) for source/destination CIDR constraints.
    // ip_proto is "ip" or "ip6". Returns a (possibly empty) array of JSON match expressions.
    static nlohmann::json build_addr_match_exprs(const std::string& ip_proto,
                                                  const std::vector<std::string>& src_addr,
                                                  const std::vector<std::string>& dst_addr,
                                                  bool negate_src_addr = false,
                                                  bool negate_dst_addr = false);
    // Build the JSON element-add object for bulk-loading elems into a named set.
    static nlohmann::json build_elements_json(const std::string& set_name,
                                              const nlohmann::json& elems);
    void append_rules_for_family(int family,
                                 PendingRule::Action action,
                                 uint32_t fwmark,
                                 const FirewallRuleCriteria& criteria);

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
