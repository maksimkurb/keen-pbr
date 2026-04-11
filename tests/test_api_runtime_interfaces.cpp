#ifdef WITH_API

#include <doctest/doctest.h>
#include <httplib.h>
#include <nlohmann/json.hpp>

#include "../src/api/handler_runtime_interfaces.hpp"
#include "../src/api/server.hpp"
#include "../src/api/sse_broadcaster.hpp"

namespace keen_pbr3 {

namespace {

const std::string kApiConfigPath = "/tmp/keen-pbr-test-config.json";
constexpr const char* kApiListen = "127.0.0.1:18189";

ApiContext make_test_api_context(
    SseBroadcaster& broadcaster,
    api::RuntimeInterfaceInventoryResponse runtime_interfaces) {
    return ApiContext{
        kApiConfigPath,
        broadcaster,
        []() { return Config{}; },
        []() { return false; },
        [](Config, std::string) {},
        []() -> std::optional<std::pair<Config, std::string>> { return std::nullopt; },
        []() {},
        [](const Config&) {},
        []() { return ServiceHealthState{}; },
        []() { return RoutingHealthReport{}; },
        []() { return api::RuntimeOutboundsResponse{}; },
        [runtime_interfaces]() { return runtime_interfaces; },
        [](const Config&) { return std::map<std::string, api::ListRefreshStateValue>{}; },
        [](const std::string&) { return TestRoutingResult{}; },
        []() {},
        []() {},
        [](Config, std::string) { return ConfigApplyResult{}; },
        []() {},
        []() {},
        []() {},
        [](std::optional<std::string>) { return ListRefreshOperationResult{}; },
    };
}

} // namespace

TEST_CASE("register_runtime_interfaces_handler: GET /api/runtime/interfaces returns interface inventory") {
    api::RuntimeInterfaceInventoryResponse runtime_interfaces;
    api::RuntimeInterfaceInventoryEntry entry;
    entry.name = "br0";
    entry.status = api::RuntimeInterfaceInventoryStatusEnum::UP;
    entry.admin_up = true;
    entry.oper_state = std::string("up");
    entry.carrier = true;
    entry.ipv4_addresses = std::vector<std::string>{"192.168.1.1/24"};
    runtime_interfaces.interfaces.push_back(entry);

    SseBroadcaster broadcaster;
    ApiConfig api_config;
    api_config.listen = std::string(kApiListen);

    ApiServer server(api_config);
    auto ctx = make_test_api_context(broadcaster, runtime_interfaces);
    register_runtime_interfaces_handler(server, ctx);

    server.start();

    httplib::Client client("127.0.0.1", 18189);
    const auto response = client.Get("/api/runtime/interfaces");
    server.stop();

    REQUIRE(response != nullptr);
    CHECK(response->status == 200);

    const auto body = nlohmann::json::parse(response->body);
    REQUIRE(body.contains("interfaces"));
    REQUIRE(body["interfaces"].is_array());
    REQUIRE(body["interfaces"].size() == 1);
    CHECK(body["interfaces"][0]["name"] == "br0");
    CHECK(body["interfaces"][0]["status"] == "up");
    CHECK(body["interfaces"][0]["admin_up"] == true);
    CHECK(body["interfaces"][0]["oper_state"] == "up");
    CHECK(body["interfaces"][0]["carrier"] == true);
    CHECK(body["interfaces"][0]["ipv4_addresses"] == nlohmann::json::array({"192.168.1.1/24"}));
}

} // namespace keen_pbr3

#endif // WITH_API
