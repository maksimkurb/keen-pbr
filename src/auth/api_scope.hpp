#pragma once

#include "../api/generated/api_types.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace keen_pbr3::auth {

using ApiScope = api::ApiTokenScope;

// Every API token value starts with this, so it is easy to tell apart from a
// WebUI session token and to find in a leak scan.
inline constexpr std::string_view kApiTokenPrefix = "kpbr_";

inline constexpr std::size_t kMaxApiTokens = 32;

const char* api_scope_name(ApiScope scope);
std::optional<ApiScope> parse_api_scope(std::string_view name);

// `write` grants everything; `read` and `write:rules` grant `read:prometheus`
// and `read` (rules cannot be edited blind); `write` also grants `write:rules`.
bool api_scope_grants(const std::vector<ApiScope>& granted, ApiScope required);

// True for a caller that may change rules but not the rest of the system.
bool api_scopes_restricted_to_rules(const std::vector<ApiScope>& granted);

// The scope a request needs, from the operations declared in docs/openapi.yaml.
// nullopt means the request is reserved for the administrator password or a
// WebUI session, i.e. no API token may make it. A route the document does not
// declare needs `write`, except under /api/auth/, which is never open to a token.
std::optional<ApiScope> required_api_scope(std::string_view method, std::string_view path);

// True for a route that needs no authentication at all.
bool is_public_api_route(std::string_view method, std::string_view path);

// True for a route that is safe for a caller that may only change rules: it
// either takes no input or checks it with the rules-only guard. A route that
// needs `write:rules` but is not listed here is refused for such a caller.
bool is_rules_only_safe_route(std::string_view method, std::string_view path);

std::string generate_api_token();
std::string generate_api_token_id();

} // namespace keen_pbr3::auth
