#ifdef WITH_API

#include "sse_response.hpp"

#include <chrono>
#include <httplib.h>
#include <string_view>
#include <utility>

namespace keen_pbr3 {

namespace {

constexpr auto kHeartbeatInterval = std::chrono::seconds{1};
constexpr std::string_view kHeartbeatFrame = ": heartbeat\n\n";

} // namespace

void set_sse_response(httplib::Response& response,
                      SseBroadcaster::SubscriptionPtr subscription,
                      std::function<void()> unsubscribe,
                      SseFrameFormatter formatter) {
    response.set_header("Cache-Control", "no-cache");
    response.set_header("Connection", "keep-alive");
    response.set_header("X-Accel-Buffering", "no");
    response.set_chunked_content_provider(
        "text/event-stream",
        [subscription = std::move(subscription),
         formatter = std::move(formatter)](std::size_t, httplib::DataSink& sink) -> bool {
            std::string frame;
            {
                KPBR_UNIQUE_LOCK(lock, subscription->mutex);
                if (!subscription->closed && subscription->messages.empty()) {
                    subscription->cv.wait_for(lock, kHeartbeatInterval);
                }

                if (!subscription->messages.empty()) {
                    frame = std::move(subscription->messages.front());
                    subscription->messages.pop_front();
                } else if (subscription->closed) {
                    sink.done();
                    return true;
                } else {
                    return sink.write(kHeartbeatFrame.data(), kHeartbeatFrame.size());
                }
            }

            if (formatter) {
                frame = formatter(std::move(frame));
            }
            return sink.write(frame.data(), frame.size());
        },
        [unsubscribe = std::move(unsubscribe)](bool) {
            unsubscribe();
        });
}

} // namespace keen_pbr3

#endif // WITH_API
