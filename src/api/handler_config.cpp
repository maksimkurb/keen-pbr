#ifdef WITH_API

#include "handler_config.hpp"
#include "generated/api_types.hpp"

#include "../config/config.hpp"
#include <nlohmann/json.hpp>

#include <cstdio>
#include <fstream>
#include <shared_mutex>

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
        std::shared_lock<std::shared_mutex> lock(ctx.state_mutex);
        nlohmann::json response = {
            {"config", nlohmann::json(ctx.visible_config_fn())},
            {"is_draft", ctx.config_is_draft_fn()},
        };
        return response.dump();
    });

    // POST /api/config - validate and stage in memory only
    server.post("/api/config", [&ctx](const std::string& body) -> std::string {
        Config staged;
        try {
            staged = parse_config(body);
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

        ctx.stage_config_fn(std::move(staged), body);

        api::ConfigUpdateResponse resp;
        resp.status = api::ConfigUpdateResponseStatus::OK;
        resp.message = "Config staged in memory";
        return nlohmann::json(resp).dump();
    });

    // POST /api/config/save - persist staged config and enqueue apply
    server.post("/api/config/save", [&ctx]() -> std::string {
        {
            std::lock_guard<std::mutex> op_lock(ctx.config_op_mutex);
            if (ctx.config_op_state.load(std::memory_order_acquire) != ConfigOperationState::Idle) {
                throw std::runtime_error("Another config operation is already in progress");
            }
            ctx.config_op_state.store(ConfigOperationState::Saving, std::memory_order_release);
        }

        std::optional<std::pair<Config, std::string>> staged_snapshot;
        {
            std::shared_lock<std::shared_mutex> lock(ctx.state_mutex);
            staged_snapshot = ctx.staged_config_snapshot_fn();
        }

        if (!staged_snapshot.has_value()) {
            std::lock_guard<std::mutex> op_lock(ctx.config_op_mutex);
            ctx.config_op_state.store(ConfigOperationState::Idle, std::memory_order_release);
            ctx.config_op_cv.notify_all();
            throw std::runtime_error("No staged config to save");
        }

        try {
            write_config_atomically(ctx.config_path, staged_snapshot->second);

            ctx.enqueue_apply_validated_config_fn(staged_snapshot->first, staged_snapshot->second);

            api::ConfigUpdateResponse resp;
            resp.status = api::ConfigUpdateResponseStatus::OK;
            resp.message = "Config saved, apply queued";
            return nlohmann::json(resp).dump();
        } catch (...) {
            std::lock_guard<std::mutex> op_lock(ctx.config_op_mutex);
            ctx.config_op_state.store(ConfigOperationState::Idle, std::memory_order_release);
            ctx.config_op_cv.notify_all();
            throw;
        }
    });
}

} // namespace keen_pbr3

#endif // WITH_API
