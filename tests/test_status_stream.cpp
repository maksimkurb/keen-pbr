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
  api::HealthIssuesResponse issues;
  nlohmann::json routing = {{"overall", "ok"}};
  nlohmann::json auth = {{"enabled", false}, {"device_name", ""}};
  nlohmann::json config = {{"revision", 1}};
};

StatusStream::Builders builders_for(TestStatusSnapshot &current) {
  return {[&] { return current.service; },
          [&] { return current.outbounds; },
          [&] { return current.interfaces; },
          [&] { return current.issues; },
          [&] { return current.routing; },
          [&] { return current.auth; },
          [&] { return current.config; }};
}

TestStatusSnapshot make_snapshot(std::string version = "1",
                                 size_t outbound_count = 0) {
  TestStatusSnapshot snapshot;
  snapshot.service.version = std::move(version);
  snapshot.service.build = "test";
  snapshot.service.status = api::HealthResponseStatus::RUNNING;
  snapshot.service.os_type = "linux";
  snapshot.service.os_version = "test";
  snapshot.service.build_variant = "test";
  snapshot.service.config_is_draft = false;
  snapshot.outbounds.outbounds.resize(outbound_count);
  for (size_t i = 0; i < outbound_count; ++i) {
    auto &outbound = snapshot.outbounds.outbounds[i];
    outbound.tag = "outbound" + std::to_string(i);
    outbound.type = api::OutboundType::INTERFACE;
    outbound.status = api::RuntimeOutboundStatusEnum::HEALTHY;
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
  StatusStream stream(builders_for(current));
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
  StatusStream stream(builders_for(current));
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
  StatusStream stream(builders_for(current), 1);
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
  auto builders = builders_for(current);
  builders.service = [&] {
    ++service_builds;
    return current.service;
  };
  builders.outbounds = [&] {
    ++outbound_builds;
    return current.outbounds;
  };
  builders.interfaces = [&] {
    ++interface_builds;
    return current.interfaces;
  };
  StatusStream stream(std::move(builders));

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

TEST_CASE("status stream snapshot carries the issues") {
  auto current = make_snapshot();
  api::HealthIssue issue{};
  issue.key = "test-issue";
  current.issues.issues.push_back(issue);
  StatusStream stream(builders_for(current));
  auto subscription = stream.subscribe();
  const auto snapshot = pop(subscription);
  CHECK(snapshot.find("\"issues\":{\"issues\":[{") != std::string::npos);
  CHECK(snapshot.find("test-issue") != std::string::npos);
}

TEST_CASE("status stream emits issues only when the derived issues change") {
  auto current = make_snapshot();
  // Issues derive from the outbounds: one issue per outbound that is not
  // healthy.
  const auto build_issues = [&] {
    api::HealthIssuesResponse issues;
    for (const auto &outbound : current.outbounds.outbounds) {
      if (outbound.status != api::RuntimeOutboundStatusEnum::HEALTHY) {
        api::HealthIssue issue{};
        issue.key = "outbound-" + outbound.tag;
        issues.issues.push_back(issue);
      }
    }
    return issues;
  };
  auto builders = builders_for(current);
  builders.issues = build_issues;
  StatusStream stream(std::move(builders));
  auto subscription = stream.subscribe();
  (void)pop(subscription);

  // Outbounds change, issues do not: no issues event.
  current.outbounds = make_snapshot("1", 1).outbounds;
  stream.reconcile(StatusUpdate::Outbounds);
  CHECK(pop(subscription).rfind("event: outbounds\n", 0) == 0);
  CHECK(queued(subscription) == 0);

  // Outbounds change and so do the issues.
  current.outbounds.outbounds[0].status =
      api::RuntimeOutboundStatusEnum::UNAVAILABLE;
  stream.reconcile(StatusUpdate::Outbounds);
  CHECK(pop(subscription).rfind("event: outbounds\n", 0) == 0);
  CHECK(pop(subscription).rfind("event: issues\n", 0) == 0);
  CHECK(queued(subscription) == 0);

  // A service change is enough to re-evaluate the issues.
  current.service.version = "2";
  stream.reconcile(StatusUpdate::Service);
  CHECK(pop(subscription).rfind("event: service\n", 0) == 0);
  CHECK(queued(subscription) == 0);
}

TEST_CASE("status stream issues-only reconcile runs only the issues builder") {
  auto current = make_snapshot();
  int service_builds = 0;
  int outbound_builds = 0;
  int issue_builds = 0;
  auto builders = builders_for(current);
  builders.service = [&] {
    ++service_builds;
    return current.service;
  };
  builders.outbounds = [&] {
    ++outbound_builds;
    return current.outbounds;
  };
  builders.issues = [&] {
    ++issue_builds;
    return current.issues;
  };
  StatusStream stream(std::move(builders));
  auto subscription = stream.subscribe();
  (void)pop(subscription);
  const int service_before = service_builds;
  const int outbound_before = outbound_builds;

  api::HealthIssue issue{};
  issue.key = "routing";
  current.issues.issues.push_back(issue);
  stream.reconcile(StatusUpdate::Issues);
  CHECK(pop(subscription).rfind("event: issues\n", 0) == 0);
  CHECK(issue_builds == 2);
  CHECK(service_builds == service_before);
  CHECK(outbound_builds == outbound_before);

  stream.reconcile(StatusUpdate::Issues);
  CHECK(queued(subscription) == 0);
}

TEST_CASE("status stream snapshot carries the routing report") {
  auto current = make_snapshot();
  current.routing = {{"overall", "degraded"}, {"firewall_backend", "nftables"}};
  StatusStream stream(builders_for(current));
  auto subscription = stream.subscribe();
  const auto snapshot = pop(subscription);
  CHECK(snapshot.find("\"routing\":{\"firewall_backend\":\"nftables\","
                      "\"overall\":\"degraded\"}") != std::string::npos);
}

TEST_CASE("status stream emits routing only when the report changes") {
  auto current = make_snapshot();
  int routing_builds = 0;
  int service_builds = 0;
  auto builders = builders_for(current);
  builders.routing = [&] {
    ++routing_builds;
    return current.routing;
  };
  builders.service = [&] {
    ++service_builds;
    return current.service;
  };
  StatusStream stream(std::move(builders));
  auto subscription = stream.subscribe();
  (void)pop(subscription);
  const int service_before = service_builds;

  // The worker completing with an identical report publishes nothing.
  stream.reconcile(StatusUpdate::Routing | StatusUpdate::Issues);
  CHECK(queued(subscription) == 0);
  CHECK(routing_builds == 2);

  current.routing = {{"overall", "degraded"}};
  stream.reconcile(StatusUpdate::Routing | StatusUpdate::Issues);
  const auto frame = pop(subscription);
  CHECK(frame.rfind("event: routing\n", 0) == 0);
  CHECK(frame.find("\"data\":{\"overall\":\"degraded\"}") != std::string::npos);
  CHECK(queued(subscription) == 0);
  // Routing is independent of the service dataset.
  CHECK(service_builds == service_before);

  // Service/Outbounds updates alone do not rebuild the routing report.
  stream.reconcile(StatusUpdate::Service);
  CHECK(routing_builds == 3);
}

TEST_CASE("status stream reports subscribers and runs the subscribe hook") {
  auto current = make_snapshot();
  StatusStream stream(builders_for(current));
  int hook_calls = 0;
  stream.set_subscribe_hook([&] { ++hook_calls; });
  CHECK_FALSE(stream.has_subscribers());
  auto subscription = stream.subscribe();
  CHECK(hook_calls == 1);
  CHECK(stream.has_subscribers());
  stream.unsubscribe(subscription);
  CHECK_FALSE(stream.has_subscribers());
}

TEST_CASE("status stream carries the auth state and emits only changes") {
  auto current = make_snapshot();
  current.auth = {{"enabled", true}, {"device_name", "Home"}};
  StatusStream stream(builders_for(current));
  auto subscription = stream.subscribe();
  const auto snapshot = pop(subscription);
  CHECK(snapshot.find("\"auth\":{\"device_name\":\"Home\","
                      "\"enabled\":true}") != std::string::npos);

  stream.reconcile(StatusUpdate::Auth);
  CHECK(queued(subscription) == 0);

  current.auth = {{"enabled", true}, {"device_name", "Office"}};
  stream.reconcile(StatusUpdate::Auth);
  const auto frame = pop(subscription);
  CHECK(frame.rfind("event: auth\n", 0) == 0);
  CHECK(frame.find("\"device_name\":\"Office\"") != std::string::npos);
  CHECK(queued(subscription) == 0);
}

TEST_CASE("status stream carries the config revision and emits each change") {
  auto current = make_snapshot();
  current.config = {{"revision", 7}};
  StatusStream stream(builders_for(current));
  auto subscription = stream.subscribe();
  CHECK(pop(subscription).find("\"config\":{\"revision\":7}") !=
        std::string::npos);

  stream.reconcile(StatusUpdate::Config);
  CHECK(queued(subscription) == 0);

  current.config = {{"revision", 8}};
  stream.reconcile(StatusUpdate::Config);
  const auto frame = pop(subscription);
  CHECK(frame.rfind("event: config\n", 0) == 0);
  CHECK(frame.find("\"data\":{\"revision\":8}") != std::string::npos);
  CHECK(queued(subscription) == 0);
}

#endif
