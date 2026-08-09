#ifdef WITH_API

#include "handler_status_events.hpp"
#include "handlers.hpp"
#include "sse_response.hpp"
#include "status_stream.hpp"

#include <httplib.h>

namespace keen_pbr3 {

void register_status_events_handler(ApiServer &server, ApiContext &ctx) {
  server.get_stream("/api/status/events", [&ctx](const httplib::Request &,
                                                 httplib::Response &res) {
    auto subscription = ctx.status_stream->subscribe();
    set_sse_response(res, subscription, [&ctx, subscription] {
      ctx.status_stream->unsubscribe(subscription);
    });
  });
}

} // namespace keen_pbr3
#endif
