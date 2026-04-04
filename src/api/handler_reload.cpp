#ifdef WITH_API

#include "handler_reload.hpp"
#include "generated/api_types.hpp"
#include "../util/safe_exec.hpp"

#include <unistd.h>
#include <nlohmann/json.hpp>
#include <vector>

namespace keen_pbr3 {

namespace {

bool file_is_executable(const char* path) {
    return path != nullptr && access(path, X_OK) == 0;
}

int run_service_action(const std::string& action) {
    const std::vector<std::vector<std::string>> candidates = {
        {"systemctl", action, "keen-pbr"},
        {"/etc/init.d/keen-pbr", action},
        {"/opt/etc/init.d/S80keen-pbr", action},
        {"service", "keen-pbr", action},
    };

    for (const auto& command : candidates) {
        const int exit_code = safe_exec(command, true);
        if (exit_code == 0) {
            return 0;
        }
    }

    return -1;
}

int run_dnsmasq_hook(const std::string& command) {
    const char* helper_path = nullptr;
    if (file_is_executable("/usr/lib/keen-pbr/dnsmasq.sh")) {
        helper_path = "/usr/lib/keen-pbr/dnsmasq.sh";
    } else if (file_is_executable("/opt/usr/lib/keen-pbr/dnsmasq.sh")) {
        helper_path = "/opt/usr/lib/keen-pbr/dnsmasq.sh";
    }

    if (helper_path == nullptr) {
        return -1;
    }

    return safe_exec({helper_path, command}, true);
}

std::string run_service_action_with_hooks(const std::string& action) {
    if (action == "stop") {
        if (run_dnsmasq_hook("deactivate") != 0) {
            throw ApiError("Failed to run dnsmasq deactivation hook", 500);
        }
    }

    if (run_service_action(action) != 0) {
        throw ApiError("Failed to execute service action: " + action, 500);
    }

    if (action == "start" || action == "restart") {
        if (run_dnsmasq_hook("ensure-runtime-prereqs") != 0 ||
            run_dnsmasq_hook("activate") != 0) {
            throw ApiError("Service action succeeded, but dnsmasq activation hook failed", 500);
        }
    }

    api::ReloadResponse resp;
    resp.status = api::ConfigUpdateResponseStatus::OK;
    resp.message = "Service action queued: " + action;
    return nlohmann::json(resp).dump();
}

} // namespace

void register_reload_handler(ApiServer& server, ApiContext& ctx) {
    // POST /api/reload - trigger list re-download and re-apply
    server.post("/api/reload", [&ctx]() -> std::string {
        {
            std::lock_guard<std::mutex> lock(ctx.config_op_mutex);
            if (ctx.config_op_state.load(std::memory_order_acquire) != ConfigOperationState::Idle) {
                throw ApiError("Another config operation is already in progress", 409);
            }
            ctx.config_op_state.store(ConfigOperationState::Reloading, std::memory_order_release);
        }

        try {
            ctx.enqueue_reload_fn();
        } catch (...) {
            std::lock_guard<std::mutex> lock(ctx.config_op_mutex);
            ctx.config_op_state.store(ConfigOperationState::Idle, std::memory_order_release);
            ctx.config_op_cv.notify_all();
            throw;
        }

        api::ReloadResponse resp;
        resp.status = api::ConfigUpdateResponseStatus::OK;
        resp.message = "Reload queued";
        return nlohmann::json(resp).dump();
    });

    server.post("/api/service/start", [&ctx]() -> std::string {
        std::lock_guard<std::mutex> lock(ctx.config_op_mutex);
        return run_service_action_with_hooks("start");
    });

    server.post("/api/service/stop", [&ctx]() -> std::string {
        std::lock_guard<std::mutex> lock(ctx.config_op_mutex);
        return run_service_action_with_hooks("stop");
    });

    server.post("/api/service/restart", [&ctx]() -> std::string {
        std::lock_guard<std::mutex> lock(ctx.config_op_mutex);
        return run_service_action_with_hooks("restart");
    });
}

} // namespace keen_pbr3

#endif // WITH_API
