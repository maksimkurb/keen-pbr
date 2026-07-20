#pragma once

#ifdef WITH_API
namespace keen_pbr3 {
class ApiServer;
struct ApiContext;
void register_status_events_handler(ApiServer &server, ApiContext &ctx);
} // namespace keen_pbr3
#endif
