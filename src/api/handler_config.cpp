#ifdef WITH_API

#include "handler_config.hpp"
#include "generated/api_types.hpp"

#include "../auth/password.hpp"
#include "../config/config.hpp"
#include "../config/config_defaults.hpp"
#include "../config/list_file_policy.hpp"
#include "../util/format_compat.hpp"
#include <keen-pbr/version.hpp>
#include <nlohmann/json.hpp>

#include <functional>
#include <map>
#include <set>
#include <string>

namespace keen_pbr3 {

namespace {

[[noreturn]] void refuse_list_field(const std::string& name, const char* field,
                                    const std::string& message, int status) {
    const std::string path = "lists." + name + "." + field;
    const std::string text = path + ": " + message;
    const nlohmann::json payload = {
        {"error", text},
        {"validation_errors", nlohmann::json::array({{{"path", path}, {"message", message}}})},
    };
    throw ApiError(text, status, payload.dump());
}

// A list `file` that is new or different from the active config must lie in the
// configuration directory (see list_file_policy.hpp). A value that is already in
// the active config is never checked, so hand-edited configs keep working.
// This applies to every caller of the API: to let only restricted tokens be held
// to it, return early unless api_caller_rules_only().
void require_list_files_allowed(const std::string& config_path, const Config& base,
                                const Config& candidate) {
    const std::map<std::string, ListConfig> none;
    const auto& base_lists = base.lists ? *base.lists : none;
    const auto& candidate_lists = candidate.lists ? *candidate.lists : none;
    const std::string config_dir = config_directory_of(config_path);
    // A path already used by the active config stays allowed under any list
    // name, so renaming a list keeps its file.
    std::set<std::string> known_files;
    for (const auto& [name, list] : base_lists) {
        if (list.file) known_files.insert(*list.file);
    }
    for (const auto& [name, list] : candidate_lists) {
        if (!list.file) continue;
        if (known_files.count(*list.file) != 0) continue;
        if (const auto problem = list_file_policy_violation(*list.file, config_dir)) {
            refuse_list_field(name, "file", problem->message, api_caller_rules_only() ? 403 : 400);
        }
    }
}

// What a token that may only change rules is allowed to do with the lists it
// adds or changes: a list may hold domains, addresses and a local file. Where
// the list is fetched from (`url`, `detour`) could make the daemon reach things
// the token has no business with, so those stay with the administrator.
void require_rules_only_lists(const Config& base, const Config& candidate) {
    const std::map<std::string, ListConfig> none;
    const auto& base_lists = base.lists ? *base.lists : none;
    const auto& candidate_lists = candidate.lists ? *candidate.lists : none;
    for (const auto& [name, list] : candidate_lists) {
        const auto found = base_lists.find(name);
        const ListConfig* before = found == base_lists.end() ? nullptr : &found->second;
        if (list.url != (before ? before->url : std::nullopt)) {
            refuse_list_field(name, "url",
                              "an API token with only write:rules access cannot add, change or remove the url of a list",
                              403);
        }
        if (list.detour != (before ? before->detour : std::nullopt)) {
            refuse_list_field(name, "detour",
                              "an API token with only write:rules access cannot add, change or remove the detour of a list",
                              403);
        }
    }
}

// A token with `write:rules` but not `write` may change the routing rules and
// the lists they use, and nothing else.
void require_rules_only_change(const Config& base, const Config& candidate) {
    if (!api_caller_rules_only()) return;
    const auto outside_rules = [](const Config& config) {
        nlohmann::json json = config;
        for (const char* key : {"version", "api", "route", "lists"}) json.erase(key);
        return json;
    };
    if (outside_rules(base) != outside_rules(candidate)) {
        throw ApiError("this token may only change routing rules and lists", 403);
    }
    require_rules_only_lists(base, candidate);
}

} // namespace

// Unset fields stay unset: the web UI saves the document it received, so a
// default filled in here would end up written to the config file.
AuthenticationConfig sanitize_authentication_for_response(AuthenticationConfig authentication) {
    authentication.password_hash.reset();
    authentication.tokens.reset();
    return authentication;
}

Config normalize_config_for_api_response(Config config) {
    if (config.api && config.api->authentication) {
        config.api->authentication =
            sanitize_authentication_for_response(*config.api->authentication);
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
            {"config_dir", config_directory_of(ctx.config_path)},
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
            require_list_files_allowed(ctx.config_path, ctx.get_active_config(), staged);
            require_rules_only_change(ctx.get_active_config(), staged);
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
        authentication = sanitize_authentication_for_response(std::move(authentication));
        // The serializer writes unset fields as null; neither key belongs in the response.
        nlohmann::json authentication_json = authentication;
        authentication_json.erase("password_hash");
        authentication_json.erase("tokens");
        return nlohmann::json{{"authentication", std::move(authentication_json)},
                              {"cors", cors},
                              {"password_set", password_set}}
            .dump();
    });

    // The security endpoints below run on the control task through
    // mutate_api_config and each edit touches only its own fields, so none of
    // them can undo a concurrent change to the others (a token created or revoked
    // meanwhile, for example).

    // Accept clear text only on this write-only operation and persist the verifier.
    server.post("/api/auth/password", [&ctx](const std::string& body) -> std::string {
        try {
            const auto request = nlohmann::json::parse(body);
            if (!request.contains("password") || !request["password"].is_string() ||
                request["password"].get_ref<const std::string&>().empty()) {
                throw ApiError("password must not be empty", 400);
            }
            const std::string hash =
                auth::generate_password_hash(request["password"].get<std::string>());
            ctx.mutate_api_config([&hash](ApiConfig& api) {
                api.authentication->password_hash = hash;
            });
            return nlohmann::json{{"password_set", true}}.dump();
        } catch (const ApiError&) {
            throw;
        } catch (const nlohmann::json::exception&) {
            throw ApiError("invalid request", 400);
        }
    });

    server.post("/api/auth/enabled", [&ctx](const std::string& body) -> std::string {
        api::AuthEnabledRequest request;
        try {
            request = nlohmann::json::parse(body).get<api::AuthEnabledRequest>();
        } catch (const std::exception&) {
            throw ApiError("invalid request", 400);
        }
        bool password_set = false;
        ctx.mutate_api_config([&](ApiConfig& api) {
            api.authentication->enabled = request.enabled;
            password_set = api.authentication->password_hash.has_value();
        });
        return nlohmann::json{{"password_set", password_set}}.dump();
    });

    server.post("/api/auth/cors", [&ctx](const std::string& body) -> std::string {
        api::AuthCorsRequest request;
        try {
            request = nlohmann::json::parse(body).get<api::AuthCorsRequest>();
        } catch (const std::exception&) {
            throw ApiError("invalid request", 400);
        }
        bool password_set = false;
        ctx.mutate_api_config([&](ApiConfig& api) {
            if (!api.cors) api.cors = CorsConfig{};
            api.cors->allowed_origins = request.allowed_origins;
            password_set = api.authentication->password_hash.has_value();
        });
        return nlohmann::json{{"password_set", password_set}}.dump();
    });

    // Persist API authentication and CORS settings without applying a routing draft.
    // Only `enabled`, the allowed origins and, when given, the password change.
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

            const bool enabled = (*authentication)["enabled"].get<bool>();
            const auto origins = cors->at("allowed_origins").get<std::vector<std::string>>();
            std::optional<std::string> password_hash;
            if (const auto password = request.find("password"); password != request.end()) {
                if (!password->is_string() || password->get_ref<const std::string&>().empty()) {
                    throw ApiError("password must not be empty", 400);
                }
                password_hash = auth::generate_password_hash(password->get<std::string>());
            }

            bool password_set = false;
            ctx.mutate_api_config([&](ApiConfig& api) {
                api.authentication->enabled = enabled;
                if (password_hash) api.authentication->password_hash = *password_hash;
                if (!api.cors) api.cors = CorsConfig{};
                api.cors->allowed_origins = origins;
                password_set = api.authentication->password_hash.has_value();
            });

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
        require_rules_only_change(ctx.get_active_config(), staged_snapshot->config);
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
