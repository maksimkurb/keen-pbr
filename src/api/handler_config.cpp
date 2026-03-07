#ifdef WITH_API

#include "handler_config.hpp"

#include "../config/config.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>

namespace keen_pbr3 {

void register_config_handler(ApiServer& server, ApiContext& ctx) {
    // GET /api/config - return raw config file content
    server.get("/api/config", [&ctx]() -> std::string {
        std::ifstream ifs(ctx.config_path);
        if (!ifs.is_open()) throw std::runtime_error("Cannot open config file");
        std::ostringstream ss;
        ss << ifs.rdbuf();
        return ss.str();
    });

    // POST /api/config - validate, write, and reload
    server.post("/api/config", [&ctx](const std::string& body) -> std::string {
        // Validate by parsing (throws on invalid config)
        parse_config(body);
        // Write atomically: temp file then rename
        std::string tmp = ctx.config_path + ".tmp";
        {
            std::ofstream ofs(tmp);
            if (!ofs.is_open()) throw std::runtime_error("Cannot write config file");
            ofs << body;
        }
        if (std::rename(tmp.c_str(), ctx.config_path.c_str()) != 0)
            throw std::runtime_error("Cannot replace config file");
        // Trigger reload
        ctx.reload_fn();
        nlohmann::json j;
        j["status"] = "ok";
        j["message"] = "Config updated and reload triggered";
        return j.dump();
    });
}

} // namespace keen_pbr3

#endif // WITH_API
