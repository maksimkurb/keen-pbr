#pragma once

#ifdef WITH_API

#include "generated/api_types.hpp"
#include "sse_broadcaster.hpp"

#include "../util/traced_mutex.hpp"

#include <functional>
#include <string>

namespace keen_pbr3 {

struct StatusSnapshot {
  api::HealthResponse service;
  api::RuntimeOutboundsResponse outbounds;
  api::RuntimeInterfaceInventoryResponse interfaces;
};

class StatusStream {
public:
  using SnapshotBuilder = std::function<StatusSnapshot()>;

  explicit StatusStream(SnapshotBuilder builder, size_t max_queue_size = 128);

  SseBroadcaster::SubscriptionPtr subscribe();
  void unsubscribe(const SseBroadcaster::SubscriptionPtr &subscription);
  void reconcile();
  void close_all();

private:
  SnapshotBuilder builder_;
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
