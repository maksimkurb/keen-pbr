#ifdef WITH_API

#include "handler_config.hpp"
#include "generated/api_types.hpp"

#include "../config/config.hpp"
#include "../log/logger.hpp"
#include <nlohmann/json.hpp>

#include <functional>
#include <stdexcept>
#include <string>

namespace keen_pbr3 {

namespace {

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

Config normalize_config_for_api_response(Config config) {
    if (!config.daemon.has_value()) {
        config.daemon = DaemonConfig{};
    }

    config.daemon->skip_marked_packets =
        config.daemon->skip_marked_packets.value_or(true);
    config.daemon->ipv6_enabled =
        config.daemon->ipv6_enabled.value_or(true);

    return config;
}

std::string serialize_config_pretty(const Config& config) {
    nlohmann::json json = config;
    std::function<bool(nlohmann::json&)> prune_json = [&](nlohmann::json& value) -> bool {
        if (value.is_object()) {
            for (auto it = value.begin(); it != value.end();) {
                if (prune_json(it.value())) {
                    it = value.erase(it);
                } else {
                    ++it;
                }
            }
            return value.empty();
        }

        if (value.is_array()) {
            for (auto& item : value) {
                (void)prune_json(item);
            }
            return false;
        }

        return value.is_null();
    };

    (void)prune_json(json);
    return json.dump(1, '\t') + "\n";
}

} // namespace

void register_config_handler(ApiServer& server, ApiContext& ctx) {
    // GET /api/config - return current config and whether it is staged in memory
    server.get("/api/config", [&ctx]() -> std::string {
        const Config visible_config =
            normalize_config_for_api_response(ctx.get_visible_config());
        const bool is_draft = ctx.config_is_draft();
        const auto list_refresh_state = ctx.get_list_refresh_state_map(visible_config);
        nlohmann::json response = {
            {"config", nlohmann::json(visible_config)},
            {"is_draft", is_draft},
            {"list_refresh_state", nlohmann::json(list_refresh_state)},
        };
        return response.dump();
    });

    // POST /api/config - validate and stage in memory only
    server.post("/api/config", [&ctx](const std::string& body) -> std::string {
        Config staged;
        try {
            staged = parse_config(body);
            validate_config(staged);
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

        std::string formatted_config = serialize_config_pretty(staged);
        ctx.stage_config(std::move(staged), std::move(formatted_config));

        api::ConfigUpdateResponse resp;
        resp.status = api::ConfigUpdateResponseStatus::OK;
        resp.message = "Config staged in memory";
        return nlohmann::json(resp).dump();
    });

    // POST /api/config/save - dry-run check, apply staged config, then persist it
    server.post("/api/config/save", [&ctx]() -> std::string {
        ctx.begin_save_operation();

        std::optional<std::pair<Config, std::string>> staged_snapshot;
        try {
            staged_snapshot = ctx.get_staged_config_snapshot();
        } catch (...) {
            ctx.finish_config_operation();
            throw;
        }

        if (!staged_snapshot.has_value()) {
            ctx.finish_config_operation();
            throw ApiError("No staged config to save", 400);
        }

        // Phase 1: validation + dry-run apply check.
        try {
            ctx.validate_candidate_config(staged_snapshot->first);
        } catch (const ConfigValidationError& e) {
            ctx.finish_config_operation();
            nlohmann::json error_payload = make_validation_error_json(e);
            error_payload["saved"] = false;
            error_payload["applied"] = false;
            error_payload["rolled_back"] = false;
            throw ApiError("Dry-run apply check failed", 400, error_payload.dump());
        } catch (const std::exception& e) {
            ctx.finish_config_operation();
            nlohmann::json error_payload = {
                {"error", std::string("Dry-run apply check failed: ") + e.what()},
                {"saved", false},
                {"applied", false},
                {"rolled_back", false},
            };
            throw ApiError("Dry-run apply check failed", 500, error_payload.dump());
        }

        // Phase 2: apply + durable commit.
        try {
            ConfigApplyResult apply_result =
                ctx.enqueue_apply_validated_config(staged_snapshot->first, staged_snapshot->second);

            if (!apply_result.error.empty()) {
                nlohmann::json error_payload = {
                    {"error", std::string("Commit/apply failed: ") + apply_result.error},
                    {"saved", apply_result.saved},
                    {"applied", apply_result.applied},
                    {"rolled_back", apply_result.rolled_back},
                };
                throw ApiError("Commit/apply failed", 500, error_payload.dump());
            }

            nlohmann::json response = {
                {"status", "ok"},
                {"message", "Config saved and applied"},
                {"saved", apply_result.saved},
                {"applied", apply_result.applied},
                {"rolled_back", apply_result.rolled_back},
            };
            if (apply_result.apply_started_ts.has_value()) {
                response["apply_started_ts"] = *apply_result.apply_started_ts;
            }
            ctx.finish_config_operation();
            return response.dump();
        } catch (...) {
            ctx.finish_config_operation();
            throw;
        }
    });
}

} // namespace keen_pbr3

#endif // WITH_API
