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
    server.post("/api/service/start", [&ctx]() -> std::string {
        ctx.start_runtime();
        return success_response("Routing runtime started");
    });

    server.post("/api/service/stop", [&ctx]() -> std::string {
        ctx.stop_runtime();
        return success_response("Routing runtime stopped");
    });

    server.post("/api/service/restart", [&ctx]() -> std::string {
        ctx.restart_runtime();
        return success_response("Routing runtime restarted");
    });
}

} // namespace keen_pbr3

#endif // WITH_API
