#ifdef WITH_API

#include <doctest/doctest.h>

#include "api/status_stream.hpp"

#include <string>

using namespace keen_pbr3;

namespace {

StatusSnapshot make_snapshot(std::string version = "1",
                             size_t outbound_count = 0) {
  StatusSnapshot snapshot;
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
  StatusStream stream([&] { return current; });
  auto subscription = stream.subscribe();

  const auto first = pop(subscription);
  CHECK(first.rfind("event: snapshot\n", 0) == 0);
  CHECK(queued(subscription) == 0);

  current.service.version = "2";
  stream.reconcile();
  CHECK(pop(subscription).rfind("event: service\n", 0) == 0);
}

TEST_CASE(
    "status stream suppresses identical data and names each changed dataset") {
  auto current = make_snapshot();
  StatusStream stream([&] { return current; });
  auto subscription = stream.subscribe();
  (void)pop(subscription);

  stream.reconcile();
  CHECK(queued(subscription) == 0);

  current.service.version = "2";
  current.outbounds = make_snapshot("2", 1).outbounds;
  stream.reconcile();
  CHECK(pop(subscription).rfind("event: service\n", 0) == 0);
  CHECK(pop(subscription).rfind("event: outbounds\n", 0) == 0);
  CHECK(queued(subscription) == 0);
}

TEST_CASE("status stream closes slow and shutdown subscribers") {
  auto current = make_snapshot();
  StatusStream stream([&] { return current; }, 1);
  auto slow = stream.subscribe();
  current.service.version = "2";
  stream.reconcile();
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

#endif
