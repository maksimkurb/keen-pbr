#pragma once

#include "firewall_verifier.hpp"

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
};

// Parsed state of the KeenPbrTable chain from `iptables -t mangle -S` output.
struct ParsedIptablesState {
    bool has_keen_pbr_chain{false};        // -N KeenPbrTable line was found
    bool has_prerouting_jump{false};       // -A PREROUTING -j KeenPbrTable was found
    std::vector<ParsedIptablesRule> rules; // rules found in KeenPbrTable chain
};

// Parse the stdout of `iptables -t mangle -S <chain>` / `ip6tables -t mangle -S <chain>`.
// Returns the parsed state of the KeenPbrTable chain.
ParsedIptablesState parse_iptables_s(const std::string& output);

// FirewallVerifier implementation for the iptables/ip6tables backend.
class IptablesFirewallVerifier : public FirewallVerifier {
public:
    explicit IptablesFirewallVerifier(CommandRunner runner);

    // Verify KeenPbrTable chain existence and PREROUTING hook for both v4 and v6.
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
    mutable std::optional<CachedState> cached_state_;
};

// Factory function called from firewall_verifier.cpp
std::unique_ptr<FirewallVerifier> create_iptables_verifier(CommandRunner runner);

} // namespace keen_pbr3
