#pragma once

#ifdef WITH_API

#include "sse_broadcaster.hpp"

#include <functional>
#include <string>

namespace httplib {
class Response;
}

namespace keen_pbr3 {

using SseFrameFormatter = std::function<std::string(std::string)>;

// Configure a broadcaster-backed SSE response. The provider sends periodic
// heartbeats so disconnected clients are detected even when no events arrive.
void set_sse_response(httplib::Response& response,
                      SseBroadcaster::SubscriptionPtr subscription,
                      std::function<void()> unsubscribe,
                      SseFrameFormatter formatter = {});

} // namespace keen_pbr3

#endif // WITH_API
