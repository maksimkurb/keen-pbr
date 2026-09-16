#pragma once

#include "firewall_verifier.hpp"
#include "firewall_rule.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace keen_pbr3 {

// A single parsed rule from `iptables -t mangle -S` output (within KeenPbrTable chain).
struct ParsedIptablesRule {
    std::string set_name;  // IP set name from --match-set
    FirewallRuleCriteria criteria;
    bool ipv6{false};      // true if the rule came from ip6tables
    bool is_mark{false};   // true if -j MARK --set-mark / --set-xmark
    bool is_drop{false};   // true if -j DROP
    bool is_pass{false};   // true if -j RETURN
    uint32_t fwmark{0};    // mark value (only valid when is_mark == true)
    bool mark_is_exact{true};      // false for partial-mask --set-xmark rules
    uint32_t xmark_mask{0xFFFFFFFF}; // parsed mask for --set-xmark
    std::optional<std::string> comment; // optional -m comment payload
    FirewallHook hook{FirewallHook::prerouting};
    std::string chain_name;
    std::string raw;
    bool is_restore_conntrack{false};
    bool is_skip_dnat{false};
    bool is_skip_marked{false};
    bool is_inbound_filter{false};
    bool is_return{false};
    bool is_accept{false};
    bool has_nonzero_mark_match{false};
    bool is_restore_companion{false};
    std::size_t order{0};
    std::vector<std::string> inbound_interfaces;
    uint32_t conntrack_mark_mask{0};
    bool restore_guard_present{false};
    uint32_t restore_guard_mask{0};
    bool restore_target_exact{false};
    bool mark_guard_present{false};
    uint32_t mark_guard_mask{0};
};

// Parsed state of the owned dispatch/generation chains from an `iptables -S`
// table dump.
struct ParsedIptablesState {
    bool has_keen_pbr_chain{false};        // -N KeenPbrTable line was found
    bool has_prerouting_jump{false};       // -A PREROUTING -j KeenPbrTable was found
    bool has_output_chain{false};
    bool has_output_jump{false};
    std::vector<ParsedIptablesRule> rules; // rules found in active owned chains
    std::vector<std::string> active_prerouting_chains;
    std::vector<std::string> active_output_chains;
    std::vector<std::string> output_chains;
};

struct ParsedIpset {
    std::string name;
    int family{0};
    uint32_t timeout_seconds{0};
};

// Parse the stdout of `iptables -t mangle -S <chain>` / `ip6tables -t mangle -S <chain>`.
// Returns the parsed state of the KeenPbrTable chain.
ParsedIptablesState parse_iptables_s(const std::string& output);

// Parse a complete iptables -S table dump.  Unlike parse_iptables_s(), this
// also records OUTPUT chains and the active generation jumps used by the
// snapshot inspector.  The parser and all backend-specific normalization stay
// shared with the legacy verifier.
ParsedIptablesState parse_iptables_s_family(
    const std::string& output, bool ipv6,
    const std::string& chain_name = "KeenPbrTable");

// Parse `ipset save` output for the reserved kpbr namespaces. Unknown lines
// are ignored so this remains compatible with older ipset implementations.
std::vector<ParsedIpset> parse_ipset_save(const std::string& output);

// FirewallVerifier implementation for the iptables/ip6tables backend.
class IptablesFirewallVerifier : public FirewallVerifier {
public:
    explicit IptablesFirewallVerifier(CommandRunner runner,
                                      RawPreroutingMode raw_prerouting = {});
    explicit IptablesFirewallVerifier(CommandRunner runner,
                                      bool use_raw_prerouting)
        : IptablesFirewallVerifier(
              std::move(runner),
              RawPreroutingMode{use_raw_prerouting, false}) {}

    // Verify the configured KeenPbrTable/KeenPbrRaw chains and PREROUTING
    // hooks for both families.
    FirewallChainCheck verify_chain() override;

    // Verify mark/drop/pass rules for all expected RuleState entries (action_type != Skip).
    std::vector<FirewallRuleCheck> verify_rules(
        const std::vector<RuleState>& expected) override;

private:
    static constexpr const char* CHAIN_NAME = "KeenPbrTable";

    struct CachedState {
        ParsedIptablesState v4;
        ParsedIptablesState v6;
    };

    const CachedState& get_state() const;

    CommandRunner runner_;
    RawPreroutingMode raw_prerouting_{};
    mutable std::optional<CachedState> cached_state_;
};

// Factory function called from firewall_verifier.cpp
std::unique_ptr<FirewallVerifier> create_iptables_verifier(CommandRunner runner,
                                                            RawPreroutingMode raw_prerouting = {});

inline std::unique_ptr<FirewallVerifier>
create_iptables_verifier(CommandRunner runner, bool use_raw_prerouting) {
    return create_iptables_verifier(std::move(runner),
                                    RawPreroutingMode{use_raw_prerouting, false});
}

} // namespace keen_pbr3
