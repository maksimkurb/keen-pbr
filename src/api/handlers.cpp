#ifdef WITH_API

#include "handlers.hpp"
#include "handler_health_service.hpp"
#include "handler_reload.hpp"
#include "handler_config.hpp"
#include "handler_health_routing.hpp"
#include "handler_runtime_outbounds.hpp"
#include "handler_test_routing.hpp"
#include "handler_dns_test.hpp"

namespace keen_pbr3 {

void register_api_handlers(ApiServer& server, ApiContext& ctx) {
    register_health_service_handler(server, ctx);
    register_reload_handler(server, ctx);
    register_config_handler(server, ctx);
    register_health_routing_handler(server, ctx);
    register_runtime_outbounds_handler(server, ctx);
    register_test_routing_handler(server, ctx);
    register_dns_test_handler(server, ctx);
}

} // namespace keen_pbr3

#endif // WITH_API
