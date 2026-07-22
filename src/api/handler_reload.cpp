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

std::string start_lifecycle(ApiContext& ctx, LifecycleOperationType type) {
    const std::string id = ctx.submit_lifecycle_operation(LifecycleRequest{.type = type});
    throw ApiAccepted(nlohmann::json{{"operation_id", id}, {"status", "accepted"}}.dump());
}

} // namespace

void register_reload_handler(ApiServer& server, ApiContext& ctx) {
    server.post("/api/service/start", [&ctx]() -> std::string {
        return start_lifecycle(ctx, LifecycleOperationType::Start);
    });

    server.post("/api/service/stop", [&ctx]() -> std::string {
        return start_lifecycle(ctx, LifecycleOperationType::Stop);
    });

    server.post("/api/service/restart", [&ctx]() -> std::string {
        return start_lifecycle(ctx, LifecycleOperationType::Restart);
    });
}

} // namespace keen_pbr3

#endif // WITH_API
