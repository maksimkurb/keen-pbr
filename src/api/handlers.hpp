#pragma once

#ifdef WITH_API

#include "../config/config.hpp"
#include "../health/health_checker.hpp"
#include "../lists/list_manager.hpp"
#include "server.hpp"

#include <string>
#include <vector>

namespace keen_pbr3 {

// Context struct holding references to subsystems needed by API handlers.
struct ApiContext {
    const std::vector<Outbound>& outbounds;
    const ListManager& list_manager;
    const HealthChecker& health_checker;

    // Callback to trigger list reload (non-const, performs side effects)
    std::function<void()> reload_fn;
};

// Register all API endpoint handlers on the given ApiServer.
//   GET  /api/status  - daemon status, loaded lists, active outbounds
//   POST /api/reload  - trigger list re-download and re-apply
//   GET  /api/health  - health check results for all outbounds
void register_api_handlers(ApiServer& server, ApiContext& ctx);

} // namespace keen_pbr3

#endif // WITH_API
