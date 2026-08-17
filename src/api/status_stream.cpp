#ifdef WITH_API

#include "status_stream.hpp"

#include <nlohmann/json.hpp>

namespace keen_pbr3 {

namespace {

template <typename T> std::string serialize(const T &value) {
  return nlohmann::json(value).dump();
}

std::string make_event_payload(const std::string &type,
                               const std::string &serialized_data) {
  return "{\"data\":" + serialized_data + ",\"type\":\"" + type + "\"}";
}

} // namespace

std::string make_named_sse_frame(const std::string &event,
                                 const std::string &payload) {
  return "event: " + event + "\ndata: " + payload + "\n\n";
}

StatusStream::StatusStream(ServiceBuilder service_builder,
                           OutboundsBuilder outbounds_builder,
                           InterfacesBuilder interfaces_builder,
                           size_t max_queue_size)
    : service_builder_(std::move(service_builder)),
      outbounds_builder_(std::move(outbounds_builder)),
      interfaces_builder_(std::move(interfaces_builder)),
      broadcaster_(max_queue_size) {}

void StatusStream::rebuild(StatusUpdate updates) {
  if (has_status_update(updates, StatusUpdate::Service)) {
    service_ = serialize(service_builder_());
  }
  if (has_status_update(updates, StatusUpdate::Outbounds)) {
    outbounds_ = serialize(outbounds_builder_());
  }
  if (has_status_update(updates, StatusUpdate::Interfaces)) {
    interfaces_ = serialize(interfaces_builder_());
  }
}

SseBroadcaster::SubscriptionPtr StatusStream::subscribe() {
  KPBR_LOCK_GUARD(mutex_);
  if (!initialized_ || !broadcaster_.has_subscribers()) {
    rebuild(StatusUpdate::All);
    initialized_ = true;
  }
  const auto serialized_snapshot = "{\"interfaces\":" + interfaces_ +
                                   ",\"outbounds\":" + outbounds_ +
                                   ",\"service\":" + service_ + "}";
  const auto payload = make_event_payload("snapshot", serialized_snapshot);
  return broadcaster_.subscribe({make_named_sse_frame("snapshot", payload)});
}

void StatusStream::unsubscribe(
    const SseBroadcaster::SubscriptionPtr &subscription) {
  KPBR_LOCK_GUARD(mutex_);
  broadcaster_.unsubscribe(subscription);
  if (!broadcaster_.has_subscribers()) {
    service_.clear();
    outbounds_.clear();
    interfaces_.clear();
    initialized_ = false;
  }
}

void StatusStream::reconcile(StatusUpdate updates) {
  if (updates == StatusUpdate::None) {
    return;
  }
  std::vector<std::string> frames;

  {
    KPBR_LOCK_GUARD(mutex_);
    if (!broadcaster_.has_subscribers()) {
      initialized_ = false;
      return;
    }

    if (has_status_update(updates, StatusUpdate::Service)) {
      const auto service = serialize(service_builder_());
      if (service != service_) {
        service_ = service;
        frames.push_back(make_named_sse_frame(
            "service", make_event_payload("service", service_)));
      }
    }
    if (has_status_update(updates, StatusUpdate::Outbounds)) {
      const auto outbounds = serialize(outbounds_builder_());
      if (outbounds != outbounds_) {
        outbounds_ = outbounds;
        frames.push_back(make_named_sse_frame(
            "outbounds", make_event_payload("outbounds", outbounds_)));
      }
    }
    if (has_status_update(updates, StatusUpdate::Interfaces)) {
      const auto interfaces = serialize(interfaces_builder_());
      if (interfaces != interfaces_) {
        interfaces_ = interfaces;
        frames.push_back(make_named_sse_frame(
            "interfaces", make_event_payload("interfaces", interfaces_)));
      }
    }
  }
  for (const auto &frame : frames) {
    broadcaster_.publish(frame);
  }
}

void StatusStream::close_all() {
  KPBR_LOCK_GUARD(mutex_);
  broadcaster_.close_all();
  service_.clear();
  outbounds_.clear();
  interfaces_.clear();
  initialized_ = false;
}

} // namespace keen_pbr3

#endif
