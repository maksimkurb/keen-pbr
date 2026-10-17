#ifdef WITH_API

#include <doctest/doctest.h>
#include <httplib.h>
#include <nlohmann/json.hpp>

#include "../src/api/handler_keenetic_dns.hpp"
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

KeeneticDnsCacheView make_view() {
    KeeneticDnsSnapshot snapshot;
    KeeneticDnsProxy system;
    system.name = "System";
    system.upstreams.push_back({"DoT", "8.8.8.8@dns.google"});
    system.upstreams.push_back({"DoH", "https://dns.example/dns-query"});
    system.domain_servers.push_back({"beeline.ru", {"85.21.192.5", "213.234.192.7"}});
    snapshot.proxies = {system};

    KeeneticDnsCacheView view;
    view.status = KeeneticDnsRefreshStatus::UNCHANGED;
    view.snapshot = snapshot;
    view.fetched_at_unix = 1775392496;  // 2026-04-05T12:34:56Z
    return view;
}

struct ServerFixture {
    SseBroadcaster broadcaster;
    ApiConfig api_config;
    ApiServer server;
    ApiContext ctx;

    ServerFixture()
        : api_config([] {
              ApiConfig config;
              config.listen = std::string(kApiListen);
              return config;
          }()),
          server(api_config),
          ctx(make_test_api_context(broadcaster)) {}

    void start() {
        register_keenetic_dns_handler(server, ctx);
        server.start();
    }
};

} // namespace

TEST_CASE("keenetic dns handler: GET returns the cached System proxy") {
    ServerFixture fixture;
    fixture.ctx.get_keenetic_dns_proxies_fn = [] { return make_view(); };
    fixture.start();

    httplib::Client client("127.0.0.1", 18190);
    const auto response = client.Get("/api/keenetic/dns-proxies");
    fixture.server.stop();

    REQUIRE(response != nullptr);
    CHECK(response->status == 200);
    const auto body = nlohmann::json::parse(response->body);
    CHECK(body["fetched_at"] == "2026-04-05T12:34:56Z");
    CHECK_FALSE(body.contains("error"));
    REQUIRE(body["proxies"].size() == 1);
    CHECK(body["proxies"][0]["name"] == "System");
    CHECK(body["proxies"][0]["upstreams"][0]["protocol"] == "DoT");
    CHECK(body["proxies"][0]["upstreams"][0]["target"] == "8.8.8.8@dns.google");
    CHECK(body["proxies"][0]["upstreams"][1]["protocol"] == "DoH");
    CHECK(body["proxies"][0]["domain_servers"][0]["domain"] == "beeline.ru");
    CHECK(body["proxies"][0]["domain_servers"][0]["addresses"].size() == 2);
}

TEST_CASE("keenetic dns handler: failed refresh keeps the cached data and reports the error") {
    ServerFixture fixture;
    fixture.ctx.refresh_keenetic_dns_fn = [] {
        KeeneticDnsCacheView view = make_view();
        view.status = KeeneticDnsRefreshStatus::FETCH_FAILED_USED_CACHE;
        view.error = "RCI is down";
        return view;
    };
    fixture.start();

    httplib::Client client("127.0.0.1", 18190);
    const auto response = client.Post("/api/keenetic/dns-proxies/refresh", "", "application/json");
    fixture.server.stop();

    REQUIRE(response != nullptr);
    CHECK(response->status == 200);
    const auto body = nlohmann::json::parse(response->body);
    CHECK(body["error"] == "RCI is down");
    CHECK(body["proxies"].size() == 1);
}

TEST_CASE("keenetic dns handler: nothing cached and a failed read is a 502") {
    ServerFixture fixture;
    fixture.ctx.get_keenetic_dns_proxies_fn = [] {
        KeeneticDnsCacheView view;
        view.error = "RCI is down";
        return view;
    };
    fixture.start();

    httplib::Client client("127.0.0.1", 18190);
    const auto response = client.Get("/api/keenetic/dns-proxies");
    fixture.server.stop();

    REQUIRE(response != nullptr);
    CHECK(response->status == 502);
    CHECK(nlohmann::json::parse(response->body)["error"] == "RCI is down");
}

TEST_CASE("keenetic dns handler: builds without Keenetic API answer 503") {
    ServerFixture fixture;
    fixture.start();

    httplib::Client client("127.0.0.1", 18190);
    const auto get_response = client.Get("/api/keenetic/dns-proxies");
    const auto post_response =
        client.Post("/api/keenetic/dns-proxies/refresh", "", "application/json");
    fixture.server.stop();

    REQUIRE(get_response != nullptr);
    REQUIRE(post_response != nullptr);
    CHECK(get_response->status == 503);
    CHECK(post_response->status == 503);
}

} // namespace keen_pbr3

#endif // WITH_API
