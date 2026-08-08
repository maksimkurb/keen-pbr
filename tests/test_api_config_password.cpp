#ifdef WITH_API

#include <doctest/doctest.h>

#include "../src/api/handler_config.hpp"

namespace keen_pbr3 {

namespace {

Config config_with_password(std::string password_hash) {
    Config config;
    config.api = ApiConfig{};
    config.api->authentication = AuthenticationConfig{};
    config.api->authentication->password_hash = std::move(password_hash);
    return config;
}

} // namespace

TEST_CASE("config API responses never contain the password verifier") {
    const auto internal = config_with_password("secret-verifier");
    const auto response = normalize_config_for_api_response(internal);

    REQUIRE(response.api.has_value());
    REQUIRE(response.api->authentication.has_value());
    CHECK_FALSE(response.api->authentication->password_hash.has_value());
    CHECK(internal.api->authentication->password_hash == "secret-verifier");
}

TEST_CASE("general config updates cannot replace or remove the password verifier") {
    const auto visible = config_with_password("server-owned-verifier");

    auto replacement = config_with_password("browser-supplied-verifier");
    protect_config_password_hash(replacement, visible);
    CHECK(replacement.api->authentication->password_hash == "server-owned-verifier");

    Config removal;
    protect_config_password_hash(removal, visible);
    REQUIRE(removal.api.has_value());
    REQUIRE(removal.api->authentication.has_value());
    CHECK(removal.api->authentication->password_hash == "server-owned-verifier");

    auto insertion = config_with_password("browser-supplied-verifier");
    protect_config_password_hash(insertion, Config{});
    CHECK_FALSE(insertion.api->authentication->password_hash.has_value());
}

} // namespace keen_pbr3

#endif // WITH_API
