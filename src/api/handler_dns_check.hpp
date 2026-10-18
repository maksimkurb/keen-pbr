#pragma once

#ifdef WITH_API

#include "handlers.hpp"
#include "server.hpp"

namespace keen_pbr3 {

// POST /api/dns/servers/check
// Body (all fields optional): { "domains": [...], "custom_domains": [...], "servers": [...] }
// Asks every DNS server of the applied configuration for the domains and
// returns how each one answered.
void register_dns_check_handler(ApiServer& server, ApiContext& ctx);

} // namespace keen_pbr3

#endif // WITH_API
