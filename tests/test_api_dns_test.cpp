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

} // namespace keen_pbr3

#endif
