#ifdef WITH_API

#include <doctest/doctest.h>
#include <httplib.h>
#include <nlohmann/json.hpp>

#include "../src/api/handler_dns_check.hpp"
#include "../src/api/server.hpp"
#include "../src/api/sse_broadcaster.hpp"

namespace keen_pbr3 {

namespace {

const std::string kApiConfigPath = "/tmp/keen-pbr-test-config.json";

ApiContext make_check_context(SseBroadcaster& broadcaster) {
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

TEST_CASE("register_dns_check_handler: validates the request and maps the daemon's errors") {
    SseBroadcaster broadcaster;
    ApiConfig api_config;
    api_config.listen = std::string("127.0.0.1:18193");
    ApiServer server(api_config);
    auto ctx = make_check_context(broadcaster);
    DnsCheckRequest seen;
    int mode = 0;
    ctx.check_dns_servers_fn = [&](const DnsCheckRequest& request) -> DnsCheckReport {
        seen = request;
        if (mode == 1) throw ApiError("A DNS server check is already running", 409);
        if (mode == 2) throw DnsCheckRequestError("unknown DNS server 'x'");
        DnsCheckReport report;
        report.overall = DnsCheckSeverity::Warning;
        return report;
    };
    register_dns_check_handler(server, ctx);
    server.start();

    httplib::Client client("127.0.0.1", 18193);
    const auto ok = client.Post("/api/dns/servers/check",
                                R"({"domains":["Youtube.com"],"custom_domains":["example.org"],"servers":["a"]})",
                                "application/json");
    const auto empty_body = client.Post("/api/dns/servers/check", "", "application/json");
    const auto too_many = client.Post(
        "/api/dns/servers/check",
        R"({"domains":["a.ru","b.ru","c.ru","d.ru","e.ru","f.ru"]})", "application/json");
    const auto invalid_json = client.Post("/api/dns/servers/check", "{", "application/json");
    mode = 1;
    const auto busy = client.Post("/api/dns/servers/check", "{}", "application/json");
    mode = 2;
    const auto unknown = client.Post("/api/dns/servers/check", R"({"servers":["x"]})", "application/json");
    server.stop();

    REQUIRE(ok != nullptr);
    CHECK(ok->status == 200);
    const auto body = nlohmann::json::parse(ok->body);
    CHECK(body["overall"] == "warning");
    CHECK(body["servers"].is_array());
    REQUIRE(empty_body != nullptr);
    CHECK(empty_body->status == 200);
    CHECK(seen.domains == std::vector<std::string>{"yandex.ru"});
    REQUIRE(too_many != nullptr);
    CHECK(too_many->status == 400);
    REQUIRE(invalid_json != nullptr);
    CHECK(invalid_json->status == 400);
    CHECK(nlohmann::json::parse(invalid_json->body)["error"] == "Invalid request body");
    REQUIRE(busy != nullptr);
    CHECK(busy->status == 409);
    REQUIRE(unknown != nullptr);
    CHECK(unknown->status == 400);
}

TEST_CASE("register_dns_check_handler: 503 where the daemon serves no check") {
    SseBroadcaster broadcaster;
    ApiConfig api_config;
    api_config.listen = std::string("127.0.0.1:18194");
    ApiServer server(api_config);
    auto ctx = make_check_context(broadcaster);
    register_dns_check_handler(server, ctx);
    server.start();
    httplib::Client client("127.0.0.1", 18194);
    const auto response = client.Post("/api/dns/servers/check", "{}", "application/json");
    server.stop();
    REQUIRE(response != nullptr);
    CHECK(response->status == 503);
}

} // namespace keen_pbr3

#endif // WITH_API
