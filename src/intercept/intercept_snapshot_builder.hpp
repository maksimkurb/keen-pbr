#pragma once

#include "../config/config.hpp"
#include "../firewall/firewall_plan.hpp"
#include "../lists/list_streamer.hpp"
#include "intercept_processor.hpp"
#include "intercept_settings.hpp"

#include <memory>
#include <string>
#include <vector>

namespace keen_pbr3 {

// The cheap half of a snapshot: for every list that the daemon fills, which
// dynamic sets it writes and with which TTL floor.  Derived from the config and
// the declared sets only (no list content is read), so it can be rebuilt
// synchronously inside the firewall-apply write pause.  `signature` identifies
// the list definition the DomainIndex content was built from.
struct InterceptListBinding {
    std::string name;
    InterceptListTarget target;
    std::string signature;
};

std::vector<InterceptListBinding> build_intercept_bindings(
    const Config& config,
    const std::vector<FirewallSetDeclaration>& sets,
    bool ipv6_enabled,
    const InterceptEffective& effective);

// Republishes `previous` for a new configuration without rebuilding the
// DomainIndex.  Every indexed list gets the binding of the NEW configuration
// when a binding with the same name and the same list signature exists; any
// other list (removed from the config, no longer filled by the daemon, or
// redefined so the index content is stale) gets an empty target and therefore
// matches nothing until the next full build.  Settings come from `effective`.
// Returns null when `previous` is null or carries no indexed list.
std::shared_ptr<const InterceptSnapshot> rebind_intercept_snapshot(
    const InterceptSnapshot* previous,
    const std::vector<InterceptListBinding>& bindings,
    const InterceptEffective& effective,
    bool ipv6_enabled);

// Builds the interception snapshot for an applied configuration: every list
// referenced by an enabled route rule whose dynamic sets were declared by
// `sets` (i.e. it has domain entries) becomes a DomainIndex list with its
// set names and TTL floor (list `ttl_ms`/1000 when >= 1000, else the
// configured intercept.min_ttl_ms converted to whole seconds).  set_v6 is
// empty when IPv6 is disabled.
// Streams list content, so call it from a blocking executor.
std::shared_ptr<const InterceptSnapshot> build_intercept_snapshot(
    const Config& config,
    const std::vector<FirewallSetDeclaration>& sets,
    bool ipv6_enabled,
    const InterceptEffective& effective,
    ListStreamer& streamer);

} // namespace keen_pbr3
