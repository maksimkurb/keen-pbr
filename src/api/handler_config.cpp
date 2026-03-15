#ifdef WITH_API

#include "handler_config.hpp"
#include "generated/api_types.hpp"

#include "../config/config.hpp"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <fstream>

namespace keen_pbr3 {

namespace {

void write_config_atomically(const std::string& config_path,
                             const std::string& body) {
    std::string tmp = config_path + ".tmp";
    {
        std::ofstream ofs(tmp);
        if (!ofs.is_open()) throw std::runtime_error("Cannot write config file");
        ofs << body;
    }
    if (std::rename(tmp.c_str(), config_path.c_str()) != 0) {
        throw std::runtime_error("Cannot replace config file");
    }
}

nlohmann::json make_validation_error_json(const ConfigValidationError& error) {
    nlohmann::json issues = nlohmann::json::array();
    for (const auto& issue : error.issues()) {
        issues.push_back({
            {"path", issue.path},
            {"message", issue.message},
        });
    }

    return {
        {"error", error.what()},
        {"validation_errors", std::move(issues)},
    };
}

} // namespace

void register_config_handler(ApiServer& server, ApiContext& ctx) {
    // GET /api/config - return current config and whether it is staged in memory
    server.get("/api/config", [&ctx]() -> std::string {
        nlohmann::json response = {
            {"config", nlohmann::json(ctx.visible_config())},
            {"is_draft", ctx.config_is_draft()},
        };
        return response.dump();
    });

    // POST /api/config - validate and stage in memory only
    server.post("/api/config", [&ctx](const std::string& body) -> std::string {
        try {
            ctx.staged_config = parse_config(body);
        } catch (const ConfigValidationError& e) {
            throw ApiError(e.what(), 400, make_validation_error_json(e).dump());
        } catch (const ConfigError& e) {
            nlohmann::json payload = {
                {"error", e.what()},
                {"validation_errors", nlohmann::json::array({
                    {{"path", "$"}, {"message", e.what()}},
                })},
            };
            throw ApiError(e.what(), 400, payload.dump());
        }

        ctx.staged_config_json = body;

        api::ConfigUpdateResponse resp;
        resp.status = api::ConfigUpdateResponseStatus::OK;
        resp.message = "Config staged in memory";
        return nlohmann::json(resp).dump();
    });

    // POST /api/config/save - persist staged config and apply it
    server.post("/api/config/save", [&ctx]() -> std::string {
        if (!ctx.staged_config.has_value() || !ctx.staged_config_json.has_value()) {
            throw std::runtime_error("No staged config to save");
        }

        write_config_atomically(ctx.config_path, *ctx.staged_config_json);
        ctx.apply_config_fn(*ctx.staged_config);
        ctx.staged_config.reset();
        ctx.staged_config_json.reset();

        api::ConfigUpdateResponse resp;
        resp.status = api::ConfigUpdateResponseStatus::OK;
        resp.message = "Config saved and reload triggered";
        return nlohmann::json(resp).dump();
    });
}

} // namespace keen_pbr3

#endif // WITH_API
