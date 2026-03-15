#pragma once

#ifdef WITH_API

#include "../cache/cache_manager.hpp"
#include "../config/config.hpp"
#include "../health/routing_health_checker.hpp"
#include "../routing/firewall_state.hpp"
#include "../routing/urltest_manager.hpp"
#include "sse_broadcaster.hpp"
#include "server.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace keen_pbr3 {

// Context struct holding references to subsystems needed by API handlers.
struct ApiContext {
    const std::string& config_path;
    Config& config;
    std::optional<Config>& staged_config;
    std::optional<std::string>& staged_config_json;
    const CacheManager& cache_manager;
    const FirewallState& firewall_state;
    const std::unique_ptr<UrltestManager>& urltest_manager;
    RoutingHealthChecker& routing_health_checker;
    const std::string& resolver_config_hash;
    SseBroadcaster& dns_test_broadcaster;

    // Callback to trigger full reload (non-const, performs side effects)
    std::function<void()> reload_fn;
    std::function<void(const Config&)> apply_config_fn;

    const Config& visible_config() const {
        return staged_config.has_value() ? *staged_config : config;
    }

    bool config_is_draft() const {
        return staged_config.has_value();
    }

    // Convenience accessors
    const std::vector<Outbound>& outbounds() const {
        static const std::vector<Outbound> empty{};
        return config.outbounds ? *config.outbounds : empty;
    }
};

// Register all API endpoint handlers on the given ApiServer.
//   GET  /api/health/service  - daemon version/status + health for all outbounds
//   POST /api/reload          - trigger list re-download and re-apply
//   GET  /api/config          - return current config and draft status
//   POST /api/config          - validate + stage config in memory
//   POST /api/config/save     - persist staged config and reload
//   GET  /api/health/routing  - routing and firewall health verification
//   POST /api/routing/test    - test expected/actual routing for an IP or domain
void register_api_handlers(ApiServer& server, ApiContext& ctx);

} // namespace keen_pbr3

#endif // WITH_API
