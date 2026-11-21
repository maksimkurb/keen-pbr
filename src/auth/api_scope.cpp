#include "api_scope.hpp"

#include "password.hpp"

#include "../api/generated/api_routes.hpp"

#include <algorithm>
#include <array>

namespace keen_pbr3::auth {
namespace {

bool has(const std::vector<ApiScope>& granted, ApiScope scope) {
    return std::find(granted.begin(), granted.end(), scope) != granted.end();
}

bool starts_with(std::string_view value, std::string_view prefix) {
    return value.substr(0, prefix.size()) == prefix;
}

} // namespace

const char* api_scope_name(ApiScope scope) {
    switch (scope) {
    case ApiScope::READ_PROMETHEUS: return "read:prometheus";
    case ApiScope::READ: return "read";
    case ApiScope::WRITE_RULES: return "write:rules";
    case ApiScope::WRITE: return "write";
    }
    return "write";
}

std::optional<ApiScope> parse_api_scope(std::string_view name) {
    for (const auto scope : {ApiScope::READ_PROMETHEUS, ApiScope::READ,
                             ApiScope::WRITE_RULES, ApiScope::WRITE}) {
        if (name == api_scope_name(scope)) return scope;
    }
    return std::nullopt;
}

bool api_scope_grants(const std::vector<ApiScope>& granted, ApiScope required) {
    if (has(granted, ApiScope::WRITE)) return true;
    switch (required) {
    case ApiScope::READ_PROMETHEUS:
        return has(granted, ApiScope::READ_PROMETHEUS) || has(granted, ApiScope::READ) ||
               has(granted, ApiScope::WRITE_RULES);
    case ApiScope::READ:
        return has(granted, ApiScope::READ) || has(granted, ApiScope::WRITE_RULES);
    case ApiScope::WRITE_RULES:
        return has(granted, ApiScope::WRITE_RULES);
    case ApiScope::WRITE:
        return false;
    }
    return false;
}

bool api_scopes_restricted_to_rules(const std::vector<ApiScope>& granted) {
    return !has(granted, ApiScope::WRITE) && has(granted, ApiScope::WRITE_RULES);
}

namespace {

const api::ApiRoute* find_route(std::string_view method, std::string_view path) {
    // HEAD is answered by the GET handler.
    if (method == "HEAD") method = "GET";
    for (const auto& route : api::kApiRoutes) {
        if (route.method == method && route.path == path) return &route;
    }
    return nullptr;
}

} // namespace

std::optional<ApiScope> required_api_scope(std::string_view method, std::string_view path) {
    if (const auto* route = find_route(method, path)) {
        if (route->access == api::RouteAccess::Scope) return route->scope;
        return std::nullopt;
    }
    if (path == "/api/auth" || starts_with(path, "/api/auth/")) return std::nullopt;
    return ApiScope::WRITE;
}

bool is_public_api_route(std::string_view method, std::string_view path) {
    const auto* route = find_route(method, path);
    return route != nullptr && route->access == api::RouteAccess::Public;
}

bool is_rules_only_safe_route(std::string_view method, std::string_view path) {
    // POST /api/config and /api/config/save run the rules-only guard on the
    // document; /api/lists/refresh takes at most a list name; discarding only deletes a draft.
    static constexpr std::array<std::string_view, 4> kSafe{
        "/api/config", "/api/config/save", "/api/lists/refresh", "/api/config/discard"};
    if (method != "POST") return false;
    return std::find(kSafe.begin(), kSafe.end(), path) != kSafe.end();
}

std::string generate_api_token() {
    return std::string(kApiTokenPrefix) + random_token();
}

std::string generate_api_token_id() {
    return random_token().substr(0, 12);
}

} // namespace keen_pbr3::auth
