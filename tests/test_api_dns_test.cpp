#ifdef WITH_API

#include <doctest/doctest.h>
#include <httplib.h>

#include "api/handler_dns_test.hpp"
#include "api/handlers.hpp"
#include "api/server.hpp"
#include "api/sse_broadcaster.hpp"

namespace keen_pbr3 {

namespace {

ApiContext make_dns_test_context(SseBroadcaster& broadcaster) {
    static const std::string config_path = "/tmp/keen-pbr-dns-test.json";
    return ApiContext{
        config_path,
        broadcaster,
        [] { return Config{}; },
        [] { return false; },
        [](Config) {},
        []() -> std::optional<StagedConfigSnapshot> {
            return std::nullopt;
        },
        [] {},
        [](const Config&) {},
        [] { return ServiceHealthState{}; },
        [] { return RoutingHealthReport{}; },
        [] { return api::RuntimeOutboundsResponse{}; },
        [] { return api::RuntimeInterfaceInventoryResponse{}; },
        [](const Config&) {
            return std::map<std::string, api::ListRefreshStateValue>{};
        },
        [](const std::string&) { return TestRoutingResult{}; },
        [] {},
        [] {},
        [](Config, std::string) { return ConfigApplyResult{}; },
        [] {},
        [] {},
        [] {},
        [](std::optional<std::string>) { return ListRefreshOperationResult{}; },
    };
}

} // namespace

TEST_CASE("dns test SSE notices a client disconnect without a DNS event") {
    SseBroadcaster broadcaster;
    auto context = make_dns_test_context(broadcaster);
    ApiConfig config;
    config.listen = std::string("127.0.0.1:18194");
    ApiServer server(config);
    register_dns_test_handler(server, context);
    server.start();

    int status = 0;
    std::string content_type;
    std::string body;
    httplib::Client client("127.0.0.1", 18194);
    (void)client.Get(
        "/api/dns/test",
        [&status, &content_type](const httplib::Response& response) {
            status = response.status;
            content_type = response.get_header_value("Content-Type");
            return true;
        },
        [&body](const char* data, size_t length) {
            body.append(data, length);
            return false;
        });

    server.stop();

    CHECK(status == 200);
    CHECK(content_type.find("text/event-stream") != std::string::npos);
    CHECK(body == "data: {\"type\":\"HELLO\"}\n\n");
}

TEST_CASE("SseBroadcaster with a gap builder drops and reports instead of closing") {
    SseBroadcaster broadcaster(4, [](const std::string& first, const std::string& last) {
        return "GAP " + first + ".." + last;
    });
    auto sub = broadcaster.subscribe();
    for (const char* m : {"1", "2", "3", "4", "5", "6"}) broadcaster.publish(m);
    {
        KPBR_UNIQUE_LOCK(lock, sub->mutex);
        CHECK_FALSE(sub->closed);
        CHECK(sub->messages.size() == 4);
        sub->messages.clear();  // the reader catches up
    }
    broadcaster.publish("7");
    KPBR_UNIQUE_LOCK(lock, sub->mutex);
    REQUIRE(sub->messages.size() == 2);
    CHECK(sub->messages[0] == "GAP 5..6");
    CHECK(sub->messages[1] == "7");
}

} // namespace keen_pbr3

#endif
