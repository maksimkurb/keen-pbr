#ifdef WITH_API

#include <doctest/doctest.h>
#include <httplib.h>
#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <thread>

#include "../src/api/server.hpp"
#include "../src/api/sse_broadcaster.hpp"
#include "../src/api/sse_response.hpp"
#include "../src/auth/password.hpp"

namespace keen_pbr3 {

TEST_CASE("Argon2id password verifiers validate and reject other passwords") {
    const auto verifier = auth::generate_password_hash("correct horse");
    CHECK(verifier.rfind("argon2id$v=19$m=2048,t=2,p=1$", 0) == 0);
    CHECK(auth::valid_password_hash(verifier));
    CHECK(auth::verify_password("correct horse", verifier));
    CHECK_FALSE(auth::verify_password("wrong", verifier));
    CHECK_FALSE(auth::valid_password_hash("argon2id$v=19$m=19456,t=2,p=1$"));
    CHECK_FALSE(auth::valid_password_hash("sha256$invalid"));
    CHECK_FALSE(auth::valid_password_hash("pbkdf2-sha256$200000$legacy$verifier"));
    auto modified = verifier;
    modified.replace(modified.find("m=2048"), 6, "m=32768");
    CHECK_FALSE(auth::valid_password_hash(modified));
}

TEST_CASE("Monocypher provides token entropy and BLAKE2b hashing") {
    const auto first = auth::random_token();
    const auto second = auth::random_token();
    CHECK(first.size() == 64);
    CHECK(first.find_first_not_of("0123456789abcdef") == std::string::npos);
    CHECK(first != second);
    CHECK(auth::blake2b_hex("") ==
          "0e5751c026e543b2e8ab2eb06099daa1d1e5df47778f7787faab45cdf12fe3a8");
    CHECK(auth::constant_time_equal(first, first));
    CHECK_FALSE(auth::constant_time_equal(first, second));
}

TEST_CASE("API accepts Basic auth and keeps only the newest Bearer session") {
    ApiConfig config;
    config.listen = std::string("127.0.0.1:18193");
    config.authentication = AuthenticationConfig{};
    config.authentication->enabled = true;
    config.authentication->password_hash = auth::generate_password_hash("secret");
    ApiServer server(config);
    server.get("/api/protected", [] { return std::string("{\"ok\":true}"); });
    server.get_stream("/api/protected-stream",
                      [](const httplib::Request&, httplib::Response& response) {
                          response.set_content("protected stream", "text/plain");
                      });
    server.get_stream("/metrics",
                      [](const httplib::Request&, httplib::Response& response) {
                          response.set_content("keen_pbr_test 1\n",
                                               "text/plain; version=0.0.4; charset=utf-8");
                      });
    server.start();
    httplib::Client client("127.0.0.1", 18193);

    const auto unauthorized = client.Get("/api/protected");
    REQUIRE(unauthorized != nullptr);
    CHECK(unauthorized->status == 401);
    CHECK(unauthorized->get_header_value("Content-Type") == "application/json");
    CHECK_FALSE(unauthorized->has_header("WWW-Authenticate"));
    CHECK(nlohmann::json::parse(unauthorized->body).at("error") == "authentication required");
    CHECK(client.Get("/api/protected-stream")->status == 401);
    CHECK(client.Get("/metrics")->status == 401);
    const auto basic = client.Get("/api/protected", httplib::Headers{{"Authorization", "Basic YWRtaW46c2VjcmV0"}});
    REQUIRE(basic != nullptr);
    CHECK(basic->status == 200);
    const auto basic_stream = client.Get(
        "/api/protected-stream", httplib::Headers{{"Authorization", "Basic YWRtaW46c2VjcmV0"}});
    REQUIRE(basic_stream != nullptr);
    CHECK(basic_stream->status == 200);
    const auto metrics = client.Get(
        "/metrics", httplib::Headers{{"Authorization", "Basic YWRtaW46c2VjcmV0"}});
    REQUIRE(metrics != nullptr);
    CHECK(metrics->status == 200);
    CHECK(metrics->get_header_value("Content-Type") ==
          "text/plain; version=0.0.4; charset=utf-8");
    CHECK(metrics->body == "keen_pbr_test 1\n");

    const auto login1 = client.Post("/api/auth/login", "{\"password\":\"secret\"}", "application/json");
    REQUIRE(login1 != nullptr);
    REQUIRE(login1->status == 200);
    const auto token1 = nlohmann::json::parse(login1->body).at("token").get<std::string>();
    const auto login2 = client.Post("/api/auth/login", "{\"password\":\"secret\"}", "application/json");
    REQUIRE(login2 != nullptr);
    const auto token2 = nlohmann::json::parse(login2->body).at("token").get<std::string>();

    CHECK(client.Get("/api/protected", httplib::Headers{{"Authorization", "Bearer " + token1}})->status == 401);
    CHECK(client.Get("/api/protected", httplib::Headers{{"Authorization", "Bearer " + token2}})->status == 200);

    ApiConfig disabled_config;
    disabled_config.authentication = AuthenticationConfig{};
    disabled_config.authentication->enabled = false;
    server.update_runtime_config(disabled_config, "Home router");
    CHECK(client.Get("/api/protected")->status == 200);
    const auto status = client.Get("/api/auth/status");
    REQUIRE(status != nullptr);
    CHECK(nlohmann::json::parse(status->body).at("device_name") == "Home router");

    ApiConfig changed_config;
    changed_config.authentication = AuthenticationConfig{};
    changed_config.authentication->enabled = true;
    changed_config.authentication->password_hash = auth::generate_password_hash("newsecret");
    server.update_runtime_config(changed_config, "Home router");
    CHECK(client.Get("/api/protected", httplib::Headers{{"Authorization", "Bearer " + token2}})->status == 401);
    CHECK(client.Get("/api/protected", httplib::Headers{{"Authorization", "Basic YWRtaW46c2VjcmV0"}})->status == 401);
    CHECK(client.Get("/api/protected", httplib::Headers{{"Authorization", "Basic YWRtaW46bmV3c2VjcmV0"}})->status == 200);
    server.stop();
}

TEST_CASE("CORS rejects foreign origins when authentication is disabled") {
    ApiConfig config;
    config.listen = std::string("127.0.0.1:18194");
    ApiServer server(config);
    server.get("/api/example", [] { return std::string("{}"); });
    server.start();
    httplib::Client client("127.0.0.1", 18194);
    const auto response = client.Get("/api/example", httplib::Headers{{"Origin", "https://evil.example"}});
    server.stop();
    REQUIRE(response != nullptr);
    CHECK(response->status == 403);
    CHECK_FALSE(response->has_header("Access-Control-Allow-Origin"));
}

namespace {

// A real streaming client against an auth-aware SSE endpoint.
class StreamProbe {
public:
    StreamProbe(int port, httplib::Headers headers) {
        thread_ = std::thread([this, port, headers = std::move(headers)] {
            httplib::Client client("127.0.0.1", port);
            client.set_read_timeout(20, 0);
            (void)client.Get(
                "/api/test-stream", headers,
                [this](const httplib::Response& response) {
                    status_ = response.status;
                    return true;
                },
                [this](const char*, size_t) {
                    received_ = true;
                    return true;
                });
            finished_ = true;
        });
    }
    ~StreamProbe() { thread_.join(); }

    bool wait_open() const {
        for (int i = 0; i < 100 && !received_ && !finished_; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds{50});
        }
        return received_ && status_ == 200;
    }
    bool closes_within(std::chrono::seconds limit) const {
        const auto deadline = std::chrono::steady_clock::now() + limit;
        while (!finished_ && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds{50});
        }
        return finished_;
    }
    bool finished() const { return finished_; }

private:
    std::atomic<bool> received_{false};
    std::atomic<bool> finished_{false};
    std::atomic<int> status_{0};
    std::thread thread_;
};

void register_test_stream(ApiServer& server, SseBroadcaster& broadcaster) {
    server.get_stream("/api/test-stream",
                      [&server, &broadcaster](const httplib::Request& request,
                                              httplib::Response& response) {
                          auto subscription = broadcaster.subscribe({"hello"});
                          set_sse_response(
                              response, subscription,
                              [&broadcaster, subscription] { broadcaster.unsubscribe(subscription); },
                              {}, server.stream_authorization(request));
                      });
}

ApiConfig auth_config(int port, const std::string& password) {
    ApiConfig config;
    config.listen = std::string("127.0.0.1:") + std::to_string(port);
    config.authentication = AuthenticationConfig{};
    config.authentication->enabled = true;
    config.authentication->password_hash = auth::generate_password_hash(password);
    return config;
}

const httplib::Headers kBasicSecret{{"Authorization", "Basic YWRtaW46c2VjcmV0"}};
constexpr auto kCloseLimit = std::chrono::seconds{5};

std::string login(httplib::Client& client) {
    const auto response = client.Post("/api/auth/login", "{\"password\":\"secret\"}", "application/json");
    REQUIRE(response != nullptr);
    REQUIRE(response->status == 200);
    return nlohmann::json::parse(response->body).at("token").get<std::string>();
}

} // namespace

TEST_CASE("authenticated streams stay open while nothing changes") {
    SseBroadcaster broadcaster;
    const auto config = auth_config(18199, "secret");
    ApiServer server(config);
    register_test_stream(server, broadcaster);
    server.start();
    httplib::Client client("127.0.0.1", 18199);
    const auto token = login(client);
    {
        StreamProbe basic(18199, kBasicSecret);
        StreamProbe bearer(18199, {{"Authorization", "Bearer " + token}});
        REQUIRE(basic.wait_open());
        REQUIRE(bearer.wait_open());
        // Same settings re-applied: no revocation.
        server.update_runtime_config(config, "Router");
        CHECK_FALSE(basic.closes_within(std::chrono::seconds{3}));
        CHECK_FALSE(bearer.finished());
        broadcaster.close_all();
        CHECK(basic.closes_within(kCloseLimit));
        CHECK(bearer.closes_within(kCloseLimit));
    }
    server.stop();
}

TEST_CASE("unauthenticated streams stay open while authentication is disabled") {
    SseBroadcaster broadcaster;
    ApiConfig config;
    config.listen = std::string("127.0.0.1:18200");
    ApiServer server(config);
    register_test_stream(server, broadcaster);
    server.start();
    {
        StreamProbe probe(18200, {});
        REQUIRE(probe.wait_open());
        ApiConfig still_disabled;
        still_disabled.authentication = AuthenticationConfig{};
        still_disabled.authentication->enabled = false;
        server.update_runtime_config(still_disabled, "Router");
        CHECK_FALSE(probe.closes_within(std::chrono::seconds{3}));
        broadcaster.close_all();
    }
    server.stop();
}

TEST_CASE("unauthenticated streams close once authentication is enabled") {
    SseBroadcaster broadcaster;
    ApiConfig config;
    config.listen = std::string("127.0.0.1:18201");
    ApiServer server(config);
    register_test_stream(server, broadcaster);
    server.start();
    {
        StreamProbe probe(18201, {});
        REQUIRE(probe.wait_open());
        server.update_runtime_config(auth_config(18201, "secret"), "Router");
        CHECK(probe.closes_within(kCloseLimit));
    }
    server.stop();
}

TEST_CASE("authenticated streams close when their credentials are revoked") {
    SseBroadcaster broadcaster;
    ApiServer server(auth_config(18202, "secret"));
    register_test_stream(server, broadcaster);
    server.start();
    httplib::Client client("127.0.0.1", 18202);

    SUBCASE("logout closes the session's stream") {
        const auto token = login(client);
        const httplib::Headers bearer{{"Authorization", "Bearer " + token}};
        StreamProbe probe(18202, bearer);
        REQUIRE(probe.wait_open());
        REQUIRE(client.Post("/api/auth/logout", bearer, "", "application/json")->status == 204);
        CHECK(probe.closes_within(kCloseLimit));
    }
    SUBCASE("a new login closes the previous session's stream only") {
        const auto first = login(client);
        StreamProbe old_stream(18202, {{"Authorization", "Bearer " + first}});
        REQUIRE(old_stream.wait_open());
        const auto second = login(client);
        StreamProbe new_stream(18202, {{"Authorization", "Bearer " + second}});
        REQUIRE(new_stream.wait_open());
        CHECK(old_stream.closes_within(kCloseLimit));
        CHECK_FALSE(new_stream.finished());
        broadcaster.close_all();
    }
    SUBCASE("a password change closes Basic and Bearer streams") {
        const auto token = login(client);
        StreamProbe basic(18202, kBasicSecret);
        StreamProbe bearer(18202, {{"Authorization", "Bearer " + token}});
        REQUIRE(basic.wait_open());
        REQUIRE(bearer.wait_open());
        server.update_runtime_config(auth_config(18202, "newsecret"), "Router");
        CHECK(basic.closes_within(kCloseLimit));
        CHECK(bearer.closes_within(kCloseLimit));
    }
    SUBCASE("disabling authentication closes streams opened with credentials") {
        StreamProbe basic(18202, kBasicSecret);
        REQUIRE(basic.wait_open());
        ApiConfig disabled;
        disabled.authentication = AuthenticationConfig{};
        disabled.authentication->enabled = false;
        server.update_runtime_config(disabled, "Router");
        CHECK(basic.closes_within(kCloseLimit));
    }
    server.stop();
}

TEST_CASE("API exposes the public auth state") {
    const auto config = auth_config(18203, "secret");
    ApiServer server(config, "Home router");
    CHECK(server.auth_state().enabled);
    CHECK(server.auth_state().device_name == "Home router");
    ApiConfig disabled;
    server.update_runtime_config(disabled, "Office");
    CHECK_FALSE(server.auth_state().enabled);
    CHECK(server.auth_state().device_name == "Office");
}

} // namespace keen_pbr3
#endif
