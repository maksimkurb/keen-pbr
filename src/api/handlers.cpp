#ifdef WITH_API

#include "handlers.hpp"
#include "handler_health_service.hpp"
#include "handler_lists_refresh.hpp"
#include "handler_reload.hpp"
#include "handler_config.hpp"
#include "handler_health_routing.hpp"
#include "handler_runtime_interfaces.hpp"
#include "handler_runtime_outbounds.hpp"
#include "handler_test_routing.hpp"
#include "handler_dns_test.hpp"
#include "handler_diagnostics.hpp"
#include "handler_status_events.hpp"

#include <httplib.h>

namespace keen_pbr3 {

void register_api_handlers(ApiServer& server, ApiContext& ctx) {
    register_health_service_handler(server, ctx);
    register_reload_handler(server, ctx);
    register_lists_refresh_handler(server, ctx);
    register_config_handler(server, ctx);
    register_health_routing_handler(server, ctx);
    register_runtime_interfaces_handler(server, ctx);
    register_runtime_outbounds_handler(server, ctx);
    register_test_routing_handler(server, ctx);
    register_dns_test_handler(server, ctx);
    register_diagnostics_handler(server);
    register_status_events_handler(server, ctx);
    server.get_stream("/metrics", [&ctx](const httplib::Request&, httplib::Response& response) {
        if (!ctx.get_prometheus_metrics_fn) {
            throw ApiError("Metrics are unavailable", 503);
        }
        response.set_content(ctx.get_prometheus_metrics_fn(),
                             "text/plain; version=0.0.4; charset=utf-8");
    });
}

} // namespace keen_pbr3

#endif // WITH_API
