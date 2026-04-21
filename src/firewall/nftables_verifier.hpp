#pragma once

#include "firewall_verifier.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace keen_pbr3 {

// A single parsed rule from nft -j list table inet KeenPbrTable output (within KeenPbrTable/prerouting chain).
struct ParsedNftRule {
    std::string set_name;  // Named set referenced in the match expression (without '@' prefix)
    FirewallRuleCriteria criteria;
    bool is_mark{false};   // true if rule has a mangle/meta mark action
    bool is_drop{false};   // true if rule has a drop verdict
    bool is_pass{false};   // true if rule has an accept/return verdict
    uint32_t fwmark{0};    // mark value (only valid when is_mark == true)
    bool ipv6{false};      // true if the payload protocol is ip6
};

// Parsed state of the KeenPbrTable from nft -j list table inet KeenPbrTable output.
struct ParsedNftablesState {
    bool has_table{false};              // inet KeenPbrTable table was found
    bool has_prerouting_chain{false};   // prerouting chain in KeenPbrTable was found
    bool has_prerouting_hook{false};    // chain has type=filter hook=prerouting
    std::vector<ParsedNftRule> rules;   // rules in the prerouting chain
};

// Parse nft JSON from `nft -j list chain ...` or `nft -j list table ...`.
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
