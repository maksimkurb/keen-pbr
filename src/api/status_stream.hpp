#pragma once

#ifdef WITH_API

#include "generated/api_types.hpp"
#include "sse_broadcaster.hpp"

#include "../util/traced_mutex.hpp"

#include <cstdint>
#include <nlohmann/json.hpp>

#include <functional>
#include <string>

namespace keen_pbr3 {

enum class StatusUpdate : std::uint8_t {
  None = 0,
  Service = 1U << 0U,
  Outbounds = 1U << 1U,
  Interfaces = 1U << 2U,
  Issues = 1U << 3U,
  Routing = 1U << 4U,
  Auth = 1U << 5U,
  All = (1U << 0U) | (1U << 1U) | (1U << 2U) | (1U << 3U) | (1U << 4U) |
        (1U << 5U),
};

constexpr StatusUpdate operator|(StatusUpdate lhs, StatusUpdate rhs) {
  return static_cast<StatusUpdate>(static_cast<std::uint8_t>(lhs) |
                                   static_cast<std::uint8_t>(rhs));
}

constexpr bool has_status_update(StatusUpdate value, StatusUpdate flag) {
  return (static_cast<std::uint8_t>(value) & static_cast<std::uint8_t>(flag)) != 0;
}

class StatusStream {
public:
  using ServiceBuilder = std::function<api::HealthResponse()>;
  using OutboundsBuilder = std::function<api::RuntimeOutboundsResponse()>;
  using InterfacesBuilder =
      std::function<api::RuntimeInterfaceInventoryResponse()>;
  using IssuesBuilder = std::function<api::HealthIssuesResponse()>;
  // The same JSON as GET /api/health/routing.
  using RoutingBuilder = std::function<nlohmann::json()>;
  // {"enabled", "device_name"}: GET /api/auth/status without `authenticated`.
  using AuthBuilder = std::function<nlohmann::json()>;
  // Called after every subscribe, outside the stream's lock.
  using SubscribeHook = std::function<void()>;

  struct Builders {
    ServiceBuilder service;
    OutboundsBuilder outbounds;
    InterfacesBuilder interfaces;
    IssuesBuilder issues;
    RoutingBuilder routing;
    AuthBuilder auth;
  };

  explicit StatusStream(Builders builders, size_t max_queue_size = 128);

  // Must be set before the stream is used by more than one thread.
  void set_subscribe_hook(SubscribeHook hook);
  bool has_subscribers();
  SseBroadcaster::SubscriptionPtr subscribe();
  void unsubscribe(const SseBroadcaster::SubscriptionPtr &subscription);
  void reconcile(StatusUpdate updates);
  void close_all();

private:
  void rebuild(StatusUpdate updates) REQUIRES(mutex_);

  Builders builders_;
  SubscribeHook subscribe_hook_;
  SseBroadcaster broadcaster_;
  TracedMutex mutex_;
  std::string service_ GUARDED_BY(mutex_);
  std::string outbounds_ GUARDED_BY(mutex_);
  std::string interfaces_ GUARDED_BY(mutex_);
  std::string issues_ GUARDED_BY(mutex_);
  std::string routing_ GUARDED_BY(mutex_);
  std::string auth_ GUARDED_BY(mutex_);
  bool initialized_ GUARDED_BY(mutex_){false};
};

std::string make_named_sse_frame(const std::string &event,
                                 const std::string &payload);

} // namespace keen_pbr3

#endif
