#pragma once

#ifdef WITH_API

#include "../cmd/test_routing.hpp"
#include "../config/config.hpp"
#include "../health/routing_health.hpp"
#include "sse_broadcaster.hpp"
#include "server.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
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

struct ServiceHealthState {
    api::HealthResponseStatus status{api::HealthResponseStatus::STOPPED};
    std::string resolver_config_hash;
    std::string resolver_config_hash_actual;
    bool config_is_draft{false};
};

// Context struct holding thread-safe accessors to daemon runtime state.
struct ApiContext {
    const std::string& config_path;
    SseBroadcaster& dns_test_broadcaster;

    std::function<Config()> get_visible_config_fn;
    std::function<bool()> config_is_draft_fn;
    std::function<void(Config, std::string)> stage_config_fn;
    std::function<std::optional<std::pair<Config, std::string>>()> get_staged_config_snapshot_fn;
    std::function<void()> clear_staged_config_fn;
    std::function<void(const Config&)> validate_candidate_config_fn;
    std::function<ServiceHealthState()> get_service_health_fn;
    std::function<RoutingHealthReport()> get_routing_health_fn;
    std::function<api::RuntimeOutboundsResponse()> get_runtime_outbounds_fn;
    std::function<TestRoutingResult(const std::string&)> compute_test_routing_fn;

    std::function<void()> begin_save_operation_fn;
    std::function<void()> finish_config_operation_fn;

    // Callbacks that mutate daemon runtime state from event loop.
    std::function<ConfigApplyResult(Config, std::string)> enqueue_apply_validated_config_fn;
    std::function<void()> start_runtime_fn;
    std::function<void()> stop_runtime_fn;
    std::function<void()> restart_runtime_fn;

    Config get_visible_config() const {
        return get_visible_config_fn();
    }

    bool config_is_draft() const {
        return config_is_draft_fn();
    }

    std::optional<std::pair<Config, std::string>> get_staged_config_snapshot() const {
        return get_staged_config_snapshot_fn();
    }

    void clear_staged_config() const {
        clear_staged_config_fn();
    }

    void stage_config(Config config, std::string staged_config_json) const {
        stage_config_fn(std::move(config), std::move(staged_config_json));
    }

    void validate_candidate_config(const Config& config) const {
        validate_candidate_config_fn(config);
    }

    ServiceHealthState get_service_health() const {
        return get_service_health_fn();
    }

    RoutingHealthReport get_routing_health() const {
        return get_routing_health_fn();
    }

    api::RuntimeOutboundsResponse get_runtime_outbounds() const {
        return get_runtime_outbounds_fn();
    }

    TestRoutingResult compute_test_routing(const std::string& target) const {
        return compute_test_routing_fn(target);
    }

    void begin_save_operation() const {
        begin_save_operation_fn();
    }

    void finish_config_operation() const {
        finish_config_operation_fn();
    }

    ConfigApplyResult enqueue_apply_validated_config(
        Config config,
        std::string saved_config_json) const {
        return enqueue_apply_validated_config_fn(
            std::move(config),
            std::move(saved_config_json));
    }

    void start_runtime() const {
        start_runtime_fn();
    }

    void stop_runtime() const {
        stop_runtime_fn();
    }

    void restart_runtime() const {
        restart_runtime_fn();
    }
};

// Register all API endpoint handlers on the given ApiServer.
//   GET  /api/health/service  - daemon version/status + resolver/config summary
//   POST /api/service/start   - start routing runtime and activate dnsmasq hook
//   POST /api/service/stop    - stop routing runtime and deactivate dnsmasq hook
//   POST /api/service/restart - restart routing runtime and activate dnsmasq hook
//   GET  /api/config          - return current config and draft status
//   POST /api/config          - validate + stage config in memory
//   POST /api/config/save     - persist staged config and apply it
//   GET  /api/health/routing  - routing and firewall health verification
//   GET  /api/runtime/outbounds - live outbound/interface runtime state
//   POST /api/routing/test    - test expected/actual routing for an IP or domain
void register_api_handlers(ApiServer& server, ApiContext& ctx);

} // namespace keen_pbr3

#endif // WITH_API
