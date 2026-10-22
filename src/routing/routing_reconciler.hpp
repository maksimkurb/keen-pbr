#pragma once

#include <set>
#include <vector>

#include "netlink.hpp"

namespace keen_pbr3 {

// The policy-rule slots keen-pbr allocates from: its priority block, an exact
// fwmark.mask selector with a mark the allocator can produce, and a lookup
// into a keen-pbr table or a terminal action.  A rule inside all of them is
// keen-pbr's even when the current configuration no longer produces it, e.g.
// after a reload shifted the mark allocation.  fwmask 0 means no namespace.
struct RuleNamespace {
    uint32_t priority_first{0};
    uint32_t priority_last{0};
    uint32_t fwmask{0};
    uint32_t mark_first{0};
    uint32_t table_first{0};
    uint32_t table_last{0};
    std::vector<uint32_t> tables;  // table outbounds outside the generated range
};

bool rule_in_namespace(const DumpedRule& rule, const RuleNamespace& ns);

// Reconciles kernel state from a kernel dump.  Routes are owned by the
// generated rtm_protocol.  Rules are owned by an exact, non-zero mark/mask
// pair reserved by the active configuration or by `previous_rules` (what this
// process applied before).  Without such a record, i.e. on the first apply
// after start, the caller passes `ns` so that rules left by an earlier run
// are claimed too.  Every unwanted owned rule is removed and logged.
// Generated routes unwanted by `desired_routes` are removed from the tables
// that hold desired routes, from `previous_route_tables` (what this process
// applied before) and, when `ns` is given, from its table range and table
// outbounds, so a table whose desired set became empty is still cleaned.
// Routes of any other protocol are never touched.
class RoutingReconciler {
public:
    explicit RoutingReconciler(RoutingNetlinkOperations& netlink)
        : netlink_(netlink) {}

    void reconcile(const std::vector<RouteSpec>& desired_routes,
                   const std::vector<RuleSpec>& desired_rules,
                   const std::vector<RuleSpec>& previous_rules = {},
                   const RuleNamespace& ns = {},
                   const std::set<uint32_t>& previous_route_tables = {});

private:
    RoutingNetlinkOperations& netlink_;
};

} // namespace keen_pbr3
