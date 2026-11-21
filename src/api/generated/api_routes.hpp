// Generated from docs/openapi.yaml via build_scripts/generate_api_types.sh
// Run "make generate" to regenerate (requires Node.js).

#pragma once

#include "api_types.hpp"

#include <array>
#include <string_view>

namespace keen_pbr3::api {

// Public: no authentication. SessionOnly: the administrator password or a WebUI
// session, never an API token. Scope: an API token holding `scope` (or wider).
enum class RouteAccess { Public, SessionOnly, Scope };

struct ApiRoute {
    std::string_view method;
    std::string_view path;
    RouteAccess access;
    ApiTokenScope scope;
};

inline constexpr std::array<ApiRoute, 33> kApiRoutes{{
    {"GET", "/metrics", RouteAccess::Scope, ApiTokenScope::READ_PROMETHEUS},
    {"GET", "/api/auth/status", RouteAccess::Public, ApiTokenScope::WRITE},
    {"POST", "/api/auth/login", RouteAccess::Public, ApiTokenScope::WRITE},
    {"POST", "/api/auth/logout", RouteAccess::SessionOnly, ApiTokenScope::WRITE},
    {"GET", "/api/auth/password", RouteAccess::SessionOnly, ApiTokenScope::WRITE},
    {"POST", "/api/auth/password", RouteAccess::SessionOnly, ApiTokenScope::WRITE},
    {"GET", "/api/auth/settings", RouteAccess::SessionOnly, ApiTokenScope::WRITE},
    {"POST", "/api/auth/settings", RouteAccess::SessionOnly, ApiTokenScope::WRITE},
    {"POST", "/api/auth/enabled", RouteAccess::SessionOnly, ApiTokenScope::WRITE},
    {"POST", "/api/auth/cors", RouteAccess::SessionOnly, ApiTokenScope::WRITE},
    {"GET", "/api/auth/tokens", RouteAccess::SessionOnly, ApiTokenScope::WRITE},
    {"POST", "/api/auth/tokens", RouteAccess::SessionOnly, ApiTokenScope::WRITE},
    {"POST", "/api/auth/tokens/revoke", RouteAccess::SessionOnly, ApiTokenScope::WRITE},
    {"GET", "/api/health/service", RouteAccess::Scope, ApiTokenScope::READ},
    {"POST", "/api/service/start", RouteAccess::Scope, ApiTokenScope::WRITE},
    {"POST", "/api/service/stop", RouteAccess::Scope, ApiTokenScope::WRITE},
    {"POST", "/api/service/restart", RouteAccess::Scope, ApiTokenScope::WRITE},
    {"POST", "/api/lists/refresh", RouteAccess::Scope, ApiTokenScope::WRITE_RULES},
    {"GET", "/api/config", RouteAccess::Scope, ApiTokenScope::READ},
    {"POST", "/api/config", RouteAccess::Scope, ApiTokenScope::WRITE_RULES},
    {"GET", "/api/config/defaults", RouteAccess::Scope, ApiTokenScope::READ},
    {"POST", "/api/config/save", RouteAccess::Scope, ApiTokenScope::WRITE_RULES},
    {"POST", "/api/config/discard", RouteAccess::Scope, ApiTokenScope::WRITE_RULES},
    {"POST", "/api/config/rollback", RouteAccess::Scope, ApiTokenScope::WRITE},
    {"POST", "/api/routing/test", RouteAccess::Scope, ApiTokenScope::READ},
    {"GET", "/api/health/routing", RouteAccess::Scope, ApiTokenScope::READ},
    {"GET", "/api/health/issues", RouteAccess::Scope, ApiTokenScope::READ},
    {"GET", "/api/runtime/outbounds", RouteAccess::Scope, ApiTokenScope::READ},
    {"GET", "/api/diagnostics/command-failure", RouteAccess::Scope, ApiTokenScope::READ},
    {"GET", "/api/runtime/interfaces", RouteAccess::Scope, ApiTokenScope::READ},
    {"GET", "/api/status/events", RouteAccess::Scope, ApiTokenScope::READ},
    {"POST", "/api/dns/servers/check", RouteAccess::Scope, ApiTokenScope::READ},
    {"GET", "/api/dns/test", RouteAccess::Scope, ApiTokenScope::READ},
}};

} // namespace keen_pbr3::api
