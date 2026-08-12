#pragma once

#ifdef WITH_API

#include "handlers.hpp"
#include "server.hpp"

namespace keen_pbr3 {

Config normalize_config_for_api_response(Config config);
void protect_config_password_hash(Config& candidate, const Config& visible);
std::string serialize_config_pretty(const Config& config);
void register_config_handler(ApiServer& server, ApiContext& ctx);

} // namespace keen_pbr3

#endif // WITH_API
