#pragma once

#include "../cache/cache_manager.hpp"
#include "../config/config.hpp"
#include "../lists/list_entry_visitor.hpp"
#include "../lists/list_set_usage.hpp"
#include "../routing/firewall_state.hpp"
#include "../routing/netlink.hpp"
#include "firewall.hpp"
#include "firewall_plan.hpp"

#include <cstdint>
#include <cstddef>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace keen_pbr3 {

struct FirewallConfigApplyPolicy {
  FirewallApplyMode mode{FirewallApplyMode::PreserveSets};
  bool force_clear_dynamic_sets{false};
};

// Select the apply mode required when a candidate configuration replaces the
// currently active runtime configuration. iptables cannot change an existing
// ipset's maxelem in place, so capacity changes require recreating owned sets.
FirewallConfigApplyPolicy firewall_config_apply_policy(
    FirewallBackend backend, const Config &current, const Config &candidate);

// Routes each Ip/Cidr entry to the loader of its address family (either may be
// null when the plan declares no set for that family). Domains are ignored.
// An entry whose family cannot be established is skipped with a warning
// instead of being handed to the IPv4 loader. Loaders are owned by the caller.
class IpFamilySplitVisitor final : public ListEntryVisitor {
public:
  IpFamilySplitVisitor(ListEntryVisitor* ipv4, ListEntryVisitor* ipv6,
                       std::string list_name)
      : ipv4_(ipv4), ipv6_(ipv6), list_name_(std::move(list_name)) {}

  void on_entry(EntryType type, std::string_view entry) override;

  std::size_t invalid_entries() const { return invalid_entries_; }

private:
  ListEntryVisitor* ipv4_;
  ListEntryVisitor* ipv6_;
  std::string list_name_;
  std::size_t invalid_entries_{0};
};

// Inputs prepared without mutating a Firewall backend. List usage is supplied
// by the caller because list analysis/streaming is an apply-mode concern.
struct FirewallPlanBuildInputs {
  const Config& config;
  const OutboundMarkMap& outbound_marks;
  const std::map<std::string, ListSetUsage>& list_usage;
  const std::vector<DumpedRoute>& main_routes;
  const std::vector<DumpedInterface>& interfaces;
  const FirewallBalanceCandidates* balance_candidates{nullptr};
  bool ipv6_enabled{true};
  uint32_t fwmark_mask{0xFFFFFFFFu};
  FirewallBackend backend{FirewallBackend::iptables};
};

// Build the canonical desired firewall state. This function has no backend
// side effects and is safe to call before Firewall::prepare_apply().
FirewallPlan build_firewall_plan(const FirewallPlanBuildInputs& inputs);

// Materialize the runtime firewall configuration using the real backend.
// Returns the plan, backend apply result and rule-state projection of the
// successful apply as one object for the caller to publish. Throws without
// returning anything when preparation or apply fails.
// previous_active is the last published ActiveFirewall (or null); it supplies
// list usage and the physical set names for the RulesOnly preflight.
ActiveFirewall apply_runtime_firewall(
    const Config& config,
    const OutboundMarkMap& outbound_marks,
    const CacheManager& cache_manager,
    Firewall& firewall,
    FirewallApplyMode mode = FirewallApplyMode::Destructive,
    const ActiveFirewall* previous_active = nullptr,
    bool force_clear_dynamic_sets = false,
    const std::vector<DumpedRoute>& main_routes = {},
    const std::vector<DumpedInterface>& interfaces = {},
    const FirewallBalanceCandidates* balance_candidates = nullptr);

} // namespace keen_pbr3
