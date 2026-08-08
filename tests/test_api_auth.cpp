#ifdef WITH_API

#include <doctest/doctest.h>
#include <httplib.h>
#include <nlohmann/json.hpp>

#include "../src/api/server.hpp"
#include "../src/auth/password.hpp"

namespace keen_pbr3 {

TEST_CASE("Argon2id password verifiers validate and reject other passwords") {
    const auto verifier = auth::generate_password_hash("correct horse");
    CHECK(verifier.rfind("argon2id$v=19$m=19456,t=2,p=1$", 0) == 0);
    CHECK(auth::valid_password_hash(verifier));
    CHECK(auth::verify_password("correct horse", verifier));
    CHECK_FALSE(auth::verify_password("wrong", verifier));
    CHECK_FALSE(auth::valid_password_hash("sha256$invalid"));
    CHECK_FALSE(auth::valid_password_hash("pbkdf2-sha256$200000$legacy$verifier"));
    auto modified = verifier;
    modified.replace(modified.find("m=19456"), 7, "m=32768");
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

} // namespace keen_pbr3
#endif
