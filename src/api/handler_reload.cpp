#ifdef WITH_API

#include "handler_reload.hpp"
#include "generated/api_types.hpp"

#include <nlohmann/json.hpp>

namespace keen_pbr3 {

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
}

} // namespace keen_pbr3

#endif // WITH_API
