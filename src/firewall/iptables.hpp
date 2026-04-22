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
    // Initialize the iptables backend; does not modify firewall state yet.
    IptablesFirewall();
    // Destructor performs best-effort cleanup without virtual dispatch.
    ~IptablesFirewall() override;

    // Buffer an ipset create command (hash:net family, optional timeout).
    void create_ipset(const std::string& set_name, int family,
                      uint32_t timeout = 0) override;

    // Buffer an iptables/ip6tables -j MARK --set-mark rule for the given ipset.
    void create_mark_rule(uint32_t fwmark,
                          const FirewallRuleCriteria& criteria = {}) override;
    // Buffer an iptables/ip6tables -j DROP rule for the given criteria.
    void create_drop_rule(const FirewallRuleCriteria& criteria = {}) override;
    // Buffer an iptables/ip6tables -j RETURN rule for the given criteria.
    void create_pass_rule(const FirewallRuleCriteria& criteria = {}) override;

    // Return an IpsetRestoreVisitor that appends 'add' lines to the pending
    // element buffer for set_name; entries are flushed during apply().
    std::unique_ptr<ListEntryVisitor> create_batch_loader(
        const std::string& set_name) override;

    // Atomically apply all pending ipsets (via ipset restore) and rules
    // (via iptables-restore / ip6tables-restore), always materializing the
    // KeenPbrTable chain scaffold and PREROUTING jump for diagnostics.
    void apply(FirewallApplyMode mode = FirewallApplyMode::Destructive) override;
    // Destroy all buffered ipsets (ipset destroy) and flush/delete the
    // KeenPbrTable chain from both iptables and ip6tables mangle tables.
    void cleanup() override;
    // Returns FirewallBackend::iptables.
    FirewallBackend backend() const override;

private:
    static constexpr const char* CHAIN_NAME = "KeenPbrTable";
    void cleanup_live_impl();
    void cleanup_impl();
    void cleanup_rules_impl();

    // Describes a set to be created via 'ipset restore'.
    struct PendingSet {
        std::string name;
        std::string family_str; // "inet" or "inet6"
        uint32_t timeout;       // entry TTL in seconds (0 = no timeout)
    };

    // Describes an iptables/ip6tables rule to be added to KeenPbrTable.
    struct PendingRule {
        std::string set_name; // ipset name to match with --match-set
        bool ipv6;            // true → ip6tables, false → iptables
        enum Action { Mark, Drop, Pass } action; // MARK, DROP, or RETURN target
        uint32_t fwmark; // only for Mark
        uint32_t fwmark_mask{0xFFFFFFFFu}; // only for Mark
        FirewallRuleCriteria criteria; // optional packet match criteria
    };

    // Build the 'create <name> hash:net family <f> [timeout <t>]' line.
    static std::string build_ipset_create_line(const PendingSet& ps);
    // Build a complete iptables-restore script for the given protocol and rules.
    static std::string build_ipt_script(bool ipv6,
                                        const std::vector<PendingRule>& rules,
                                        const FirewallGlobalPrefilter& prefilter = {});
    // Build early RETURN lines for the global prefilter.
    static std::string build_prefilter_lines(
        const FirewallGlobalPrefilter& prefilter);
    // Build the proto/port fragment for a single rule (single proto, not tcp/udp).
    static std::string build_proto_port_fragment(L4Proto proto,
                                                 const PortSpec& src_port,
                                                 const PortSpec& dst_port,
                                                 bool negate_src_port = false,
                                                 bool negate_dst_port = false);
    // Build one or more iptables-restore lines for a queued rule.
    static std::vector<std::string> build_rule_lines(
        const PendingRule& pr,
        const FirewallGlobalPrefilter& prefilter);
    // Expand filter (proto, src_addr, dst_addr) into cross-product of PendingRules
    // and append them to out.  tcp/udp is split into two entries.  Multiple CIDRs
    // in src_addr / dst_addr each become separate rules (OR semantics when combined).
    void append_rules_for_family(bool ipv6,
                                 PendingRule::Action action,
                                 uint32_t fwmark,
                                 const FirewallRuleCriteria& criteria);

    // Sets queued for creation, flushed by apply().
    std::vector<PendingSet> pending_sets_;
    // Per-set element buffers for ipset restore lines, keyed by set name.
    std::map<std::string, std::ostringstream> pending_elements_;
    // Rules queued for insertion into KeenPbrTable, flushed by apply().
    std::vector<PendingRule> pending_rules_;

    // Track created ipsets: set_name -> family (AF_INET/AF_INET6)
    std::map<std::string, int> created_sets_;

    // Track whether chain + jump rule exist for each protocol
    bool chain_v4_created_ = false;
    bool chain_v6_created_ = false;

#ifdef KEEN_PBR3_TESTING
    friend class IptablesBuilderTest;
    // Allow test access to build_proto_port_fragment
    friend struct IptablesBuilderTestHelper;
#endif
};

// Factory function called from firewall.cpp
std::unique_ptr<Firewall> create_iptables_firewall();

} // namespace keen_pbr3
