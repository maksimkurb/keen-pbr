#include "firewall_state.hpp"

#include <utility>

namespace keen_pbr3 {

void FirewallState::publish_active_firewall(ActiveFirewall active) {
    active_ = std::make_shared<const ActiveFirewall>(std::move(active));
}

std::shared_ptr<const ActiveFirewall> FirewallState::active_firewall() const {
    return active_;
}

void FirewallState::clear_active_firewall() {
    active_.reset();
}

void FirewallState::set_urltest_selection(const std::string& urltest_tag,
                                           const std::string& child_tag) {
    urltest_selections_[urltest_tag] = child_tag;
}

const std::vector<RuleState>& FirewallState::get_rules() const {
    static const std::vector<RuleState> empty;
    return active_ ? active_->rule_states : empty;
}

const OutboundMarkMap& FirewallState::get_outbound_marks() const {
    return outbound_marks_;
}

void FirewallState::set_outbound_marks(OutboundMarkMap marks) {
    outbound_marks_ = std::move(marks);
}

const std::map<std::string, std::string>& FirewallState::get_urltest_selections() const {
    return urltest_selections_;
}

std::string FirewallState::resolve_effective_outbound(const RuleState& rule) const {
    // Check if the rule's outbound is a urltest with a selected child
    auto it = urltest_selections_.find(rule.outbound_tag);
    if (it != urltest_selections_.end()) {
        return it->second;
    }
    return rule.outbound_tag;
}

} // namespace keen_pbr3
