#ifdef WITH_API

#include <doctest/doctest.h>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include "../src/api/handler_config.hpp"
#include "../src/api/server.hpp"
#include "../src/api/sse_broadcaster.hpp"

namespace keen_pbr3 {

namespace {

Config config_with_password(std::string password_hash) {
    Config config;
    config.api = ApiConfig{};
    config.api->authentication = AuthenticationConfig{};
    config.api->authentication->password_hash = std::move(password_hash);
    return config;
}

ApiContext make_config_post_context(SseBroadcaster& broadcaster,
                                    const std::string& config_path,
                                    int* staged_count,
                                    RawPreroutingMode raw_prerouting) {
    ApiContext ctx{
        config_path,
        broadcaster,
        []() { return Config{}; },
        []() { return false; },
        [staged_count](Config) { ++*staged_count; },
        []() -> std::optional<StagedConfigSnapshot> { return std::nullopt; },
        []() {},
        [](const Config&) {},
    };
    ctx.validation_context_fn = [raw_prerouting]() {
        return ConfigValidationContext{raw_prerouting};
    };
    return ctx;
}

nlohmann::json post_balance_config(int port, RawPreroutingMode raw_prerouting,
                                   int* status, int* staged_count) {
    const std::string config_path = "/tmp/keen-pbr-test-config.json";
    SseBroadcaster broadcaster;
    ApiConfig api_config;
    api_config.listen = "127.0.0.1:" + std::to_string(port);
    ApiServer server(api_config);
    auto ctx = make_config_post_context(broadcaster, config_path, staged_count,
                                        raw_prerouting);
    register_config_handler(server, ctx);
    server.start();

    const std::string body = R"({
      "daemon":{"firewall_backend":"iptables"},
      "dns":{"servers":[{"tag":"default_dns","address":"127.0.0.1"}]},
      "outbounds":[
        {"type":"interface","tag":"wan","interface":"wan"},
        {"type":"urltest","tag":"auto","url":"http://example.test",
         "strategy":"balance","outbound_groups":[{"outbounds":["wan"]}]}
      ]})";
    httplib::Client client("127.0.0.1", port);
    const auto response = client.Post("/api/config", body, "application/json");
    server.stop();
    REQUIRE(response != nullptr);
    *status = response->status;
    return nlohmann::json::parse(response->body);
}

} // namespace

TEST_CASE("POST /api/config rejects balance when the daemon uses raw PREROUTING") {
#ifdef KEEN_PBR_PLATFORM_KEENETIC
    return;
#endif
    int status = 0;
    int staged = 0;
    const auto body = post_balance_config(18191, RawPreroutingMode{true, false},
                                          &status, &staged);
    CHECK(status == 400);
    CHECK(staged == 0);
    REQUIRE(body.contains("validation_errors"));
    bool found = false;
    for (const auto& issue : body["validation_errors"]) {
        if (issue["path"] == "outbounds[1].strategy" &&
            issue["message"].get<std::string>().find("raw table") != std::string::npos) {
            found = true;
        }
    }
    CHECK(found);
}

TEST_CASE("POST /api/config accepts balance when the daemon does not use raw PREROUTING") {
#ifdef KEEN_PBR_PLATFORM_KEENETIC
    return;
#endif
    int status = 0;
    int staged = 0;
    (void)post_balance_config(18192, RawPreroutingMode{}, &status, &staged);
    CHECK(status == 200);
    CHECK(staged == 1);
}

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

TEST_CASE("ipset capacity defaults stay absent in API config responses") {
    Config config;
    config.daemon = DaemonConfig{};

    const auto response = normalize_config_for_api_response(config);
    REQUIRE(response.daemon.has_value());
    CHECK_FALSE(response.daemon->ipset_hashsize.has_value());
    CHECK_FALSE(response.daemon->ipset_maxelem.has_value());

    const auto serialized = serialize_config_pretty(response);
    CHECK(serialized.find("ipset_hashsize") == std::string::npos);
    CHECK(serialized.find("ipset_maxelem") == std::string::npos);
}

TEST_CASE("configured ipset capacities are preserved in API serialization") {
    Config config;
    config.daemon = DaemonConfig{};
    config.daemon->ipset_hashsize = 2048;
    config.daemon->ipset_maxelem = 131072;

    const auto serialized = serialize_config_pretty(config);
    CHECK(serialized.find("\"ipset_hashsize\": 2048") != std::string::npos);
    CHECK(serialized.find("\"ipset_maxelem\": 131072") != std::string::npos);
}

} // namespace keen_pbr3

#endif // WITH_API
