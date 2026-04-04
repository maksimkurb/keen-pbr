#pragma once

#ifdef WITH_API

#include "../cache/cache_manager.hpp"
#include "../config/config.hpp"
#include "../health/routing_health.hpp"
#include "../routing/urltest_manager.hpp"
#include "sse_broadcaster.hpp"
#include "server.hpp"

#include <functional>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <utility>
#include <vector>

namespace keen_pbr3 {

enum class ConfigOperationState : uint8_t {
    Idle = 0,
    Saving,
    Reloading,
};

struct ConfigApplyResult {
    bool applied{false};
    bool rolled_back{false};
    std::string error;
};

// Context struct holding thread-safe accessors to daemon runtime state.
struct ApiContext {
    const std::string& config_path;
    const CacheManager& cache_manager;
    std::shared_mutex& state_mutex;
    SseBroadcaster& dns_test_broadcaster;

    std::function<Config()> visible_config_fn;
    std::function<bool()> config_is_draft_fn;
    std::function<void(Config, std::string)> stage_config_fn;
    std::function<std::optional<std::pair<Config, std::string>>()> staged_config_snapshot_fn;
    std::function<void()> clear_staged_config_fn;
    std::function<void(const Config&)> dry_run_apply_check_fn;
    std::function<RoutingHealthReport()> routing_health_check_fn;
    std::function<api::RuntimeOutboundsResponse()> runtime_outbounds_fn;
    std::function<std::string()> resolver_config_hash_fn;
    std::function<std::string()> resolver_config_hash_actual_fn;

    // Global serialization for config operations.
    std::mutex& config_op_mutex;
    std::condition_variable& config_op_cv;
    std::atomic<ConfigOperationState>& config_op_state;

    // Callbacks that mutate daemon runtime state from event loop.
    std::function<void()> enqueue_reload_fn;
    std::function<ConfigApplyResult(Config, std::string)> enqueue_apply_validated_config_fn;

    Config visible_config() const {
        return visible_config_fn();
    }

    bool config_is_draft() const {
        return config_is_draft_fn();
    }
};

// Register all API endpoint handlers on the given ApiServer.
//   GET  /api/health/service  - daemon version/status + resolver/config summary
//   POST /api/reload          - trigger list re-download and re-apply
//   POST /api/service/start   - start service and activate dnsmasq hook
//   POST /api/service/stop    - stop service and deactivate dnsmasq hook
//   POST /api/service/restart - restart service and activate dnsmasq hook
//   GET  /api/config          - return current config and draft status
//   POST /api/config          - validate + stage config in memory
//   POST /api/config/save     - persist staged config and reload
//   GET  /api/health/routing  - routing and firewall health verification
//   GET  /api/runtime/outbounds - live outbound/interface runtime state
//   POST /api/routing/test    - test expected/actual routing for an IP or domain
void register_api_handlers(ApiServer& server, ApiContext& ctx);

} // namespace keen_pbr3

#endif // WITH_API
