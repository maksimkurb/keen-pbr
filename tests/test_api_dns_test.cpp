#ifdef WITH_API

#include <doctest/doctest.h>
#include <httplib.h>

#include <atomic>
#include <chrono>
#include <thread>

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

TEST_CASE("dns test SSE validates show and default marker view closes after a marker") {
    SseBroadcaster broadcaster;
    auto context = make_dns_test_context(broadcaster);
    ApiConfig config;
    config.listen = std::string("127.0.0.1:18195");
    ApiServer server(config);
    register_dns_test_handler(server, context);
    server.start();

    httplib::Client client("127.0.0.1", 18195);
    client.set_read_timeout(2, 0);
    const auto invalid = client.Get("/api/dns/test?show=invalid");
    REQUIRE(invalid);
    CHECK(invalid->status == 400);

    std::string body;
    httplib::Result result;
    std::atomic<bool> request_done{false};
    std::thread request([&] {
        result = client.Get(
            "/api/dns/test?show=keen-pbr&domain=check.keen.pbr",
            [](const httplib::Response& response) { return response.status == 200; },
            [&](const char* data, size_t length) {
                body.append(data, length);
                return true;
            });
        request_done.store(true);
    });

    for (int i = 0; i < 100 && !broadcaster.has_subscribers(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    const bool subscribed = broadcaster.has_subscribers();
    if (!subscribed) {
        server.stop();
        request.join();
        CHECK(subscribed);
        return;
    }
    broadcaster.publish(R"({"type":"INTERCEPT","source":"dns","domain":"other.example"})",
                        {"dns", "other.example"});
    broadcaster.publish(R"({"type":"INTERCEPT","source":"marker","domain":"other.keen.pbr"})",
                        {"marker", "other.keen.pbr"});
    CHECK(broadcaster.has_subscribers());
    broadcaster.publish(R"({"type":"INTERCEPT","source":"marker","domain":"check.keen.pbr"})",
                        {"marker", "check.keen.pbr"});
    for (int i = 0; i < 100 && broadcaster.has_subscribers(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    request.join();
    CHECK(request_done.load());
    REQUIRE(result);
    CHECK(result->status == 200);
    CHECK(body.find("{\"type\":\"HELLO\"}") != std::string::npos);
    CHECK(body.find("\"source\":\"dns\"") == std::string::npos);
    CHECK(body.find("other.keen.pbr") == std::string::npos);
    CHECK(body.find("\"source\":\"marker\"") != std::string::npos);
    CHECK_FALSE(broadcaster.has_subscribers());
    server.stop();
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
    broadcaster.publish("7", {});
    KPBR_UNIQUE_LOCK(lock, sub->mutex);
    REQUIRE(sub->messages.size() == 2);
    CHECK(sub->messages[0] == "GAP 5..6");
    CHECK(sub->messages[1] == "7");
}

TEST_CASE("dns test SSE full alias remains continuous through marker events") {
    SseBroadcaster broadcaster;
    auto context = make_dns_test_context(broadcaster);
    ApiConfig config;
    config.listen = std::string("127.0.0.1:18196");
    ApiServer server(config);
    register_dns_test_handler(server, context);
    server.start();

    httplib::Client client("127.0.0.1", 18196);
    client.set_read_timeout(2, 0);
    std::string body;
    std::thread request([&] {
        (void)client.Get(
            "/api/dns/test?show=full",
            [](const httplib::Response& response) { return response.status == 200; },
            [&](const char* data, size_t length) {
                body.append(data, length);
                return body.find("\"source\":\"dns\"") == std::string::npos;
            });
    });
    for (int i = 0; i < 100 && !broadcaster.has_subscribers(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    const bool subscribed = broadcaster.has_subscribers();
    if (!subscribed) {
        server.stop();
        request.join();
        CHECK(subscribed);
        return;
    }
    broadcaster.publish(R"({"type":"INTERCEPT","source":"marker","domain":"check.keen.pbr"})",
                        {"marker", "check.keen.pbr"});
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    CHECK(broadcaster.has_subscribers());
    broadcaster.publish(R"({"type":"INTERCEPT","source":"dns","domain":"other.example"})",
                        {"dns", "other.example"});
    request.join();
    CHECK(body.find("\"source\":\"marker\"") != std::string::npos);
    CHECK(body.find("\"source\":\"dns\"") != std::string::npos);
    server.stop();
}

TEST_CASE("dns test SSE filter uses meta and does not require valid JSON") {
    SseBroadcaster broadcaster;
    auto context = make_dns_test_context(broadcaster);
    ApiConfig config;
    config.listen = std::string("127.0.0.1:18197");
    ApiServer server(config);
    register_dns_test_handler(server, context);
    server.start();

    httplib::Client client("127.0.0.1", 18197);
    client.set_read_timeout(2, 0);
    std::string body;
    std::thread request([&] {
        (void)client.Get(
            "/api/dns/test?show=keen-pbr",
            [](const httplib::Response& response) { return response.status == 200; },
            [&](const char* data, size_t length) {
                body.append(data, length);
                return true;
            });
    });

    for (int i = 0; i < 100 && !broadcaster.has_subscribers(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    const bool subscribed = broadcaster.has_subscribers();
    if (!subscribed) {
        server.stop();
        request.join();
        CHECK(subscribed);
        return;
    }

    // Publish non-JSON message with marker meta: verifies filter uses meta only
    broadcaster.publish("not valid json", {"marker", "check.keen.pbr"});
    for (int i = 0; i < 100 && broadcaster.has_subscribers(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    request.join();
    CHECK(body.find("{\"type\":\"HELLO\"}") != std::string::npos);
    CHECK(body.find("not valid json") != std::string::npos);
    CHECK_FALSE(broadcaster.has_subscribers());
    server.stop();
}

} // namespace keen_pbr3

#endif
