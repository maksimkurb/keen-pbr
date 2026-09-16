#pragma once

#include "firewall_verifier.hpp"
#include "firewall_rule.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace keen_pbr3 {

struct ParsedNftBalanceTarget {
    uint32_t index{0};
    uint32_t mark{0};
    std::string setter_chain;
    std::optional<MarkAction> setter;
    std::optional<MarkAction> setter_ct;
};

// A single parsed rule from `nft -j list table inet KeenPbrTable` output.
struct ParsedNftRule {
    std::string set_name;  // Named set referenced in the match expression (without '@' prefix)
    FirewallRuleCriteria criteria;
    bool is_mark{false};   // true if rule has a mangle/meta mark action
    bool is_drop{false};   // true if rule has a drop verdict
    bool is_pass{false};   // true if rule has an accept/return verdict
    uint32_t fwmark{0};    // mark value (only valid when is_mark == true)
    bool ipv6{false};      // true if the payload protocol is ip6
    std::optional<std::string> comment; // optional native nft rule comment
    uint32_t xmark_mask{0xFFFFFFFFu};
    FirewallHook hook{FirewallHook::prerouting};
    std::string raw;
    bool is_balance{false};
    std::vector<uint32_t> balance_marks;
    std::string balance_selector_mode;
    uint32_t balance_selector_modulus{0};
    bool balance_guard_present{false};
    std::string balance_guard_op;
    uint32_t balance_guard_mask{0};
    uint32_t balance_guard_value{0};
    std::vector<ParsedNftBalanceTarget> balance_targets;
};

struct ParsedNftSet {
    std::string name;
    std::string type;
    uint32_t timeout_seconds{0};
};

// Parsed state of KeenPbrTable from `nft -j list chain ...` output, or from
// `nft -t -j list table ...` fallback output when the chain is missing.
struct ParsedNftablesState {
    bool has_table{false};              // inet KeenPbrTable table was found
    bool has_prerouting_chain{false};   // prerouting chain in KeenPbrTable was found
    bool has_prerouting_hook{false};    // chain has type=filter hook=prerouting
    bool has_output_chain{false};
    bool has_output_hook{false};
    std::vector<ParsedNftRule> rules;   // rules in the owned policy chains
    std::vector<ParsedNftSet> sets;     // named sets in KeenPbrTable
};

// Parse nft JSON from `nft -j list chain ...` or `nft -t -j list table ...`.
// Returns the parsed state of KeenPbrTable entries present in the document.
// On any JSON parse error or invalid input, returns a default (empty) state.
ParsedNftablesState parse_nft_json(const std::string& json_output);

// FirewallVerifier implementation for the nftables backend.
class NftablesFirewallVerifier : public FirewallVerifier {
public:
    explicit NftablesFirewallVerifier(CommandRunner runner);

    // Verify KeenPbrTable table/chain existence and prerouting hook.
    FirewallChainCheck verify_chain() override;

    // Verify mark/drop/pass rules for all expected RuleState entries (action_type != Skip).
    std::vector<FirewallRuleCheck> verify_rules(
        const std::vector<RuleState>& expected) override;

private:
    static constexpr const char* TABLE_NAME = "KeenPbrTable";
    static constexpr const char* CHAIN_NAME = "prerouting";

    struct CachedState {
        ParsedNftablesState state;
        std::string error;
    };

    const CachedState& get_state() const;

    CommandRunner runner_;
    mutable std::optional<CachedState> cached_state_;
};

// Factory function called from firewall_verifier.cpp
std::unique_ptr<FirewallVerifier> create_nftables_verifier(CommandRunner runner);

} // namespace keen_pbr3
