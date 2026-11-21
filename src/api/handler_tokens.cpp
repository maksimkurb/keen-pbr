#ifdef WITH_API

#include "handler_tokens.hpp"

#include "generated/api_types.hpp"

#include "../auth/api_scope.hpp"
#include "../auth/password.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <map>

namespace keen_pbr3 {

namespace {

constexpr std::size_t kMaxTokenNameBytes = 128;

std::int64_t unix_now() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

api::ApiTokenInfo to_info(const ApiTokenConfig& record,
                          const std::map<std::string, std::int64_t>& last_used) {
    api::ApiTokenInfo info;
    info.id = record.id.value_or("");
    info.name = record.name.value_or("");
    info.scopes = record.scopes.value_or(std::vector<api::ApiTokenScope>{});
    info.created_at = record.created_at.value_or(0);
    info.expires_at = record.expires_at;
    if (const auto it = last_used.find(info.id); it != last_used.end()) {
        info.last_used_at = it->second;
    }
    return info;
}

std::string list_tokens_json(ApiContext& ctx) {
    const Config visible = ctx.get_visible_config();
    const auto last_used = ctx.get_token_last_used_fn ? ctx.get_token_last_used_fn()
                                                      : std::map<std::string, std::int64_t>{};
    api::ApiTokenListResponse response;
    if (visible.api && visible.api->authentication && visible.api->authentication->tokens) {
        for (const auto& record : *visible.api->authentication->tokens) {
            response.tokens.push_back(to_info(record, last_used));
        }
    }
    return nlohmann::json(response).dump();
}

} // namespace

void register_tokens_handler(ApiServer& server, ApiContext& ctx) {
    server.get("/api/auth/tokens", [&ctx]() -> std::string {
        return list_tokens_json(ctx);
    });

    server.post("/api/auth/tokens", [&ctx](const std::string& body) -> std::string {
        api::ApiTokenCreateRequest request;
        try {
            request = nlohmann::json::parse(body).get<api::ApiTokenCreateRequest>();
        } catch (const std::exception&) {
            throw ApiError("invalid token request", 400);
        }

        const auto first = request.name.find_first_not_of(" \t");
        const auto last = request.name.find_last_not_of(" \t");
        if (first == std::string::npos) throw ApiError("token name must not be empty", 400);
        const std::string name = request.name.substr(first, last - first + 1);
        if (name.size() > kMaxTokenNameBytes) throw ApiError("token name is too long", 400);

        if (request.scopes.empty()) throw ApiError("a token needs at least one scope", 400);
        std::vector<api::ApiTokenScope> scopes;
        for (const auto scope : request.scopes) {
            if (std::find(scopes.begin(), scopes.end(), scope) == scopes.end()) {
                scopes.push_back(scope);
            }
        }
        const auto now = unix_now();
        if (request.expires_at && *request.expires_at <= now) {
            throw ApiError("token expiry must be in the future", 400);
        }

        const Config visible = ctx.get_visible_config();
        if (!visible.api || !visible.api->authentication ||
            !visible.api->authentication->enabled.value_or(false)) {
            throw ApiError("enable authentication before creating API tokens", 409);
        }

        const std::string token = auth::generate_api_token();
        ApiTokenConfig record;
        record.id = auth::generate_api_token_id();
        record.name = name;
        record.scopes = scopes;
        record.token_hash = auth::blake2b_hex(token);
        record.created_at = now;
        record.expires_at = request.expires_at;

        ctx.mutate_api_tokens([&record](std::vector<ApiTokenConfig>& tokens) {
            if (tokens.size() >= auth::kMaxApiTokens) {
                throw ApiError("too many API tokens; revoke one first", 409);
            }
            tokens.push_back(record);
        });

        api::ApiTokenCreateResponse response{to_info(record, {}), token};
        return nlohmann::json(response).dump();
    });

    server.post("/api/auth/tokens/revoke", [&ctx](const std::string& body) -> std::string {
        std::string id;
        try {
            id = nlohmann::json::parse(body).get<api::ApiTokenRevokeRequest>().id;
        } catch (const std::exception&) {
            throw ApiError("invalid token request", 400);
        }
        ctx.mutate_api_tokens([&id](std::vector<ApiTokenConfig>& tokens) {
            const auto it = std::find_if(tokens.begin(), tokens.end(),
                                         [&id](const ApiTokenConfig& t) { return t.id == id; });
            if (it == tokens.end()) throw ApiError("no such API token", 404);
            tokens.erase(it);
        });
        return list_tokens_json(ctx);
    });
}

} // namespace keen_pbr3

#endif // WITH_API
