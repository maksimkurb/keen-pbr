#pragma once

#ifdef WITH_API

#include "../cache/cache_manager.hpp"
#include "../config/config.hpp"
#include "../health/routing_health_checker.hpp"
#include "../routing/firewall_state.hpp"
#include "../routing/urltest_manager.hpp"
#include "server.hpp"

#include <map>
#include <string>
#include <vector>

namespace keen_pbr3 {

// Context struct holding references to subsystems needed by API handlers.
struct ApiContext {
    const std::string& config_path;
    const Config& config;
    const CacheManager& cache_manager;
    const FirewallState& firewall_state;
    const std::unique_ptr<UrltestManager>& urltest_manager;
    RoutingHealthChecker& routing_health_checker;
    const std::string& resolver_config_hash;

    // Callback to trigger full reload (non-const, performs side effects)
    std::function<void()> reload_fn;

    // Convenience accessors
    const std::vector<Outbound>& outbounds() const {
        static const std::vector<Outbound> empty{};
        return config.outbounds ? *config.outbounds : empty;
    }
};

// Register all API endpoint handlers on the given ApiServer.
//   GET  /api/health/service  - daemon version/status + health for all outbounds
//   POST /api/reload          - trigger list re-download and re-apply
//   GET  /api/config          - return raw config file content
//   POST /api/config          - validate + write config file + reload
//   GET  /api/health/routing  - routing and firewall health verification
void register_api_handlers(ApiServer& server, ApiContext& ctx);

} // namespace keen_pbr3

#endif // WITH_API
