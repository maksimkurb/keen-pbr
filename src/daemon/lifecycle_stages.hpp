#pragma once

#include "../runtime/lifecycle_operation.hpp"

#include <vector>

namespace keen_pbr3 {

// Routing/firewall stages of a lifecycle operation.
std::vector<LifecycleOperationStage> lifecycle_stages(LifecycleOperationType type);

} // namespace keen_pbr3
