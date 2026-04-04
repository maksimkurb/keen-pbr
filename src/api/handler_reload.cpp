#ifdef WITH_API

#include "handler_reload.hpp"
#include "generated/api_types.hpp"

#include <nlohmann/json.hpp>

namespace keen_pbr3 {

namespace {
std::string success_response(const std::string& message) {
    api::ReloadResponse resp;
    resp.status = api::ConfigUpdateResponseStatus::OK;
    resp.message = message;
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
        std::unique_lock<std::mutex> lock(ctx.config_op_mutex);
        if (ctx.config_op_state.load(std::memory_order_acquire) != ConfigOperationState::Idle) {
            throw ApiError("Another config operation is already in progress", 409);
        }
        if (ctx.service_running_fn()) {
            throw ApiError("Routing runtime is already started", 409);
        }
        ctx.config_op_state.store(ConfigOperationState::Reloading, std::memory_order_release);
        lock.unlock();

        try {
            ctx.enqueue_service_start_fn();
        } catch (...) {
            lock.lock();
            ctx.config_op_state.store(ConfigOperationState::Idle, std::memory_order_release);
            lock.unlock();
            ctx.config_op_cv.notify_all();
            throw;
        }

        lock.lock();
        ctx.config_op_state.store(ConfigOperationState::Idle, std::memory_order_release);
        lock.unlock();
        ctx.config_op_cv.notify_all();
        return success_response("Routing runtime started");
    });

    server.post("/api/service/stop", [&ctx]() -> std::string {
        std::unique_lock<std::mutex> lock(ctx.config_op_mutex);
        if (ctx.config_op_state.load(std::memory_order_acquire) != ConfigOperationState::Idle) {
            throw ApiError("Another config operation is already in progress", 409);
        }
        if (!ctx.service_running_fn()) {
            throw ApiError("Routing runtime is already stopped", 409);
        }
        ctx.config_op_state.store(ConfigOperationState::Reloading, std::memory_order_release);
        lock.unlock();

        try {
            ctx.enqueue_service_stop_fn();
        } catch (...) {
            lock.lock();
            ctx.config_op_state.store(ConfigOperationState::Idle, std::memory_order_release);
            lock.unlock();
            ctx.config_op_cv.notify_all();
            throw;
        }

        lock.lock();
        ctx.config_op_state.store(ConfigOperationState::Idle, std::memory_order_release);
        lock.unlock();
        ctx.config_op_cv.notify_all();
        return success_response("Routing runtime stopped");
    });

    server.post("/api/service/restart", [&ctx]() -> std::string {
        std::unique_lock<std::mutex> lock(ctx.config_op_mutex);
        if (ctx.config_op_state.load(std::memory_order_acquire) != ConfigOperationState::Idle) {
            throw ApiError("Another config operation is already in progress", 409);
        }
        if (!ctx.service_running_fn()) {
            throw ApiError("Routing runtime is stopped; start it first", 409);
        }
        ctx.config_op_state.store(ConfigOperationState::Reloading, std::memory_order_release);
        lock.unlock();

        try {
            ctx.enqueue_service_restart_fn();
        } catch (...) {
            lock.lock();
            ctx.config_op_state.store(ConfigOperationState::Idle, std::memory_order_release);
            lock.unlock();
            ctx.config_op_cv.notify_all();
            throw;
        }

        lock.lock();
        ctx.config_op_state.store(ConfigOperationState::Idle, std::memory_order_release);
        lock.unlock();
        ctx.config_op_cv.notify_all();
        return success_response("Routing runtime restarted");
    });
}

} // namespace keen_pbr3

#endif // WITH_API
