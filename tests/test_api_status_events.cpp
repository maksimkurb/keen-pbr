#ifdef WITH_API

#include <doctest/doctest.h>
#include <httplib.h>

#include "api/handler_status_events.hpp"
#include "api/handler_health_service.hpp"
#include "api/handlers.hpp"
#include "api/server.hpp"
#include "api/status_stream.hpp"

namespace keen_pbr3 {

namespace {

ApiContext make_status_context(SseBroadcaster &broadcaster,
                               StatusStream &stream) {
  static const std::string config_path = "/tmp/keen-pbr-status-test.json";
  return ApiContext{
      config_path,
      broadcaster,
      [] { return Config{}; },
      [] { return false; },
      [](Config, std::string) {},
      []() -> std::optional<std::pair<Config, std::string>> {
        return std::nullopt;
      },
      [] {},
      [](const Config &) {},
      [] { return ServiceHealthState{}; },
      [] { return RoutingHealthReport{}; },
      [] { return api::RuntimeOutboundsResponse{}; },
      [] { return api::RuntimeInterfaceInventoryResponse{}; },
      [](const Config &) {
        return std::map<std::string, api::ListRefreshStateValue>{};
      },
      [](const std::string &) { return TestRoutingResult{}; },
      [] {},
      [] {},
      [](Config, std::string) { return ConfigApplyResult{}; },
      [] {},
      [] {},
      [] {},
      [](std::optional<std::string>) { return ListRefreshOperationResult{}; },
      &stream,
  };
}

StatusSnapshot api_snapshot() {
  StatusSnapshot snapshot;
  snapshot.service.version = "test";
  snapshot.service.build = "test";
  snapshot.service.status = api::HealthResponseStatus::RUNNING;
  snapshot.service.os_type = "linux";
  snapshot.service.os_version = "test";
  snapshot.service.build_variant = "test";
  snapshot.service.resolver_live_status = api::ResolverLiveStatus::HEALTHY;
  snapshot.service.config_is_draft = false;
  return snapshot;
}

} // namespace

TEST_CASE("status events endpoint returns SSE headers and snapshot first") {
  SseBroadcaster dns_broadcaster;
  StatusStream status_stream([] { return api_snapshot(); });
  auto context = make_status_context(dns_broadcaster, status_stream);
  ApiConfig config;
  config.listen = std::string("127.0.0.1:18193");
  ApiServer server(config);
  register_status_events_handler(server, context);
  server.start();

  int status = 0;
  std::string content_type;
  std::string body;
  httplib::Client client("127.0.0.1", 18193);
  (void)client.Get(
      "/api/status/events",
      [&status, &content_type](const httplib::Response &response) {
        status = response.status;
        content_type = response.get_header_value("Content-Type");
        return true;
      },
      [&body](const char *data, size_t length) {
        body.append(data, length);
        return false;
      });
  server.stop();

  CHECK(status == 200);
  CHECK(content_type.find("text/event-stream") != std::string::npos);
  CHECK(body.rfind("event: snapshot\ndata: ", 0) == 0);
}

TEST_CASE("health response includes lifecycle operation for SSE snapshots") {
  ServiceHealthState health;
  health.status = api::HealthResponseStatus::RUNNING;
  health.runtime_state = "running";
  health.lifecycle_operation = LifecycleOperationSnapshot{
      "restart-1",
      LifecycleOperationType::Restart,
      LifecycleOperationResult::Running,
      123,
      std::nullopt,
      "",
      {{"stop_routing", "Stopping routing and firewall", LifecycleOperationStatus::Succeeded, ""},
       {"start_routing", "Starting routing and firewall", LifecycleOperationStatus::Running, ""}},
  };

  const api::HealthResponse response = build_health_response(health);
  REQUIRE(response.lifecycle_operation.has_value());
  CHECK(response.lifecycle_operation->id == "restart-1");
  CHECK(response.lifecycle_operation->type == api::LifecycleOperationType::RESTART);
  CHECK(response.lifecycle_operation->status == api::LifecycleOperationStatus::RUNNING);
  REQUIRE(response.lifecycle_operation->stages.size() == 2);
  CHECK(response.lifecycle_operation->stages[1].status ==
        api::LifecycleOperationStageStatus::RUNNING);
}

} // namespace keen_pbr3

#endif
