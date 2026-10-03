#include "../firewall_rule_modules.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace keen_pbr3 {

namespace {
constexpr std::string_view kModuleId = "prefilter.restore_conntrack_mark";

// iptables owns the restore rule unconditionally; nftables only needs it when
// at least one outbound has a materialized (non-zero) mark. A zero fwmark mask
// leaves nothing to restore on either backend.
bool restore_conntrack_mark_enabled(const FirewallBuildContext& context) {
  if (context.fwmark_mask == 0) {
    return false;
  }
  if (context.backend == FirewallBackend::iptables) {
    return true;
  }
  return context.outbound_marks != nullptr &&
         std::any_of(context.outbound_marks->begin(),
                     context.outbound_marks->end(),
                     [](const auto& entry) { return entry.second != 0; });
}
}

void register_restore_conntrack_mark_rules(const FirewallBuildContext& context,
                                           FirewallRuleRegistrar& registrar) {
  if (!restore_conntrack_mark_enabled(context)) {
    return;
  }
  FirewallRuleInstance rule;
  rule.key = FirewallRuleKey::compact(kModuleId, "mask=" +
                                               std::to_string(context.fwmark_mask));
  rule.stage = FirewallRuleStage::restore_conntrack;
  rule.priority = 0;
  rule.family = FirewallFamily::any;
  rule.action = RestoreConntrackMarkAction{context.fwmark_mask};
  registrar.register_rule(std::move(rule));
}

} // namespace keen_pbr3
