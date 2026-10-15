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

StatusStream::StatusStream(Builders builders, size_t max_queue_size)
    : builders_(std::move(builders)), broadcaster_(max_queue_size) {}

void StatusStream::set_subscribe_hook(SubscribeHook hook) {
  subscribe_hook_ = std::move(hook);
}

bool StatusStream::has_subscribers() {
  KPBR_LOCK_GUARD(mutex_);
  return broadcaster_.has_subscribers();
}

void StatusStream::rebuild(StatusUpdate updates) {
  if (has_status_update(updates, StatusUpdate::Service)) {
    service_ = serialize(builders_.service());
  }
  if (has_status_update(updates, StatusUpdate::Outbounds)) {
    outbounds_ = serialize(builders_.outbounds());
  }
  if (has_status_update(updates, StatusUpdate::Interfaces)) {
    interfaces_ = serialize(builders_.interfaces());
  }
  if (has_status_update(updates, StatusUpdate::Issues)) {
    issues_ = serialize(builders_.issues());
  }
  if (has_status_update(updates, StatusUpdate::Routing)) {
    routing_ = builders_.routing().dump();
  }
  if (has_status_update(updates, StatusUpdate::Auth)) {
    auth_ = builders_.auth().dump();
  }
}

SseBroadcaster::SubscriptionPtr StatusStream::subscribe() {
  SseBroadcaster::SubscriptionPtr subscription;
  {
    KPBR_LOCK_GUARD(mutex_);
    if (!initialized_ || !broadcaster_.has_subscribers()) {
      rebuild(StatusUpdate::All);
      initialized_ = true;
    }
    const auto serialized_snapshot = "{\"auth\":" + auth_ +
                                     ",\"interfaces\":" + interfaces_ +
                                     ",\"issues\":" + issues_ +
                                     ",\"outbounds\":" + outbounds_ +
                                     ",\"routing\":" + routing_ +
                                     ",\"service\":" + service_ + "}";
    const auto payload = make_event_payload("snapshot", serialized_snapshot);
    subscription =
        broadcaster_.subscribe({make_named_sse_frame("snapshot", payload)});
  }
  if (subscribe_hook_) subscribe_hook_();
  return subscription;
}

void StatusStream::unsubscribe(
    const SseBroadcaster::SubscriptionPtr &subscription) {
  KPBR_LOCK_GUARD(mutex_);
  broadcaster_.unsubscribe(subscription);
  if (!broadcaster_.has_subscribers()) {
    service_.clear();
    outbounds_.clear();
    interfaces_.clear();
    issues_.clear();
    routing_.clear();
    auth_.clear();
    initialized_ = false;
  }
}

void StatusStream::reconcile(StatusUpdate updates) {
  if (updates == StatusUpdate::None) {
    return;
  }
  // The issues are derived from the service and the outbounds.
  if (has_status_update(updates, StatusUpdate::Service) ||
      has_status_update(updates, StatusUpdate::Outbounds)) {
    updates = updates | StatusUpdate::Issues;
  }
  std::vector<std::string> frames;

  {
    KPBR_LOCK_GUARD(mutex_);
    if (!broadcaster_.has_subscribers()) {
      initialized_ = false;
      return;
    }

    if (has_status_update(updates, StatusUpdate::Service)) {
      const auto service = serialize(builders_.service());
      if (service != service_) {
        service_ = service;
        frames.push_back(make_named_sse_frame(
            "service", make_event_payload("service", service_)));
      }
    }
    if (has_status_update(updates, StatusUpdate::Outbounds)) {
      const auto outbounds = serialize(builders_.outbounds());
      if (outbounds != outbounds_) {
        outbounds_ = outbounds;
        frames.push_back(make_named_sse_frame(
            "outbounds", make_event_payload("outbounds", outbounds_)));
      }
    }
    if (has_status_update(updates, StatusUpdate::Interfaces)) {
      const auto interfaces = serialize(builders_.interfaces());
      if (interfaces != interfaces_) {
        interfaces_ = interfaces;
        frames.push_back(make_named_sse_frame(
            "interfaces", make_event_payload("interfaces", interfaces_)));
      }
    }
    if (has_status_update(updates, StatusUpdate::Issues)) {
      const auto issues = serialize(builders_.issues());
      if (issues != issues_) {
        issues_ = issues;
        frames.push_back(make_named_sse_frame(
            "issues", make_event_payload("issues", issues_)));
      }
    }
    if (has_status_update(updates, StatusUpdate::Routing)) {
      const auto routing = builders_.routing().dump();
      if (routing != routing_) {
        routing_ = routing;
        frames.push_back(make_named_sse_frame(
            "routing", make_event_payload("routing", routing_)));
      }
    }
    if (has_status_update(updates, StatusUpdate::Auth)) {
      const auto auth = builders_.auth().dump();
      if (auth != auth_) {
        auth_ = auth;
        frames.push_back(
            make_named_sse_frame("auth", make_event_payload("auth", auth_)));
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
  issues_.clear();
  routing_.clear();
  auth_.clear();
  initialized_ = false;
}

} // namespace keen_pbr3

#endif
