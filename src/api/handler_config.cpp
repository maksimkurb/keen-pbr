#ifdef WITH_API

#include "handler_config.hpp"
#include "generated/api_types.hpp"

#include "../auth/password.hpp"
#include "../config/config.hpp"
#include "../config/config_defaults.hpp"
#include "../util/format_compat.hpp"
#include <keen-pbr/version.hpp>
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

} // namespace

// Unset fields stay unset: the web UI saves the document it received, so a
// default filled in here would end up written to the config file.
Config normalize_config_for_api_response(Config config) {
    if (config.api && config.api->authentication) {
        config.api->authentication->password_hash.reset();
    }

    return config;
}

void protect_config_password_hash(Config& candidate, const Config& visible) {
    const auto existing_password_hash =
        visible.api && visible.api->authentication
            ? visible.api->authentication->password_hash
            : std::nullopt;
    if (existing_password_hash) {
        if (!candidate.api) candidate.api = ApiConfig{};
        if (!candidate.api->authentication) {
            candidate.api->authentication = AuthenticationConfig{};
        }
        candidate.api->authentication->password_hash = existing_password_hash;
    } else if (candidate.api && candidate.api->authentication) {
        candidate.api->authentication->password_hash.reset();
    }
}

Config make_config_defaults() {
    namespace d = config_defaults;
    const auto hex32 = [](uint32_t value) { return format("0x{:08x}", value); };

    Config config;
    DaemonConfig& daemon = config.daemon.emplace();
    daemon.strict_enforcement = d::strict_enforcement;
    daemon.skip_marked_packets = d::skip_marked_packets;
    daemon.clear_dynamic_sets_on_apply = d::clear_dynamic_sets_on_apply;
    daemon.reuse_static_sets_on_runtime_refresh = d::reuse_static_sets_on_runtime_refresh;
    daemon.ipv6_enabled = d::ipv6_enabled;
    daemon.ipset_hashsize = d::ipset_hashsize;
    daemon.ipset_maxelem = d::ipset_maxelem;

    FwmarkConfig& fwmark = config.fwmark.emplace();
    fwmark.start = hex32(d::fwmark_start);
    fwmark.mask = hex32(d::fwmark_mask);

    IprouteConfig& iproute = config.iproute.emplace();
    iproute.table_start = d::table_start;
    iproute.process_router_traffic = d::process_router_traffic;

    config.lists_autoupdate.emplace().enabled = d::lists_autoupdate_enabled;

    DnsConfig& dns_config = config.dns.emplace();
    dns_config.proxy_address = std::string(d::dns_proxy_address);
    dns_config.proxy_port_start = d::dns_proxy_port_start;

    InterceptConfig& intercept = config.intercept.emplace();
    intercept.enabled = d::intercept_enabled;
    intercept.min_ttl_ms = d::intercept_min_ttl_ms;
    intercept.max_ttl_ms = d::intercept_max_ttl_ms;
    InterceptDnsConfig& dns = intercept.dns.emplace();
    dns.enabled = d::intercept_dns_enabled;
    dns.queue_num = d::intercept_dns_queue_num;
    dns.hold_timeout_ms = d::intercept_dns_hold_timeout_ms;
    api::InterceptMarkerConfig& marker = dns.marker.emplace();
    marker.domain = std::string(d::intercept_marker_domain);
    marker.answer_ipv4 = std::string(d::intercept_marker_answer_ipv4);
    InterceptL7Config& l7 = intercept.l7.emplace();
    l7.enabled = d::intercept_l7_enabled;
    l7.nflog_group = d::intercept_l7_nflog_group;
    l7.tls = d::intercept_l7_tls;
    l7.http = d::intercept_l7_http;
    l7.quic = d::intercept_l7_quic;
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

void register_config_handler(ApiServer& server, ApiContext& ctx) {
    // GET /api/config - return current config and whether it is staged in memory
    server.get("/api/config", [&ctx]() -> std::string {
        const Config visible_config =
            normalize_config_for_api_response(ctx.get_visible_config());
        const bool is_draft = ctx.config_is_draft();
        const auto list_refresh_state = ctx.get_list_refresh_state_map(visible_config);
        nlohmann::json draft_config = visible_config;
        draft_config.erase("api");
        nlohmann::json response = {
            {"config", std::move(draft_config)},
            {"is_draft", is_draft},
            {"list_refresh_state", nlohmann::json(list_refresh_state)},
        };
        return response.dump();
    });

    // GET /api/config/defaults - what the daemon uses for unset values
    server.get("/api/config/defaults", []() -> std::string {
        return nlohmann::json(make_config_defaults()).dump();
    });

    // POST /api/config - validate and stage in memory only
    server.post("/api/config", [&ctx](const std::string& body) -> std::string {
        Config staged;
        try {
            staged = parse_config(body);
            // A saved config is always stamped with the running version.
            staged.version = KEEN_PBR3_VERSION_STRING;
            const Config visible = ctx.get_visible_config();
            staged.api = visible.api;
            validate_config(staged, ConfigValidationMode::Runtime,
                            ctx.validation_context());
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

        ctx.stage_config(std::move(staged));

        api::ConfigUpdateResponse resp;
        resp.status = api::OkResponseStatus::OK;
        resp.message = "Config staged in memory";
        return nlohmann::json(resp).dump();
    });

    // POST /api/config/discard - restore the last saved config as the visible config.
    server.post("/api/config/discard", [&ctx]() -> std::string {
        if (!ctx.config_is_draft()) {
            throw ApiError("No staged config to discard", 400);
        }

        ctx.clear_staged_config();

        api::ConfigUpdateResponse resp;
        resp.status = api::OkResponseStatus::OK;
        resp.message = "Staged config discarded";
        return nlohmann::json(resp).dump();
    });

    // POST /api/config/rollback - restore the inode retained before a failed apply.
    server.post("/api/config/rollback", [&ctx]() -> std::string {
        LifecycleRequest request;
        request.type = LifecycleOperationType::RollbackConfig;
        const std::string operation_id = ctx.submit_lifecycle_operation(std::move(request));
        throw ApiAccepted(nlohmann::json{{"operation_id", operation_id},
                                         {"status", "accepted"}}.dump());
    });

    // Password state is deliberately separate from the config representation.
    // Neither this endpoint nor GET /api/config exposes the verifier.
    server.get("/api/auth/password", [&ctx]() -> std::string {
        const auto visible = ctx.get_visible_config();
        const bool password_set = visible.api && visible.api->authentication &&
                                  visible.api->authentication->password_hash.has_value();
        return nlohmann::json{{"password_set", password_set}}.dump();
    });

    server.get("/api/auth/settings", [&ctx]() -> std::string {
        const Config visible = ctx.get_visible_config();
        auto authentication = visible.api
            ? visible.api->authentication.value_or(AuthenticationConfig{})
            : AuthenticationConfig{};
        const auto cors = visible.api
            ? visible.api->cors.value_or(CorsConfig{})
            : CorsConfig{};
        const bool password_set = authentication.password_hash.has_value();
        authentication.password_hash.reset();
        return nlohmann::json{{"authentication", authentication},
                              {"cors", cors},
                              {"password_set", password_set}}
            .dump();
    });

    // Accept clear text only on this write-only operation and persist the verifier.
    server.post("/api/auth/password", [&ctx](const std::string& body) -> std::string {
        try {
            const auto request = nlohmann::json::parse(body);
            if (!request.contains("password") || !request["password"].is_string() ||
                request["password"].get_ref<const std::string&>().empty()) {
                throw ApiError("password must not be empty", 400);
            }

            const Config visible = ctx.get_visible_config();
            AuthenticationConfig authentication = visible.api
                ? visible.api->authentication.value_or(AuthenticationConfig{})
                : AuthenticationConfig{};
            authentication.password_hash =
                auth::generate_password_hash(request["password"].get<std::string>());
            const CorsConfig cors = visible.api
                ? visible.api->cors.value_or(CorsConfig{})
                : CorsConfig{};
            ctx.commit_api_security(std::move(authentication), cors);
            return nlohmann::json{{"password_set", true}}.dump();
        } catch (const ApiError&) {
            throw;
        } catch (const nlohmann::json::exception&) {
            throw ApiError("invalid request", 400);
        }
    });

    // Persist API authentication and CORS settings without applying a routing draft.
    server.post("/api/auth/settings", [&ctx](const std::string& body) -> std::string {
        try {
            const auto request = nlohmann::json::parse(body);
            const auto authentication = request.find("authentication");
            const auto cors = request.find("cors");
            if (authentication == request.end() || !authentication->is_object() ||
                !authentication->contains("enabled") || !(*authentication)["enabled"].is_boolean() ||
                cors == request.end() || !cors->is_object() ||
                !cors->contains("allowed_origins") || !(*cors)["allowed_origins"].is_array()) {
                throw ApiError("invalid security settings", 400);
            }

            AuthenticationConfig next_authentication;
            next_authentication.enabled = (*authentication)["enabled"].get<bool>();
            const Config visible = ctx.get_visible_config();
            if (visible.api && visible.api->authentication) {
                next_authentication.password_hash = visible.api->authentication->password_hash;
            }
            if (const auto password = request.find("password"); password != request.end()) {
                if (!password->is_string() || password->get_ref<const std::string&>().empty()) {
                    throw ApiError("password must not be empty", 400);
                }
                next_authentication.password_hash =
                    auth::generate_password_hash(password->get<std::string>());
            }

            CorsConfig next_cors;
            next_cors.allowed_origins = cors->at("allowed_origins").get<std::vector<std::string>>();
            const bool password_set = next_authentication.password_hash.has_value();
            ctx.commit_api_security(std::move(next_authentication), std::move(next_cors));

            return nlohmann::json{{"password_set", password_set}}.dump();
        } catch (const ApiError&) {
            throw;
        } catch (const nlohmann::json::exception&) {
            throw ApiError("invalid security settings", 400);
        }
    });

    // POST /api/config/save - register work immediately; the daemon owns progress.
    server.post("/api/config/save", [&ctx]() -> std::string {
        std::optional<StagedConfigSnapshot> staged_snapshot;
        staged_snapshot = ctx.get_staged_config_snapshot();

        if (!staged_snapshot.has_value()) {
            throw ApiError("No staged config to save", 400);
        }
        LifecycleRequest request;
        request.type = LifecycleOperationType::ApplyConfig;
        request.config = std::move(staged_snapshot->config);
        request.staged_revision = staged_snapshot->revision;
        const std::string operation_id = ctx.submit_lifecycle_operation(std::move(request));
        throw ApiAccepted(nlohmann::json{{"operation_id", operation_id}, {"status", "accepted"}}.dump());
    });
}

} // namespace keen_pbr3

#endif // WITH_API
