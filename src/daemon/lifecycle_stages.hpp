#pragma once

#include "../runtime/lifecycle_operation.hpp"

#include <vector>

namespace keen_pbr3 {

// Stages of a lifecycle operation.  The resolver stages (reload_dnsmasq,
// verify_dnsmasq, reload_fallback) exist only when a resolver integration is
// involved; without it an operation consists of its routing/firewall stages.
std::vector<LifecycleOperationStage> lifecycle_stages(LifecycleOperationType type,
                                                      bool resolver_integration);

} // namespace keen_pbr3
