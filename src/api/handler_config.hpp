#pragma once

#ifdef WITH_API

#include "handlers.hpp"
#include "server.hpp"

namespace keen_pbr3 {

Config normalize_config_for_api_response(Config config);
// The authentication settings as the browser may see them: no password verifier
// and no API token records (their hashes). Every response that carries
// authentication settings goes through this.
AuthenticationConfig sanitize_authentication_for_response(AuthenticationConfig authentication);
Config make_config_defaults();
void protect_config_password_hash(Config& candidate, const Config& visible);
std::string serialize_config_pretty(const Config& config);
void register_config_handler(ApiServer& server, ApiContext& ctx);

} // namespace keen_pbr3

#endif // WITH_API
