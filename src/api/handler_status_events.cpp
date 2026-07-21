#ifdef WITH_API

#include "handler_status_events.hpp"
#include "handlers.hpp"
#include "status_stream.hpp"

#include <chrono>
#include <httplib.h>

namespace keen_pbr3 {

void register_status_events_handler(ApiServer &server, ApiContext &ctx) {
  server.get_stream("/api/status/events", [&ctx](const httplib::Request &,
                                                 httplib::Response &res) {
    auto subscription = ctx.status_stream->subscribe();
    res.set_header("Cache-Control", "no-cache");
    res.set_header("Connection", "keep-alive");
    res.set_header("X-Accel-Buffering", "no");
    res.set_chunked_content_provider(
        "text/event-stream",
        [subscription](size_t, httplib::DataSink &sink) -> bool {
          std::string frame;
          {
            KPBR_UNIQUE_LOCK(lock, subscription->mutex);
            if (!subscription->closed && subscription->messages.empty()) {
              subscription->cv.wait_for(lock, std::chrono::seconds(15));
            }
            if (!subscription->messages.empty()) {
              frame = std::move(subscription->messages.front());
              subscription->messages.pop_front();
            } else if (subscription->closed) {
              sink.done();
              return true;
            } else {
              frame = ": heartbeat\n\n";
            }
          }
          return sink.write(frame.data(), frame.size());
        },
        [&ctx, subscription](bool) {
          ctx.status_stream->unsubscribe(subscription);
        });
  });
}

} // namespace keen_pbr3
#endif
