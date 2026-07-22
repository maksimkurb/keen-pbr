#ifdef WITH_API

#include "handler_reload.hpp"
#include "generated/api_types.hpp"

#include <nlohmann/json.hpp>

#include <stdexcept>

namespace keen_pbr3 {

namespace {
std::string success_response(const std::string& message) {
    api::ReloadResponse resp;
    resp.status = api::ConfigUpdateResponseStatus::OK;
    resp.message = message;
    return nlohmann::json(resp).dump();
}

std::vector<LifecycleOperationStage> service_stages(LifecycleOperationType type) {
    if (type == LifecycleOperationType::Stop) {
        return {{"runtime", "Stop routing and firewall"},
                {"resolver", "Verify dnsmasq fallback"}};
    }
    if (type == LifecycleOperationType::Restart) {
        return {{"stop", "Stop routing and firewall"},
                {"runtime", "Start routing and firewall"},
                {"resolver", "Verify dnsmasq lifecycle"}};
    }
    return {{"runtime", "Start routing and firewall"},
            {"resolver", "Verify dnsmasq lifecycle"}};
}

std::string start_lifecycle(ApiContext& ctx, LifecycleOperationType type,
                            std::function<void()> work) {
    if (ctx.lifecycle_operations == nullptr) throw ApiError("Lifecycle coordinator is unavailable", 503);
    LifecycleOperationSnapshot operation;
    if (const auto active = ctx.lifecycle_operations->begin(type, service_stages(type), operation)) {
        throw ApiError("A lifecycle operation is already active", 409,
                       nlohmann::json{{"error", "A lifecycle operation is already active"},
                                      {"active_operation_id", *active}}.dump());
    }
    const std::string id = operation.id;
    if (!ctx.enqueue_lifecycle_task("api-lifecycle-service", [&ctx, id, type, work = std::move(work)]() {
            try {
                if (type == LifecycleOperationType::Restart) {
                    ctx.lifecycle_operations->start_stage(id, "stop");
                    // restart_runtime owns its atomic stop/start transition; this stage
                    // remains server-authored rather than UI-derived.
                    ctx.lifecycle_operations->succeed_stage(id, "stop");
                }
                ctx.lifecycle_operations->start_stage(id, "runtime");
                work();
                ctx.lifecycle_operations->succeed_stage(id, "runtime");
                ctx.lifecycle_operations->start_stage(id, "resolver");
                ctx.lifecycle_operations->succeed_stage(id, "resolver");
                ctx.lifecycle_operations->finish(id);
            } catch (const std::exception& error) {
                ctx.lifecycle_operations->fail_stage(id, "runtime", error.what());
                ctx.lifecycle_operations->finish(id, error.what());
            }
        })) {
        ctx.lifecycle_operations->finish(id, "Lifecycle executor is unavailable");
        throw ApiError("Lifecycle executor is unavailable", 503);
    }
    throw ApiAccepted(nlohmann::json{{"operation_id", id}, {"status", "accepted"}}.dump());
}

} // namespace

void register_reload_handler(ApiServer& server, ApiContext& ctx) {
    server.post("/api/service/start", [&ctx]() -> std::string {
        return start_lifecycle(ctx, LifecycleOperationType::Start, [&ctx] { ctx.start_runtime(); });
    });

    server.post("/api/service/stop", [&ctx]() -> std::string {
        return start_lifecycle(ctx, LifecycleOperationType::Stop, [&ctx] { ctx.stop_runtime(); });
    });

    server.post("/api/service/restart", [&ctx]() -> std::string {
        return start_lifecycle(ctx, LifecycleOperationType::Restart, [&ctx] { ctx.restart_runtime(); });
    });
}

} // namespace keen_pbr3

#endif // WITH_API
