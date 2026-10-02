#pragma once

#include "firewall.hpp"
#include "firewall_physical.hpp"
#include "firewall_rule.hpp"

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

    void prepare_apply(FirewallApplyMode mode) override;

    // Buffer an nftables named set (ipv4_addr/ipv6_addr, optional timeout).
    void create_ipset(const std::string& set_name, int family,
                      uint32_t timeout = 0) override;

    void set_owned_marks(const std::vector<uint32_t>& marks) override;

    // Return an NftBatchVisitor that appends element values to the pending
    // element buffer for set_name; elements are flushed during apply().
    std::unique_ptr<ListEntryVisitor> create_batch_loader(
        const std::string& set_name) override;

    // Atomically apply all pending table/set/rule/element operations via
    // a single 'nft -j -f -' invocation with a JSON batch.
    void apply(const FirewallPlan& plan,
               FirewallApplyMode mode = FirewallApplyMode::Destructive) override;
    // Delete the inet KeenPbrTable table, removing all sets and rules within it.
    void cleanup() override;
    // Returns FirewallBackend::nftables.
    FirewallBackend backend() const override;

private:
    static constexpr const char* TABLE_NAME = "KeenPbrTable";
    static constexpr const char* CHAIN_NAME = "prerouting";
    static constexpr const char* OUTPUT_CHAIN_NAME = "output";
    void cleanup_live_impl();
    void cleanup_impl();
    bool table_exists() const;

    struct LiveTableState {
        bool table_exists{false};
        bool chain_exists{false};
        bool output_chain_exists{false};
        std::set<uint32_t> setter_chain_marks;
        std::set<std::string> set_names;
        std::map<std::string, std::string> set_schemas;
    };

    LiveTableState read_live_table_state() const;
    nlohmann::json build_apply_document(const LiveTableState& live_state,
                                        bool emit_full_table,
                                        bool static_sets_only = false,
                                        bool clear_dynamic_sets = false,
                                        bool rules_only = false);

    // Describes an nftables named set to be created.
    struct PendingSet {
        std::string name;
        std::string type;   // "ipv4_addr" or "ipv6_addr"
        uint32_t timeout;   // entry TTL in seconds (0 = no timeout)
    };

    void compile_plan(const FirewallPlan& plan, FirewallApplyMode mode);
    void apply_prepared(FirewallApplyMode mode);
    void clear_pending();

    // Build the nftables JSON object for creating the inet KeenPbrTable table.
    static nlohmann::json build_table_json();
    // Build the JSON object for a named set with type and optional timeout.
    static nlohmann::json build_set_json(const PendingSet& ps);
    // Build the JSON object for the prerouting chain (type filter, hook prerouting).
    static nlohmann::json build_chain_json();
    static nlohmann::json build_output_chain_json();
    // Build the JSON object for deleting the prerouting chain.
    static nlohmann::json build_delete_chain_json();
    static nlohmann::json build_delete_output_chain_json();
    static nlohmann::json build_setter_chain_json(uint32_t fwmark);
    static nlohmann::json build_delete_setter_chain_json(uint32_t fwmark);
    static nlohmann::json build_flush_set_json(const std::string& set_name);
    static nlohmann::json build_delete_set_json(const std::string& set_name);
    static bool is_dynamic_set_name(const std::string& set_name);
    static std::string set_schema_key(const PendingSet& set);
    void preflight_reused_set_schemas(const LiveTableState& live_state) const;
    // Build the JSON element-add object for bulk-loading elems into a named set.
    static nlohmann::json build_elements_json(const std::string& set_name,
                                              const nlohmann::json& elems);
    // Sets queued for creation, flushed by apply().
    std::vector<PendingSet> pending_sets_;
    // Per-set element buffers (JSON arrays) for batch element loading, keyed by set name.
    std::map<std::string, nlohmann::json> pending_elements_;
    // Lowered rules of the owned chains (prerouting, output, setters), flushed
    // by apply().
    PhysicalRuleset pending_ruleset_;
    std::set<uint32_t> owned_marks_;

    // Track created sets for family lookup: set_name -> family (AF_INET/AF_INET6)
    std::map<std::string, int> created_sets_;

    // True once the inet KeenPbrTable table has been created via apply().
    bool table_created_ = false;
    FirewallApplyMode prepared_mode_{FirewallApplyMode::Destructive};
    bool apply_prepared_{false};

#ifdef KEEN_PBR3_TESTING
    friend class NftablesBuilderTest;
#endif
};

// Renders one canonical rule as an `add rule` command of the nft JSON API for
// `chain`.  Pure: all policy was decided by the lowering.  Throws FirewallError
// for rules nft cannot express.
nlohmann::json render_nft_rule(const PhysicalChainId& chain,
                               const PhysicalRule& rule);

// Factory function called from firewall.cpp
std::unique_ptr<Firewall> create_nftables_firewall();

} // namespace keen_pbr3
