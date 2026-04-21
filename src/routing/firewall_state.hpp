#pragma once

#include "../config/config.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace keen_pbr3 {

// Action type for a firewall rule
enum class RuleActionType {
    Mark,   // Packet marking with fwmark (interface/table outbound)
    Drop,   // DROP rule (blackhole outbound)
    Pass,   // Pass-through verdict that stops further keen-pbr rule processing
    Skip    // No firewall rule (e.g. unresolved outbound)
};

// Tracked state of a single applied firewall rule
struct RuleState {
    size_t rule_index;                  // Index into config.route.rules
    std::vector<std::string> list_names; // Lists this rule covers
    std::vector<std::string> set_names;  // Firewall set names created
    std::string outbound_tag;           // Resolved outbound tag
    RuleActionType action_type;
    uint32_t fwmark{0};                 // Only valid if action_type == Mark
    FirewallRuleCriteria criteria;      // Realized selector criteria for live rules
};

// In-memory state of the firewall configuration.
// Source of truth for API queries and for determining what to rebuild
// when urltest selection changes.
class FirewallState {
public:
    FirewallState() = default;

    // Replace the current rule state
    void set_rules(std::vector<RuleState> rules);

    // Update the urltest selection for a given urltest tag
    void set_urltest_selection(const std::string& urltest_tag,
                               const std::string& child_tag);

    // Get current rule state
    const std::vector<RuleState>& get_rules() const;

    // Get outbound mark assignments
    const OutboundMarkMap& get_outbound_marks() const;

    // Set outbound mark assignments
    void set_outbound_marks(OutboundMarkMap marks);

    // Get the configured fwmark mask used by mark rules and policy rules.
    uint32_t get_fwmark_mask() const;

    // Set the configured fwmark mask used by mark rules and policy rules.
    void set_fwmark_mask(uint32_t fwmark_mask);

    // Get urltest selections (urltest_tag -> selected child tag)
    const std::map<std::string, std::string>& get_urltest_selections() const;

    // Resolve the effective outbound for a rule. If the rule's outbound
    // is a urltest, returns the currently selected child tag. Otherwise
    // returns the outbound tag directly.
    std::string resolve_effective_outbound(const RuleState& rule) const;

private:
    OutboundMarkMap outbound_marks_;
    uint32_t fwmark_mask_{0xFFFFFFFFu};
    std::vector<RuleState> rules_;
    std::map<std::string, std::string> urltest_selections_;
};

} // namespace keen_pbr3
