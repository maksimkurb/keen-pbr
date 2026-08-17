#pragma once

#ifdef WITH_API

#include "generated/api_types.hpp"
#include "sse_broadcaster.hpp"

#include "../util/traced_mutex.hpp"

#include <cstdint>
#include <functional>
#include <string>

namespace keen_pbr3 {

enum class StatusUpdate : std::uint8_t {
  None = 0,
  Service = 1U << 0U,
  Outbounds = 1U << 1U,
  Interfaces = 1U << 2U,
  All = (1U << 0U) | (1U << 1U) | (1U << 2U),
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

  StatusStream(ServiceBuilder service_builder,
               OutboundsBuilder outbounds_builder,
               InterfacesBuilder interfaces_builder,
               size_t max_queue_size = 128);

  SseBroadcaster::SubscriptionPtr subscribe();
  void unsubscribe(const SseBroadcaster::SubscriptionPtr &subscription);
  void reconcile(StatusUpdate updates);
  void close_all();

private:
  void rebuild(StatusUpdate updates) REQUIRES(mutex_);

  ServiceBuilder service_builder_;
  OutboundsBuilder outbounds_builder_;
  InterfacesBuilder interfaces_builder_;
  SseBroadcaster broadcaster_;
  TracedMutex mutex_;
  std::string service_ GUARDED_BY(mutex_);
  std::string outbounds_ GUARDED_BY(mutex_);
  std::string interfaces_ GUARDED_BY(mutex_);
  bool initialized_ GUARDED_BY(mutex_){false};
};

std::string make_named_sse_frame(const std::string &event,
                                 const std::string &payload);

} // namespace keen_pbr3

#endif
