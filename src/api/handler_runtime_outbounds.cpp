#ifdef WITH_API

#include "handler_runtime_outbounds.hpp"

#include <nlohmann/json.hpp>

namespace keen_pbr3 {

void register_runtime_outbounds_handler(ApiServer& server, ApiContext& ctx) {
    server.get("/api/runtime/outbounds", [&ctx]() -> std::string {
        return nlohmann::json(ctx.get_runtime_outbounds()).dump();
    });
}

} // namespace keen_pbr3

#endif // WITH_API
