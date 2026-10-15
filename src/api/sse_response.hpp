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
// When `still_authorized` is set it is evaluated on every tick and the stream
// ends once it returns false (credentials revoked, session expired).
void set_sse_response(httplib::Response& response,
                      SseBroadcaster::SubscriptionPtr subscription,
                      std::function<void()> unsubscribe,
                      SseFrameFormatter formatter = {},
                      std::function<bool()> still_authorized = {});

} // namespace keen_pbr3

#endif // WITH_API
