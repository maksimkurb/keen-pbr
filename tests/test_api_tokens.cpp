#ifdef WITH_API

#include <doctest/doctest.h>
#include <httplib.h>
#include <keen-pbr/version.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <list>

#include "../src/api/handler_config.hpp"
#include "../src/api/generated/api_routes.hpp"
#include "../src/api/handler_tokens.hpp"
#include "../src/api/handlers.hpp"
#include "../src/api/server.hpp"
#include "../src/api/sse_broadcaster.hpp"
#include "../src/auth/api_scope.hpp"
#include "../src/auth/password.hpp"

namespace keen_pbr3 {
namespace {

using auth::ApiScope;

std::int64_t now() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

ApiTokenConfig make_token(const std::string& id, const std::string& value,
                          std::vector<ApiScope> scopes,
                          std::optional<std::int64_t> expires_at = std::nullopt) {
    ApiTokenConfig token;
    token.id = id;
    token.name = "test " + id;
    token.scopes = std::move(scopes);
    token.token_hash = auth::blake2b_hex(value);
    token.created_at = now();
    token.expires_at = expires_at;
    return token;
}

httplib::Headers bearer(const std::string& value) {
    return {{"Authorization", "Bearer " + value}};
}

ApiConfig auth_config(int port, std::vector<ApiTokenConfig> tokens) {
    ApiConfig config;
    config.listen = "127.0.0.1:" + std::to_string(port);
    config.authentication = AuthenticationConfig{};
    config.authentication->enabled = true;
    config.authentication->password_hash = auth::generate_password_hash("secret");
    config.authentication->tokens = std::move(tokens);
    return config;
}

const std::string& test_config_path() {
    static const std::string path = "/tmp/keen-pbr-test-config.json";
    return path;
}

// `config_path` is kept by reference, so the caller's string must outlive the context.
ApiContext fixture_context(SseBroadcaster& broadcaster, Config visible,
                           std::function<void(Config)> stage = [](Config) {},
                           const std::string& config_path = test_config_path()) {
    // ApiContext only refers to the path, so it must outlive the call even when
    // the caller passed a temporary.
    static std::list<std::string> kept_paths;
    kept_paths.push_back(config_path);
    return ApiContext{
        kept_paths.back(),
        broadcaster,
        [visible]() { return visible; },
        []() { return false; },
        std::move(stage),
        []() -> std::optional<StagedConfigSnapshot> { return std::nullopt; },
        []() {},
        [](const Config&) {},
    };
}

// Stands in for the daemon's control task: an edit runs on the latest stored
// settings, as it does there.
struct ApiStore {
    ApiConfig api;
    std::function<void()> before_edit;

    void install(ApiContext& ctx) {
        ctx.get_visible_config_fn = [this]() {
            Config config;
            config.api = api;
            return config;
        };
        ctx.mutate_api_config_fn = [this](const std::function<void(ApiConfig&)>& edit) {
            if (before_edit) before_edit();
            ApiConfig next = api;
            if (!next.authentication) next.authentication = AuthenticationConfig{};
            edit(next);
            if (next.authentication->tokens && next.authentication->tokens->empty()) {
                next.authentication->tokens.reset();
            }
            api = std::move(next);
        };
    }
};

const httplib::Headers& admin_headers() {
    static const httplib::Headers headers{
        {"Authorization", "Basic " + httplib::detail::base64_encode("admin:secret")}};
    return headers;
}

} // namespace

TEST_CASE("API scopes imply the scopes below them") {
    CHECK(auth::api_scope_grants({ApiScope::WRITE}, ApiScope::READ_PROMETHEUS));
    CHECK(auth::api_scope_grants({ApiScope::WRITE}, ApiScope::WRITE_RULES));
    CHECK(auth::api_scope_grants({ApiScope::READ}, ApiScope::READ_PROMETHEUS));
    CHECK(auth::api_scope_grants({ApiScope::READ}, ApiScope::READ));
    CHECK_FALSE(auth::api_scope_grants({ApiScope::READ}, ApiScope::WRITE_RULES));
    CHECK(auth::api_scope_grants({ApiScope::WRITE_RULES}, ApiScope::READ));
    CHECK_FALSE(auth::api_scope_grants({ApiScope::WRITE_RULES}, ApiScope::WRITE));
    CHECK(auth::api_scope_grants({ApiScope::READ_PROMETHEUS}, ApiScope::READ_PROMETHEUS));
    CHECK_FALSE(auth::api_scope_grants({ApiScope::READ_PROMETHEUS}, ApiScope::READ));
    CHECK_FALSE(auth::api_scope_grants({}, ApiScope::READ_PROMETHEUS));
    CHECK(auth::api_scopes_restricted_to_rules({ApiScope::WRITE_RULES}));
    CHECK_FALSE(auth::api_scopes_restricted_to_rules({ApiScope::WRITE_RULES, ApiScope::WRITE}));
    CHECK_FALSE(auth::api_scopes_restricted_to_rules({ApiScope::READ}));
    CHECK(auth::parse_api_scope("write:rules") == ApiScope::WRITE_RULES);
    CHECK_FALSE(auth::parse_api_scope("admin").has_value());
}

TEST_CASE("Each route needs the scope it should") {
    using auth::required_api_scope;
    CHECK(required_api_scope("GET", "/metrics") == ApiScope::READ_PROMETHEUS);
    CHECK(required_api_scope("GET", "/api/config") == ApiScope::READ);
    CHECK(required_api_scope("HEAD", "/api/config") == ApiScope::READ);
    CHECK(required_api_scope("GET", "/api/status/events") == ApiScope::READ);
    CHECK(required_api_scope("POST", "/api/routing/test") == ApiScope::READ);
    CHECK(required_api_scope("POST", "/api/dns/servers/check") == ApiScope::READ);
    CHECK(required_api_scope("POST", "/api/config") == ApiScope::WRITE_RULES);
    CHECK(required_api_scope("POST", "/api/config/save") == ApiScope::WRITE_RULES);
    CHECK(required_api_scope("POST", "/api/config/discard") == ApiScope::WRITE_RULES);
    CHECK(required_api_scope("POST", "/api/lists/refresh") == ApiScope::WRITE_RULES);
    CHECK(required_api_scope("POST", "/api/config/rollback") == ApiScope::WRITE);
    CHECK(required_api_scope("POST", "/api/service/restart") == ApiScope::WRITE);
    // A route the document does not declare fails closed, even a GET.
    CHECK(required_api_scope("POST", "/api/something/new") == ApiScope::WRITE);
    CHECK(required_api_scope("GET", "/api/something/new") == ApiScope::WRITE);
    // Password, settings and token management are never open to a token.
    CHECK_FALSE(required_api_scope("GET", "/api/auth/tokens").has_value());
    CHECK_FALSE(required_api_scope("POST", "/api/auth/tokens").has_value());
    CHECK_FALSE(required_api_scope("POST", "/api/auth/tokens/revoke").has_value());
    CHECK_FALSE(required_api_scope("POST", "/api/auth/password").has_value());
    CHECK_FALSE(required_api_scope("POST", "/api/auth/enabled").has_value());
    CHECK_FALSE(required_api_scope("POST", "/api/auth/cors").has_value());
    CHECK_FALSE(required_api_scope("GET", "/api/auth/settings").has_value());
    CHECK_FALSE(required_api_scope("POST", "/api/auth/logout").has_value());
    CHECK_FALSE(required_api_scope("POST", "/api/auth/not-declared").has_value());
    CHECK(auth::is_public_api_route("GET", "/api/auth/status"));
    CHECK(auth::is_public_api_route("POST", "/api/auth/login"));
    CHECK_FALSE(auth::is_public_api_route("GET", "/api/auth/login"));
    CHECK_FALSE(auth::is_public_api_route("GET", "/api/config"));
}

TEST_CASE("Only routes that guard their input are open to a rules-only token") {
    CHECK(auth::is_rules_only_safe_route("POST", "/api/config"));
    CHECK(auth::is_rules_only_safe_route("POST", "/api/config/save"));
    CHECK(auth::is_rules_only_safe_route("POST", "/api/config/discard"));
    CHECK(auth::is_rules_only_safe_route("POST", "/api/lists/refresh"));
    CHECK_FALSE(auth::is_rules_only_safe_route("POST", "/api/new/rules-route"));
    CHECK_FALSE(auth::is_rules_only_safe_route("GET", "/api/config"));
    // The list can only name routes that need write:rules.
    for (const char* path : {"/api/config", "/api/config/save", "/api/config/discard",
                             "/api/lists/refresh"}) {
        CHECK(auth::required_api_scope("POST", path) == ApiScope::WRITE_RULES);
    }
}

TEST_CASE("The route table matches the routes the server registers") {
    constexpr int port = 18247;
    SseBroadcaster broadcaster;
    auto ctx = fixture_context(broadcaster, Config{});
    ApiServer server(auth_config(port, {}));
    register_api_handlers(server, ctx);

    const auto registered = server.registered_routes();
    REQUIRE_FALSE(registered.empty());
    const auto in_table = [](const std::string& method, const std::string& path) {
        for (const auto& route : api::kApiRoutes) {
            if (route.method == method && route.path == path) return true;
        }
        return false;
    };
    // A route added without declaring it in docs/openapi.yaml would silently need `write`.
    for (const auto& entry : registered) {
        const std::string label = entry.first + " " + entry.second;
        INFO(label, " is not declared in docs/openapi.yaml");
        CHECK(in_table(entry.first, entry.second));
    }
    for (const auto& route : api::kApiRoutes) {
        const auto found = std::find(registered.begin(), registered.end(),
                                     std::make_pair(std::string(route.method), std::string(route.path)));
        const std::string label = std::string(route.method) + " " + std::string(route.path);
        INFO(label, " is documented but not registered");
        CHECK(found != registered.end());
    }
}

TEST_CASE("An API token is limited to its scopes") {
    constexpr int port = 18241;
    const std::string prom = auth::generate_api_token();
    const std::string reader = auth::generate_api_token();
    const std::string rules = auth::generate_api_token();
    const std::string writer = auth::generate_api_token();
    const std::string expired = auth::generate_api_token();
    ApiServer server(auth_config(port, {
        make_token("prom", prom, {ApiScope::READ_PROMETHEUS}),
        make_token("read", reader, {ApiScope::READ}),
        make_token("rules", rules, {ApiScope::WRITE_RULES}),
        make_token("write", writer, {ApiScope::WRITE}),
        make_token("old", expired, {ApiScope::WRITE}, now() - 5),
    }));
    server.get("/api/health/service", [] { return std::string("{\"ok\":true}"); });
    server.get("/api/auth/tokens", [] { return std::string("{\"tokens\":[]}"); });
    server.post("/api/service/restart", [] { return std::string("{\"ok\":true}"); });
    server.post("/api/lists/refresh", [] { return std::string("{\"ok\":true}"); });
    server.post("/api/config/discard", [] { return std::string("{\"ok\":true}"); });
    server.get("/api/undeclared", [] { return std::string("{\"ok\":true}"); });
    server.get_stream("/metrics", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("keen_pbr_test 1\n", "text/plain");
    });
    server.start();
    httplib::Client client("127.0.0.1", port);

    const auto status = [&](const std::string& token, const char* method, const char* path) {
        const auto result = std::string(method) == "GET"
            ? client.Get(path, bearer(token))
            : client.Post(path, bearer(token), "{}", "application/json");
        REQUIRE(result != nullptr);
        return result->status;
    };

    CHECK(status(prom, "GET", "/metrics") == 200);
    CHECK(status(prom, "GET", "/api/health/service") == 403);
    CHECK(status(reader, "GET", "/metrics") == 200);
    CHECK(status(reader, "GET", "/api/health/service") == 200);
    CHECK(status(reader, "POST", "/api/lists/refresh") == 403);
    CHECK(status(rules, "GET", "/api/health/service") == 200);
    CHECK(status(rules, "POST", "/api/lists/refresh") == 200);
    CHECK(status(rules, "POST", "/api/config/discard") == 200);
    CHECK(status(rules, "POST", "/api/service/restart") == 403);
    CHECK(status(writer, "POST", "/api/service/restart") == 200);
    CHECK(status(writer, "POST", "/api/lists/refresh") == 200);
    // Token management belongs to the password, even for the strongest token.
    CHECK(status(writer, "GET", "/api/auth/tokens") == 403);
    // A route missing from the documented table needs `write`.
    CHECK(status(reader, "GET", "/api/undeclared") == 403);
    CHECK(status(writer, "GET", "/api/undeclared") == 200);
    CHECK(status(expired, "GET", "/api/health/service") == 401);
    CHECK(status(auth::generate_api_token(), "GET", "/api/health/service") == 401);
    CHECK(status("kpbr_", "GET", "/api/health/service") == 401);

    const auto denied = client.Get("/api/health/service", bearer(prom));
    REQUIRE(denied != nullptr);
    CHECK(nlohmann::json::parse(denied->body).at("error").get<std::string>().find("read") !=
          std::string::npos);
    // The password still opens everything.
    const auto basic = client.Get("/api/auth/tokens", admin_headers());
    REQUIRE(basic != nullptr);
    CHECK(basic->status == 200);
    server.stop();
}

TEST_CASE("A stream route sees whether the caller may only change rules") {
    constexpr int port = 18248;
    const std::string rules = auth::generate_api_token();
    const std::string reader = auth::generate_api_token();
    ApiServer server(auth_config(port, {
        make_token("rules", rules, {ApiScope::WRITE_RULES}),
        make_token("read", reader, {ApiScope::READ}),
    }));
    server.get_stream("/api/status/events", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(api_caller_rules_only() ? "rules-only" : "other", "text/plain");
    });
    server.start();
    httplib::Client client("127.0.0.1", port);
    CHECK(client.Get("/api/status/events", bearer(rules))->body == "rules-only");
    CHECK(client.Get("/api/status/events", bearer(reader))->body == "other");
    CHECK(client.Get("/api/status/events", admin_headers())->body == "other");
    server.stop();
}

TEST_CASE("Revoking a token takes effect without a restart and last use stays in memory") {
    constexpr int port = 18242;
    const std::string value = auth::generate_api_token();
    ApiServer server(auth_config(port, {make_token("one", value, {ApiScope::READ})}));
    server.get("/api/health/service", [] { return std::string("{\"ok\":true}"); });
    server.start();
    httplib::Client client("127.0.0.1", port);

    CHECK(server.token_last_used().empty());
    const auto before = now();
    CHECK(client.Get("/api/health/service", bearer(value))->status == 200);
    const auto used = server.token_last_used();
    REQUIRE(used.count("one") == 1);
    CHECK(used.at("one") >= before);

    server.update_runtime_config(auth_config(port, {}), "");
    CHECK(client.Get("/api/health/service", bearer(value))->status == 401);
    CHECK(server.token_last_used().empty());
    server.stop();
}

TEST_CASE("Token endpoints create, list and revoke tokens and never return a hash") {
    constexpr int port = 18243;
    SseBroadcaster broadcaster;
    ApiStore store;
    store.api = auth_config(port, {});
    auto ctx = fixture_context(broadcaster, Config{});
    store.install(ctx);
    const auto stored = [&]() {
        return store.api.authentication->tokens.value_or(std::vector<ApiTokenConfig>{});
    };
    ApiServer server(auth_config(port, {}));
    register_tokens_handler(server, ctx);
    server.start();
    httplib::Client client("127.0.0.1", port);
    const httplib::Headers& admin = admin_headers();

    const auto created = client.Post(
        "/api/auth/tokens", admin,
        R"({"name":"  monitoring  ","scopes":["read:prometheus","read:prometheus"]})",
        "application/json");
    REQUIRE(created != nullptr);
    CHECK(created->status == 200);
    // The new token is shown once and must not be kept by a cache.
    CHECK(created->get_header_value("Cache-Control") == "no-store");
    const auto body = nlohmann::json::parse(created->body);
    const auto token = body.at("token").get<std::string>();
    CHECK(token.rfind("kpbr_", 0) == 0);
    CHECK(body.at("info").at("name") == "monitoring");
    CHECK(body.at("info").at("scopes") == nlohmann::json::array({"read:prometheus"}));
    REQUIRE(stored().size() == 1);
    const std::string hash = *stored()[0].token_hash;
    CHECK(hash == auth::blake2b_hex(token));
    CHECK(created->body.find(hash) == std::string::npos);

    const auto listed = client.Get("/api/auth/tokens", admin);
    REQUIRE(listed != nullptr);
    CHECK(listed->get_header_value("Cache-Control") == "no-store");
    CHECK(listed->body.find(token) == std::string::npos);
    CHECK(listed->body.find(hash) == std::string::npos);
    CHECK(listed->body.find("token_hash") == std::string::npos);
    CHECK(nlohmann::json::parse(listed->body).at("tokens").size() == 1);

    CHECK(client.Post("/api/auth/tokens", admin, R"({"name":"","scopes":["read"]})",
                      "application/json")->status == 400);
    CHECK(client.Post("/api/auth/tokens", admin, R"({"name":"x","scopes":[]})",
                      "application/json")->status == 400);
    CHECK(client.Post("/api/auth/tokens", admin, R"({"name":"x","scopes":["root"]})",
                      "application/json")->status == 400);
    CHECK(client.Post("/api/auth/tokens", admin,
                      R"({"name":"x","scopes":["read"],"expires_at":1})",
                      "application/json")->status == 400);
    CHECK(stored().size() == 1);

    CHECK(client.Post("/api/auth/tokens/revoke", admin, R"({"id":"nope"})",
                      "application/json")->status == 404);
    const auto revoked = client.Post(
        "/api/auth/tokens/revoke", admin,
        nlohmann::json{{"id", *stored()[0].id}}.dump(), "application/json");
    REQUIRE(revoked != nullptr);
    CHECK(revoked->status == 200);
    CHECK(revoked->body.find(hash) == std::string::npos);
    CHECK(stored().empty());
    server.stop();
}

TEST_CASE("Tokens cannot be created while authentication is off") {
    constexpr int port = 18244;
    SseBroadcaster broadcaster;
    Config visible;
    visible.api = ApiConfig{};
    auto ctx = fixture_context(broadcaster, visible);
    ctx.mutate_api_config_fn = [](const std::function<void(ApiConfig&)>&) {
        FAIL("nothing may be stored");
    };
    ApiConfig config;
    config.listen = "127.0.0.1:" + std::to_string(port);
    ApiServer server(config);
    register_tokens_handler(server, ctx);
    server.start();
    httplib::Client client("127.0.0.1", port);
    const auto result = client.Post("/api/auth/tokens", R"({"name":"x","scopes":["read"]})",
                                    "application/json");
    REQUIRE(result != nullptr);
    CHECK(result->status == 409);
    server.stop();
}

TEST_CASE("A write:rules token may change rules and lists but nothing else") {
    constexpr int port = 18245;
    SseBroadcaster broadcaster;
    int staged = 0;
    Config active;
    active.version = KEEN_PBR3_VERSION_STRING;
    auto ctx = fixture_context(broadcaster, active, [&staged](Config) { ++staged; });
    const std::string rules = auth::generate_api_token();
    const std::string writer = auth::generate_api_token();
    ApiServer server(auth_config(port, {
        make_token("rules", rules, {ApiScope::WRITE_RULES}),
        make_token("write", writer, {ApiScope::WRITE}),
    }));
    register_config_handler(server, ctx);
    server.start();
    httplib::Client client("127.0.0.1", port);

    // A config without a version is upgraded in memory, which would change
    // more than the rules; real configs carry the version they were saved with.
    const std::string version = std::string(R"("version":")") + KEEN_PBR3_VERSION_STRING + "\"";
    const std::string rules_change = "{" + version + R"(,"route":{"inbound_interfaces":["br0"]}})";
    const std::string other_change = "{" + version + R"(,"daemon":{"ipv6_enabled":false}})";
    const std::string both_change =
        "{" + version + R"(,"route":{"inbound_interfaces":["br0"]},"daemon":{"ipv6_enabled":false}})";

    CHECK(client.Post("/api/config", bearer(rules), rules_change, "application/json")->status == 200);
    CHECK(staged == 1);
    const auto refused = client.Post("/api/config", bearer(rules), other_change, "application/json");
    REQUIRE(refused != nullptr);
    CHECK(refused->status == 403);
    CHECK(client.Post("/api/config", bearer(rules), both_change, "application/json")->status == 403);
    CHECK(staged == 1);
    // The strongest token is not held back.
    CHECK(client.Post("/api/config", bearer(writer), other_change, "application/json")->status == 200);
    CHECK(staged == 2);
    server.stop();
}

TEST_CASE("A write:rules token cannot point a list at URLs") {
    constexpr int port = 18249;
    SseBroadcaster broadcaster;
    int staged = 0;
    Config active;
    active.version = KEEN_PBR3_VERSION_STRING;
    std::map<std::string, ListConfig> lists;
    lists["old_url"].url = "https://example.com/list.txt";
    lists["old_url"].detour = "vpn";
    lists["inline"].domains = std::vector<std::string>{"a.example"};
    active.lists = lists;
    auto ctx = fixture_context(broadcaster, active, [&staged](Config) { ++staged; });
    ctx.get_active_config_fn = [active]() { return active; };
    const std::string rules = auth::generate_api_token();
    const std::string writer = auth::generate_api_token();
    ApiServer server(auth_config(port, {
        make_token("rules", rules, {ApiScope::WRITE_RULES}),
        make_token("write", writer, {ApiScope::WRITE}),
    }));
    register_config_handler(server, ctx);
    server.start();
    httplib::Client client("127.0.0.1", port);

    const std::string version = std::string(R"("version":")") + KEEN_PBR3_VERSION_STRING + "\"";
    const auto post = [&](const std::string& token, const std::string& lists_json) {
        auto result = client.Post("/api/config", bearer(token),
                                  "{" + version + R"(,"lists":)" + lists_json + "}",
                                  "application/json");
        REQUIRE(result != nullptr);
        return result;
    };
    const std::string existing =
        R"("old_url":{"url":"https://example.com/list.txt","detour":"vpn"},)";

    // Existing sources stay as they are while the domains change.
    CHECK(post(rules, "{" + existing + R"("inline":{"domains":["a.example","b.example"]}})")->status == 200);
    CHECK(staged == 1);
    // Removing a list is a change to the rules too.
    CHECK(post(rules, R"({"inline":{"domains":["a.example"]}})")->status == 200);
    staged = 0;

    for (const std::string list_json : {
             R"("bad":{"url":"http://127.0.0.1:1/x"})",
             R"("bad":{"domains":["a.example"],"detour":"vpn"})",
         }) {
        CHECK(post(rules, "{" + existing + list_json + "}")->status == 403);
    }
    CHECK(post(rules, R"({"old_url":{"url":"http://internal/x"}})")->status == 403);
    CHECK(post(rules, R"({"old_url":{"url":"https://example.com/list.txt","detour":"other"}})")->status == 403);
    CHECK(staged == 0);
    const auto reply = post(rules, R"({"bad":{"url":"http://x.example/a"}})");
    CHECK(reply->status == 403);
    CHECK(nlohmann::json::parse(reply->body).at("validation_errors")[0].at("path") ==
          "lists.bad.url");

    // The strongest token may set a url.
    CHECK(post(writer, R"({"bad":{"url":"https://example.com/other.txt"}})")->status == 200);
    server.stop();
}

TEST_CASE("A list file set through the API must be in the configuration directory") {
    constexpr int port = 18252;
    SseBroadcaster broadcaster;
    int staged = 0;
    const std::string config_path = "/etc/keen-pbr/config.json";
    Config active;
    active.version = KEEN_PBR3_VERSION_STRING;
    std::map<std::string, ListConfig> lists;
    lists["legacy"].file = "/etc/list.text";
    lists["relative"].file = "./local.txt";
    active.lists = lists;
    auto ctx = fixture_context(broadcaster, active, [&staged](Config) { ++staged; }, config_path);
    ctx.get_active_config_fn = [active]() { return active; };
    ctx.get_list_refresh_state_map_fn = [](const Config&) {
        return std::map<std::string, api::ListRefreshState>{};
    };
    const std::string rules = auth::generate_api_token();
    const std::string writer = auth::generate_api_token();
    ApiServer server(auth_config(port, {
        make_token("rules", rules, {ApiScope::WRITE_RULES}),
        make_token("write", writer, {ApiScope::WRITE}),
    }));
    register_config_handler(server, ctx);
    server.start();
    httplib::Client client("127.0.0.1", port);

    const std::string version = std::string(R"("version":")") + KEEN_PBR3_VERSION_STRING + "\"";
    const auto post = [&](const httplib::Headers& headers, const std::string& lists_json) {
        auto result = client.Post("/api/config", headers,
                                  "{" + version + R"(,"lists":)" + lists_json + "}",
                                  "application/json");
        REQUIRE(result != nullptr);
        return result;
    };
    const std::string kept = R"("legacy":{"file":"/etc/list.text"},"relative":{"file":"./local.txt"})";
    struct Caller { const char* name; httplib::Headers headers; };
    const std::vector<Caller> callers{
        {"admin", admin_headers()}, {"write", bearer(writer)}, {"rules", bearer(rules)}};

    for (const auto& caller : callers) {
        INFO(caller.name);
        staged = 0;
        // A path that is already in the config is kept, wherever it is.
        CHECK(post(caller.headers, "{" + kept + "}")->status == 200);
        CHECK(staged == 1);
        // Changed to another place outside the directory: refused.
        const auto outside = post(caller.headers,
            R"({"legacy":{"file":"/etc/other.lst"},"relative":{"file":"./local.txt"}})");
        CHECK((outside->status == 400 || outside->status == 403));
        CHECK(outside->status == (std::string(caller.name) == "rules" ? 403 : 400));
        const auto body = nlohmann::json::parse(outside->body);
        REQUIRE(body.at("validation_errors").size() == 1);
        CHECK(body.at("validation_errors")[0].at("path") == "lists.legacy.file");
        CHECK(body.at("validation_errors")[0].at("message") ==
              "\"/etc/other.lst\" is outside the configuration directory /etc/keen-pbr; "
              "lists may only use files inside it");
        // New lists are checked too.
        CHECK(post(caller.headers, "{" + kept + R"(,"new":{"file":"/etc/passwd"}})")->status != 200);
        CHECK(post(caller.headers, "{" + kept + R"(,"new":{"file":"/etc/keen-pbr/x.json"}})")->status != 200);
        CHECK(post(caller.headers, "{" + kept + R"(,"new":{"file":"lists/x.lst"}})")->status != 200);
        CHECK(post(caller.headers, "{" + kept + R"(,"new":{"file":"/etc/keen-pbr/../x.lst"}})")->status != 200);
        CHECK(staged == 1);
        // Inside the directory, at any depth, is fine.
        CHECK(post(caller.headers,
                   R"({"legacy":{"file":"/etc/keen-pbr/lists/sub/my list.txt"},"relative":{"file":"./local.txt"}})")
                  ->status == 200);
        CHECK(post(caller.headers, "{" + kept + R"(,"new":{"file":"/etc/keen-pbr/a.list"}})")->status == 200);
        CHECK(staged == 3);
    }

    // The web interface learns the directory from GET /api/config.
    const auto state = client.Get("/api/config", admin_headers());
    REQUIRE(state != nullptr);
    CHECK(state->status == 200);
    CHECK(nlohmann::json::parse(state->body).at("config_dir") == "/etc/keen-pbr");
    server.stop();
}

TEST_CASE("Security settings change only their own fields") {
    constexpr int port = 18246;
    SseBroadcaster broadcaster;
    ApiStore store;
    store.api = auth_config(port, {make_token("keep", "kpbr_x", {ApiScope::READ})});
    const std::string password_hash = *store.api.authentication->password_hash;
    const std::string token_hash = *(*store.api.authentication->tokens)[0].token_hash;
    auto ctx = fixture_context(broadcaster, Config{});
    store.install(ctx);
    ApiServer server(auth_config(port, {}));
    register_config_handler(server, ctx);
    server.start();
    httplib::Client client("127.0.0.1", port);

    // A token revoked while the request is on its way must stay revoked: the
    // edit starts from what is stored when it runs, not from what the handler saw.
    store.before_edit = [&]() { store.api.authentication->tokens.reset(); };
    const auto result = client.Post(
        "/api/auth/settings", admin_headers(),
        R"({"authentication":{"enabled":false},"cors":{"allowed_origins":["https://a.example"]}})",
        "application/json");
    REQUIRE(result != nullptr);
    CHECK(result->status == 200);
    CHECK(result->get_header_value("Cache-Control") == "no-store");
    CHECK_FALSE(store.api.authentication->tokens.has_value());
    CHECK(store.api.authentication->enabled == false);
    CHECK(store.api.authentication->password_hash == password_hash);
    REQUIRE(store.api.cors.has_value());
    CHECK(*store.api.cors->allowed_origins == std::vector<std::string>{"https://a.example"});
    store.before_edit = nullptr;

    // The same goes for the password and the granular endpoints.
    store.api.authentication->tokens = std::vector<ApiTokenConfig>{make_token("keep", "kpbr_x", {ApiScope::READ})};
    store.before_edit = [&]() { store.api.authentication->tokens.reset(); };
    CHECK(client.Post("/api/auth/password", admin_headers(), R"({"password":"new-secret"})",
                      "application/json")->status == 200);
    CHECK(store.api.authentication->password_hash != password_hash);
    CHECK_FALSE(store.api.authentication->tokens.has_value());
    CHECK(client.Post("/api/auth/enabled", admin_headers(), R"({"enabled":true})",
                      "application/json")->status == 200);
    CHECK(store.api.authentication->enabled == true);
    CHECK(client.Post("/api/auth/cors", admin_headers(), R"({"allowed_origins":["https://b.example"]})",
                      "application/json")->status == 200);
    CHECK(*store.api.cors->allowed_origins == std::vector<std::string>{"https://b.example"});
    CHECK(client.Post("/api/auth/cors", admin_headers(), R"({"allowed_origins":"x"})",
                      "application/json")->status == 400);
    CHECK(client.Post("/api/auth/enabled", admin_headers(), R"({})", "application/json")->status == 400);
    store.before_edit = nullptr;

    // Settings keep the stored tokens when nothing touched them.
    store.api.authentication->tokens = std::vector<ApiTokenConfig>{make_token("keep", "kpbr_x", {ApiScope::READ})};
    CHECK(client.Post("/api/auth/settings", admin_headers(),
                      R"({"authentication":{"enabled":true},"cors":{"allowed_origins":[]}})",
                      "application/json")->status == 200);
    REQUIRE(store.api.authentication->tokens.has_value());
    CHECK(store.api.authentication->tokens->size() == 1);

    // No response carries a hash or the token records.
    const auto settings = client.Get("/api/auth/settings", admin_headers());
    REQUIRE(settings != nullptr);
    CHECK(settings->status == 200);
    for (const std::string needle : {token_hash, store.api.authentication->password_hash.value(),
                                     std::string("tokens"), std::string("token_hash"),
                                     std::string("password_hash")}) {
        INFO("needle ", needle, " body ", settings->body);
        CHECK(settings->body.find(needle) == std::string::npos);
    }
    server.stop();
}

TEST_CASE("GET /api/config never carries token records or hashes") {
    constexpr int port = 18250;
    SseBroadcaster broadcaster;
    Config visible;
    visible.version = KEEN_PBR3_VERSION_STRING;
    visible.api = auth_config(port, {make_token("keep", "kpbr_x", {ApiScope::READ})});
    const std::string token_hash = *(*visible.api->authentication->tokens)[0].token_hash;
    auto ctx = fixture_context(broadcaster, visible);
    ctx.get_list_refresh_state_map_fn = [](const Config&) {
        return std::map<std::string, api::ListRefreshState>{};
    };
    ApiServer server(auth_config(port, {}));
    register_config_handler(server, ctx);
    server.start();
    httplib::Client client("127.0.0.1", port);
    const auto result = client.Get("/api/config", admin_headers());
    REQUIRE(result != nullptr);
    CHECK(result->status == 200);
    CHECK(result->body.find(token_hash) == std::string::npos);
    CHECK(result->body.find("tokens") == std::string::npos);

    const auto sanitized = sanitize_authentication_for_response(*visible.api->authentication);
    CHECK_FALSE(sanitized.tokens.has_value());
    CHECK_FALSE(sanitized.password_hash.has_value());
    CHECK(sanitized.enabled == true);
    server.stop();
}

TEST_CASE("A token that expires now is refused and one that expires later is accepted") {
    constexpr int port = 18254;
    const std::string at_limit = auth::generate_api_token();
    const std::string later = auth::generate_api_token();
    ApiServer server(auth_config(port, {
        make_token("limit", at_limit, {ApiScope::READ}, now()),
        make_token("later", later, {ApiScope::READ}, now() + 3600),
    }));
    server.get("/api/health/service", [] { return std::string("{\"ok\":true}"); });
    server.start();
    httplib::Client client("127.0.0.1", port);
    CHECK(client.Get("/api/health/service", bearer(at_limit))->status == 401);
    CHECK(client.Get("/api/health/service", bearer(later))->status == 200);
    server.stop();
}

TEST_CASE("Unknown and near-miss routes never open for a write token") {
    constexpr int port = 18255;
    const std::string writer = auth::generate_api_token();
    ApiServer server(auth_config(port, {make_token("write", writer, {ApiScope::WRITE})}));
    server.get("/api/auth/tokens", [] { return std::string("{\"tokens\":[]}"); });
    server.start();
    httplib::Client client("127.0.0.1", port);
    const auto status = [&](const char* method, const char* path) {
        const auto result = std::string(method) == "HEAD" ? client.Head(path, bearer(writer))
                                                          : client.Get(path, bearer(writer));
        REQUIRE(result != nullptr);
        return result->status;
    };
    CHECK(status("HEAD", "/api/auth/tokens") == 403);
    CHECK(status("GET", "/api/auth/tokens/") == 403);
    CHECK(status("GET", "/api/auth/zzz") == 403);
    CHECK(status("GET", "/api/zzz") == 404);
    server.stop();
}

TEST_CASE("An empty X-Auth-Token header falls back to Authorization") {
    constexpr int port = 18256;
    const std::string reader = auth::generate_api_token();
    ApiServer server(auth_config(port, {make_token("read", reader, {ApiScope::READ})}));
    server.get("/api/health/service", [] { return std::string("{\"ok\":true}"); });
    server.start();
    httplib::Client client("127.0.0.1", port);
    httplib::Headers headers = bearer(reader);
    headers.emplace("X-Auth-Token", "");
    const auto empty = client.Get("/api/health/service", headers);
    REQUIRE(empty != nullptr);
    CHECK(empty->status == 200);
    server.stop();
}

TEST_CASE("A rules-only token cannot save a staged config that changes more than rules") {
    constexpr int port = 18257;
    SseBroadcaster broadcaster;
    Config active;
    active.version = KEEN_PBR3_VERSION_STRING;
    Config staged_config = active;
    int submitted = 0;
    auto ctx = fixture_context(broadcaster, active);
    ctx.get_active_config_fn = [active]() { return active; };
    ctx.get_staged_config_snapshot_fn = [&staged_config]() {
        return std::optional<StagedConfigSnapshot>{StagedConfigSnapshot{staged_config, 1}};
    };
    ctx.submit_lifecycle_operation_fn = [&submitted](LifecycleRequest) {
        ++submitted;
        return std::string("op1");
    };
    const std::string rules = auth::generate_api_token();
    ApiServer server(auth_config(port, {make_token("rules", rules, {ApiScope::WRITE_RULES})}));
    register_config_handler(server, ctx);
    server.start();
    httplib::Client client("127.0.0.1", port);

    DaemonConfig daemon;
    daemon.ipv6_enabled = false;
    staged_config.daemon = daemon;
    CHECK(client.Post("/api/config/save", bearer(rules), "", "application/json")->status == 403);
    CHECK(submitted == 0);
    staged_config = active;
    CHECK(client.Post("/api/config/save", bearer(rules), "", "application/json")->status == 202);
    CHECK(submitted == 1);
    server.stop();
}

TEST_CASE("A write:rules token cannot remove the url or detour of a list") {
    constexpr int port = 18258;
    SseBroadcaster broadcaster;
    Config active;
    active.version = KEEN_PBR3_VERSION_STRING;
    std::map<std::string, ListConfig> lists;
    lists["old_url"].url = "https://example.com/list.txt";
    lists["old_url"].detour = "vpn";
    active.lists = lists;
    auto ctx = fixture_context(broadcaster, active);
    ctx.get_active_config_fn = [active]() { return active; };
    const std::string rules = auth::generate_api_token();
    ApiServer server(auth_config(port, {make_token("rules", rules, {ApiScope::WRITE_RULES})}));
    register_config_handler(server, ctx);
    server.start();
    httplib::Client client("127.0.0.1", port);
    const std::string version = std::string(R"("version":")") + KEEN_PBR3_VERSION_STRING + "\"";
    const auto post = [&](const std::string& lists_json) {
        auto result = client.Post("/api/config", bearer(rules),
                                  "{" + version + R"(,"lists":)" + lists_json + "}",
                                  "application/json");
        REQUIRE(result != nullptr);
        return result->status;
    };
    CHECK(post(R"({"old_url":{"url":"https://example.com/list.txt"}})") == 403);
    CHECK(post(R"({"old_url":{"detour":"vpn"}})") == 403);
    CHECK(post(R"({"old_url":{"domains":["a.example"]}})") == 403);
    CHECK(post(R"({"old_url":{"url":"https://example.com/list.txt","detour":"vpn"}})") == 200);
    server.stop();
}

TEST_CASE("Renaming a list keeps its file even when it is outside the configuration directory") {
    constexpr int port = 18259;
    SseBroadcaster broadcaster;
    Config active;
    active.version = KEEN_PBR3_VERSION_STRING;
    std::map<std::string, ListConfig> lists;
    lists["legacy"].file = "/etc/list.text";
    active.lists = lists;
    auto ctx = fixture_context(broadcaster, active, [](Config) {}, "/etc/keen-pbr/config.json");
    ctx.get_active_config_fn = [active]() { return active; };
    const std::string rules = auth::generate_api_token();
    ApiServer server(auth_config(port, {make_token("rules", rules, {ApiScope::WRITE_RULES})}));
    register_config_handler(server, ctx);
    server.start();
    httplib::Client client("127.0.0.1", port);
    const std::string version = std::string(R"("version":")") + KEEN_PBR3_VERSION_STRING + "\"";
    const auto post = [&](const std::string& lists_json) {
        auto result = client.Post("/api/config", bearer(rules),
                                  "{" + version + R"(,"lists":)" + lists_json + "}",
                                  "application/json");
        REQUIRE(result != nullptr);
        return result->status;
    };
    CHECK(post(R"({"renamed":{"file":"/etc/list.text"}})") == 200);
    CHECK(post(R"({"renamed":{"file":"/etc/other.text"}})") == 403);
    server.stop();
}

TEST_CASE("A security edit the daemon rejects is a 400 with validation errors") {
    constexpr int port = 18260;
    SseBroadcaster broadcaster;
    auto ctx = fixture_context(broadcaster, Config{});
    ctx.mutate_api_config_fn = [](const std::function<void(ApiConfig&)>&) {
        throw ConfigValidationError({{"api.authentication.password_hash",
                                      "a password is required when authentication is enabled"}});
    };
    ApiServer server(auth_config(port, {}));
    register_config_handler(server, ctx);
    server.start();
    httplib::Client client("127.0.0.1", port);
    const auto result = client.Post("/api/auth/enabled", admin_headers(), R"({"enabled":true})",
                                    "application/json");
    REQUIRE(result != nullptr);
    CHECK(result->status == 400);
    CHECK(nlohmann::json::parse(result->body).at("validation_errors").size() == 1);
    server.stop();
}

} // namespace keen_pbr3

#endif // WITH_API
