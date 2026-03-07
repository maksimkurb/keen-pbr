#ifdef WITH_API

#include "handler_reload.hpp"

#include <nlohmann/json.hpp>

namespace keen_pbr3 {

void register_reload_handler(ApiServer& server, ApiContext& ctx) {
    // POST /api/reload - trigger list re-download and re-apply
    server.post("/api/reload", [&ctx]() -> std::string {
        ctx.reload_fn();
        nlohmann::json j;
        j["status"] = "ok";
        j["message"] = "Reload triggered";
        return j.dump();
    });
}

} // namespace keen_pbr3

#endif // WITH_API
