#ifdef WITH_API

#include "handler_runtime_interfaces.hpp"

#include <nlohmann/json.hpp>

namespace keen_pbr3 {

void register_runtime_interfaces_handler(ApiServer& server, ApiContext& ctx) {
    server.get("/api/runtime/interfaces", [&ctx]() -> std::string {
        return nlohmann::json(ctx.get_runtime_interfaces()).dump();
    });
}

} // namespace keen_pbr3

#endif // WITH_API
