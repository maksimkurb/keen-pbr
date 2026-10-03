#pragma once

#include "../config/config.hpp"
#include "../firewall/firewall_plan.hpp"
#include "../lists/list_streamer.hpp"
#include "intercept_processor.hpp"
#include "intercept_settings.hpp"

#include <memory>

namespace keen_pbr3 {

// Builds the interception snapshot for an applied configuration: every list
// referenced by an enabled route rule whose dynamic sets were declared by
// `sets` (i.e. it has domain entries) becomes a DomainIndex list with its
// set names and TTL floor (list `ttl_ms`/1000 when >= 1000, else the
// configured intercept.min_ttl_s).  set_v6 is empty when IPv6 is disabled.
// Streams list content, so call it from a blocking executor.
std::shared_ptr<const InterceptSnapshot> build_intercept_snapshot(
    const Config& config,
    const std::vector<FirewallSetDeclaration>& sets,
    bool ipv6_enabled,
    const InterceptEffective& effective,
    ListStreamer& streamer);

} // namespace keen_pbr3
