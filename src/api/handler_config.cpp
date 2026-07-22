#ifdef WITH_API

#include "handler_config.hpp"
#include "generated/api_types.hpp"

#include "../config/config.hpp"
#include <nlohmann/json.hpp>

#include <functional>
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
    config.daemon->clear_dynamic_sets_on_apply =
        config.daemon->clear_dynamic_sets_on_apply.value_or(true);
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

    // POST /api/config/save - register work immediately; the daemon owns progress.
    server.post("/api/config/save", [&ctx]() -> std::string {
        std::optional<std::pair<Config, std::string>> staged_snapshot;
        staged_snapshot = ctx.get_staged_config_snapshot();

        if (!staged_snapshot.has_value()) {
            throw ApiError("No staged config to save", 400);
        }
        LifecycleRequest request;
        request.type = LifecycleOperationType::ApplyConfig;
        request.config = staged_snapshot->first;
        request.serialized_config = staged_snapshot->second;
        const std::string operation_id = ctx.submit_lifecycle_operation(std::move(request));
        throw ApiAccepted(nlohmann::json{{"operation_id", operation_id}, {"status", "accepted"}}.dump());
    });
}

} // namespace keen_pbr3

#endif // WITH_API
