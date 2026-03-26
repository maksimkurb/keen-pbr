#ifdef WITH_API

#include "handler_runtime_outbounds.hpp"

#include <nlohmann/json.hpp>
#include <shared_mutex>

namespace keen_pbr3 {

void register_runtime_outbounds_handler(ApiServer& server, ApiContext& ctx) {
    server.get("/api/runtime/outbounds", [&ctx]() -> std::string {
        std::shared_lock<std::shared_mutex> lock(ctx.state_mutex);
        return nlohmann::json(ctx.runtime_outbounds_fn()).dump();
    });
}

} // namespace keen_pbr3

#endif // WITH_API
