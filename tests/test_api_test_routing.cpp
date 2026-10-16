#ifdef WITH_API

#include <doctest/doctest.h>
#include <httplib.h>
#include <nlohmann/json.hpp>

#include "../src/api/handler_test_routing.hpp"
#include "../src/api/server.hpp"
#include "../src/api/sse_broadcaster.hpp"

namespace keen_pbr3 {

namespace {

const std::string kApiConfigPath = "/tmp/keen-pbr-test-config.json";
constexpr const char* kApiListen = "127.0.0.1:18190";

ApiContext make_test_api_context(SseBroadcaster& broadcaster) {
    return ApiContext{
        kApiConfigPath,
        broadcaster,
        []() { return Config{}; },
        []() { return false; },
        [](Config) {},
        []() -> std::optional<StagedConfigSnapshot> { return std::nullopt; },
        []() {},
        [](const Config&) {},
        []() { return ServiceHealthState{}; },
        []() { return RoutingHealthReport{}; },
        []() { return api::RuntimeOutboundsResponse{}; },
        []() { return api::RuntimeInterfaceInventoryResponse{}; },
        [](const Config&) { return std::map<std::string, api::ListRefreshStateValue>{}; },
        [](const std::string& target) {
            TestRoutingResult result;
            result.target = target;
            return result;
        },
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

TEST_CASE("register_test_routing_handler: rejects empty target") {
    SseBroadcaster broadcaster;
    ApiConfig api_config;
    api_config.listen = std::string(kApiListen);

    ApiServer server(api_config);
    auto ctx = make_test_api_context(broadcaster);
    register_test_routing_handler(server, ctx);

    server.start();

    httplib::Client client("127.0.0.1", 18190);
    const auto response =
        client.Post("/api/routing/test", R"({"target":""})", "application/json");
    server.stop();

    REQUIRE(response != nullptr);
    CHECK(response->status == 400);

    const auto body = nlohmann::json::parse(response->body);
    CHECK(body["error"] == "Field 'target' must not be empty");
}

TEST_CASE("register_test_routing_handler: validates packet criteria") {
    SseBroadcaster broadcaster;
    ApiConfig api_config;
    api_config.listen = std::string("127.0.0.1:18191");

    ApiServer server(api_config);
    auto ctx = make_test_api_context(broadcaster);
    register_test_routing_handler(server, ctx);
    server.start();

    httplib::Client client("127.0.0.1", 18191);
    const auto response = client.Post(
        "/api/routing/test",
        R"({"target":"203.0.113.10","proto":"tcp","dest_port":0})",
        "application/json");
    const auto fractional_port = client.Post(
        "/api/routing/test",
        R"({"target":"203.0.113.10","dest_port":443.5})",
        "application/json");
    const auto fractional_dscp = client.Post(
        "/api/routing/test",
        R"({"target":"203.0.113.10","dscp":0.5})",
        "application/json");
    const auto string_port = client.Post(
        "/api/routing/test",
        R"({"target":"203.0.113.10","src_port":"443"})",
        "application/json");
    const auto huge_port = client.Post(
        "/api/routing/test",
        R"({"target":"203.0.113.10","dest_port":18446744073709551615})",
        "application/json");
    const auto huge_dscp = client.Post(
        "/api/routing/test",
        R"({"target":"203.0.113.10","dscp":18446744073709551615})",
        "application/json");
    const auto mismatched_family = client.Post(
        "/api/routing/test",
        R"({"target":"203.0.113.10","src_addr":"2001:db8::10"})",
        "application/json");
    server.stop();

    REQUIRE(response != nullptr);
    REQUIRE(fractional_port != nullptr);
    REQUIRE(fractional_dscp != nullptr);
    REQUIRE(string_port != nullptr);
    REQUIRE(huge_port != nullptr);
    REQUIRE(huge_dscp != nullptr);
    REQUIRE(mismatched_family != nullptr);
    CHECK(response->status == 400);
    CHECK(fractional_port->status == 400);
    CHECK(fractional_dscp->status == 400);
    CHECK(string_port->status == 400);
    CHECK(huge_port->status == 400);
    CHECK(huge_dscp->status == 400);
    CHECK(mismatched_family->status == 400);
    const auto body = nlohmann::json::parse(response->body);
    CHECK(body["error"] == "Field 'dest_port' must be between 1 and 65535");
}

TEST_CASE("register_test_routing_handler: forwards packet criteria") {
    SseBroadcaster broadcaster;
    ApiConfig api_config;
    api_config.listen = std::string("127.0.0.1:18192");

    ApiServer server(api_config);
    auto ctx = make_test_api_context(broadcaster);
    bool called = false;
    ctx.compute_test_routing_with_criteria_fn =
        [&called](const std::string& target, const TestRoutingCriteria& criteria) {
            called = true;
            CHECK(target == "203.0.113.10");
            CHECK(criteria.proto == std::optional<std::string>("tcp"));
            CHECK(criteria.dest_port == std::optional<uint16_t>(443));
            CHECK(criteria.src_addr == std::optional<std::string>("192.168.1.10"));
            CHECK(criteria.src_port == std::optional<uint16_t>(51514));
            CHECK(criteria.dscp == std::optional<uint8_t>(46));

            TestRoutingResult result;
            result.target = target;
            TestRoutingEntry entry;
            entry.ip = target;
            entry.expected_outbound = "vpn";
            entry.actual_outbound = "(unknown)";
            entry.matched_rule_index = 2;
            entry.criteria_match = true;
            entry.ok = false;
            result.entries.push_back(std::move(entry));
            RuleDiagnostic diagnostic;
            diagnostic.rule_index = 2;
            diagnostic.rule.outbound = "vpn";
            RuleIpDiagnostic ip_diagnostic;
            ip_diagnostic.ip = target;
            ip_diagnostic.in_lists = true;
            ip_diagnostic.set_write_evidence = SetWriteEvidence{
                SetWriteEvidenceStatus::Recorded, std::optional<uint64_t>(12)};
            diagnostic.ip_rows.push_back(std::move(ip_diagnostic));
            result.rule_diagnostics.push_back(std::move(diagnostic));
            return result;
        };
    register_test_routing_handler(server, ctx);
    server.start();

    httplib::Client client("127.0.0.1", 18192);
    const auto response = client.Post(
        "/api/routing/test",
        R"({"target":"203.0.113.10","proto":"tcp","dest_port":443,"src_addr":"192.168.1.10","src_port":51514,"dscp":46})",
        "application/json");
    server.stop();

    REQUIRE(response != nullptr);
    CHECK(response->status == 200);
    CHECK(called);
    const auto body = nlohmann::json::parse(response->body);
    CHECK(body["results"][0]["matched_rule_index"] == 2);
    CHECK(body["results"][0]["criteria_match"] == true);
    CHECK(body["rule_diagnostics"][0]["ip_rows"][0]["set_write_evidence"]["status"] ==
          "recorded");
    CHECK(body["rule_diagnostics"][0]["ip_rows"][0]["set_write_evidence"]["age_seconds"] == 12);
}

} // namespace keen_pbr3

#endif // WITH_API
