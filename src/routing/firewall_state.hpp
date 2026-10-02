#pragma once

#include "../config/config.hpp"
#include "../firewall/firewall_plan.hpp"

#include <cstdint>
#include <map>
#include <memory>
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

// One successful firewall apply, published as a unit: the desired plan, what
// the backend realized, and the rule-state API projection. Immutable once
// built, so readers holding a shared_ptr never see a mix of two applies.
struct ActiveFirewall {
    FirewallPlan plan;
    FirewallApplyResult result;
    std::vector<RuleState> rule_states;
};

// In-memory firewall runtime state. The active firewall is canonical; its
// rule_states are the compatibility/API projection. URLTEST selections affect
// routing state, while firewall rules retain the URLTEST outbound's stable
// mark. Copying this object shares the immutable ActiveFirewall (no deep copy);
// RuntimeStateStore's mutex guards the copy that is published to readers.
class FirewallState {
public:
    FirewallState() = default;

    // Publish a successfully applied firewall as one pointer swap.
    void publish_active_firewall(ActiveFirewall active);

    // Snapshot of the last firewall whose backend apply completed
    // successfully; null when none is active. Take it once per operation.
    std::shared_ptr<const ActiveFirewall> active_firewall() const;

    // Drop the active firewall after the corresponding kernel firewall
    // cleanup has completed successfully.
    void clear_active_firewall();

    // Update the urltest selection for a given urltest tag
    void set_urltest_selection(const std::string& urltest_tag,
                               const std::string& child_tag);

    // Rule states of the active firewall (empty when none is active). Prefer
    // active_firewall() when the plan or result is needed as well.
    const std::vector<RuleState>& get_rules() const;

    // Get outbound mark assignments
    const OutboundMarkMap& get_outbound_marks() const;

    // Set outbound mark assignments
    void set_outbound_marks(OutboundMarkMap marks);

    // Get urltest selections (urltest_tag -> selected child tag)
    const std::map<std::string, std::string>& get_urltest_selections() const;

    // Resolve the effective outbound for a rule. If the rule's outbound
    // is a urltest, returns the currently selected child tag. Otherwise
    // returns the outbound tag directly.
    std::string resolve_effective_outbound(const RuleState& rule) const;

private:
    OutboundMarkMap outbound_marks_;
    std::shared_ptr<const ActiveFirewall> active_;
    std::map<std::string, std::string> urltest_selections_;
};

} // namespace keen_pbr3
