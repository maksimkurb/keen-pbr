#ifdef WITH_API

#include <doctest/doctest.h>
#include <httplib.h>
#include <nlohmann/json.hpp>

#include "../src/api/server.hpp"
#include "../src/auth/password.hpp"

namespace keen_pbr3 {

TEST_CASE("PBKDF2 password verifiers validate and reject other passwords") {
    const auto verifier = auth::generate_password_hash("correct horse");
    CHECK(auth::valid_password_hash(verifier));
    CHECK(auth::verify_password("correct horse", verifier));
    CHECK_FALSE(auth::verify_password("wrong", verifier));
    CHECK_FALSE(auth::valid_password_hash("sha256$invalid"));
}

TEST_CASE("API accepts Basic auth and keeps only the newest Bearer session") {
    ApiConfig config;
    config.listen = std::string("127.0.0.1:18193");
    config.authentication = AuthenticationConfig{};
    config.authentication->enabled = true;
    config.authentication->password_hash = auth::generate_password_hash("secret");
    ApiServer server(config);
    server.get("/api/protected", [] { return std::string("{\"ok\":true}"); });
    server.start();
    httplib::Client client("127.0.0.1", 18193);

    CHECK(client.Get("/api/protected")->status == 401);
    const auto basic = client.Get("/api/protected", httplib::Headers{{"Authorization", "Basic YWRtaW46c2VjcmV0"}});
    REQUIRE(basic != nullptr);
    CHECK(basic->status == 200);

    const auto login1 = client.Post("/api/auth/login", "{\"password\":\"secret\"}", "application/json");
    REQUIRE(login1 != nullptr);
    REQUIRE(login1->status == 200);
    const auto token1 = nlohmann::json::parse(login1->body).at("token").get<std::string>();
    const auto login2 = client.Post("/api/auth/login", "{\"password\":\"secret\"}", "application/json");
    REQUIRE(login2 != nullptr);
    const auto token2 = nlohmann::json::parse(login2->body).at("token").get<std::string>();

    CHECK(client.Get("/api/protected", httplib::Headers{{"Authorization", "Bearer " + token1}})->status == 401);
    CHECK(client.Get("/api/protected", httplib::Headers{{"Authorization", "Bearer " + token2}})->status == 200);
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

} // namespace keen_pbr3
#endif
