#ifdef WITH_API

#include <doctest/doctest.h>

#include "api/status_stream.hpp"

#include <string>

using namespace keen_pbr3;

namespace {

struct TestStatusSnapshot {
  api::HealthResponse service;
  api::RuntimeOutboundsResponse outbounds;
  api::RuntimeInterfaceInventoryResponse interfaces;
};

TestStatusSnapshot make_snapshot(std::string version = "1",
                                 size_t outbound_count = 0) {
  TestStatusSnapshot snapshot;
  snapshot.service.version = std::move(version);
  snapshot.service.build = "test";
  snapshot.service.status = api::HealthResponseStatus::RUNNING;
  snapshot.service.os_type = "linux";
  snapshot.service.os_version = "test";
  snapshot.service.build_variant = "test";
  snapshot.service.resolver_live_status = api::ResolverLiveStatus::HEALTHY;
  snapshot.service.config_is_draft = false;
  snapshot.outbounds.outbounds.resize(outbound_count);
  for (size_t i = 0; i < outbound_count; ++i) {
    auto &outbound = snapshot.outbounds.outbounds[i];
    outbound.tag = "outbound" + std::to_string(i);
    outbound.type = api::OutboundType::INTERFACE;
    outbound.status = api::ResolverLiveStatus::HEALTHY;
  }
  return snapshot;
}

std::string pop(const SseBroadcaster::SubscriptionPtr &subscription) {
  KPBR_LOCK_GUARD(subscription->mutex);
  REQUIRE_FALSE(subscription->messages.empty());
  auto value = std::move(subscription->messages.front());
  subscription->messages.pop_front();
  return value;
}

size_t queued(const SseBroadcaster::SubscriptionPtr &subscription) {
  KPBR_LOCK_GUARD(subscription->mutex);
  return subscription->messages.size();
}

} // namespace

TEST_CASE("status stream queues one snapshot before changes") {
  auto current = make_snapshot();
  StatusStream stream([&] { return current.service; },
                      [&] { return current.outbounds; },
                      [&] { return current.interfaces; });
  auto subscription = stream.subscribe();

  const auto first = pop(subscription);
  CHECK(first.rfind("event: snapshot\n", 0) == 0);
  CHECK(queued(subscription) == 0);

  current.service.version = "2";
  stream.reconcile(StatusUpdate::Service);
  CHECK(pop(subscription).rfind("event: service\n", 0) == 0);
}

TEST_CASE(
    "status stream suppresses identical data and names each changed dataset") {
  auto current = make_snapshot();
  StatusStream stream([&] { return current.service; },
                      [&] { return current.outbounds; },
                      [&] { return current.interfaces; });
  auto subscription = stream.subscribe();
  (void)pop(subscription);

  stream.reconcile(StatusUpdate::All);
  CHECK(queued(subscription) == 0);

  current.service.version = "2";
  current.outbounds = make_snapshot("2", 1).outbounds;
  stream.reconcile(StatusUpdate::Service | StatusUpdate::Outbounds);
  CHECK(pop(subscription).rfind("event: service\n", 0) == 0);
  CHECK(pop(subscription).rfind("event: outbounds\n", 0) == 0);
  CHECK(queued(subscription) == 0);
}

TEST_CASE("status stream closes slow and shutdown subscribers") {
  auto current = make_snapshot();
  StatusStream stream([&] { return current.service; },
                      [&] { return current.outbounds; },
                      [&] { return current.interfaces; }, 1);
  auto slow = stream.subscribe();
  current.service.version = "2";
  stream.reconcile(StatusUpdate::Service);
  {
    KPBR_LOCK_GUARD(slow->mutex);
    CHECK(slow->closed);
  }

  auto active = stream.subscribe();
  stream.close_all();
  {
    KPBR_LOCK_GUARD(active->mutex);
    CHECK(active->closed);
  }
}

TEST_CASE("status stream does no work without subscribers") {
  auto current = make_snapshot();
  int service_builds = 0;
  int outbound_builds = 0;
  int interface_builds = 0;
  StatusStream stream(
      [&] {
        ++service_builds;
        return current.service;
      },
      [&] {
        ++outbound_builds;
        return current.outbounds;
      },
      [&] {
        ++interface_builds;
        return current.interfaces;
      });

  stream.reconcile(StatusUpdate::All);
  CHECK(service_builds == 0);
  CHECK(outbound_builds == 0);
  CHECK(interface_builds == 0);

  auto first = stream.subscribe();
  CHECK(service_builds == 1);
  CHECK(outbound_builds == 1);
  CHECK(interface_builds == 1);
  (void)pop(first);

  auto second = stream.subscribe();
  CHECK(service_builds == 1);
  CHECK(outbound_builds == 1);
  CHECK(interface_builds == 1);
  (void)pop(second);

  current.service.version = "2";
  stream.reconcile(StatusUpdate::Service);
  CHECK(service_builds == 2);
  CHECK(outbound_builds == 1);
  CHECK(interface_builds == 1);

  stream.unsubscribe(first);
  stream.unsubscribe(second);
  current.service.version = "3";
  stream.reconcile(StatusUpdate::All);
  CHECK(service_builds == 2);
  CHECK(outbound_builds == 1);
  CHECK(interface_builds == 1);

  auto replacement = stream.subscribe();
  CHECK(service_builds == 3);
  CHECK(outbound_builds == 2);
  CHECK(interface_builds == 2);
  CHECK(pop(replacement).find("\"version\":\"3\"") != std::string::npos);
}

#endif
