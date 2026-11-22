#include <doctest/doctest.h>

#include "../src/http/http_client.hpp"
#include "../src/http/http_transport.hpp"
#include "../src/keenetic/rci_client.hpp"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <sys/stat.h>
#include <vector>
#include <string>
#include <thread>
#include <unistd.h>

using namespace keen_pbr3;
TEST_CASE("RCI client: an unreachable RCI is reported as HttpError") {
    reset_rci_test_state();
    // Nothing listens on port 1.
    set_rci_http_base_for_tests("http://127.0.0.1:1");
    CHECK_THROWS_AS((void)rci_get("show/version"), HttpError);
    reset_rci_test_state();
}

// ------------------------------------------------------------ access tokens

namespace {

constexpr const char* kSampleToken = "ILMNnoLPNAn3q923cXs8UQIGVU5Qvqo8AVd5llM8U1GvA9wYYZOJNyU1";

// Output of `ndmc -c 'authentication token generate keen-pbr'` on 5.2 Beta 4.
std::string generate_output(const std::string& token) {
    return "\n               id: 7\n            value: \n                   " + token +
           "\n\nCore::Security::Authenticator: Added a token for user \"admin\".\n";
}

// Output of `ndmc -c 'show authentication token'` on 5.2 Beta 4.
const char* kTokenListing = R"(
            token: 
                   id: 2
          fingerprint: 48...63
      truncated-value: ILM...yU1
              service: Core::Security::Authenticator
                local: yes
                 user: admin
            user-data: keen-pbr-test
              expires: never
          last-access: never

            token: 
                   id: 5
          fingerprint: 87...1d
      truncated-value: bNf...M1O
              service: Core::Security::Authenticator
                local: yes
                 user: admin
            user-data: keen-pbr
)";

class FakeTransport : public HttpTransport {
public:
    std::function<HttpTransportResponse(const HttpTransportRequest&)> handler;
    std::vector<HttpTransportRequest> requests;
    std::mutex mutex;

    HttpTransportResponse perform(const HttpTransportRequest& request) override {
        {
            std::lock_guard<std::mutex> lock(mutex);
            requests.push_back(request);
        }
        return handler(request);
    }
};

std::string token_header(const HttpTransportRequest& request) {
    for (const auto& header : request.headers) {
        if (header.rfind("X-Ndma-Tkn: ", 0) == 0) return header.substr(12);
    }
    return "";
}

HttpTransportResponse respond(long status, std::string body = "{}") {
    HttpTransportResponse response;
    response.status_code = status;
    response.body = std::move(body);
    return response;
}

struct TokenDir {
    TokenDir() {
        char pattern[] = "/tmp/keen-pbr-rci-token-XXXXXX";
        REQUIRE(::mkdtemp(pattern) != nullptr);
        dir = pattern;
        file = dir + "/rci-token";
    }
    ~TokenDir() { std::filesystem::remove_all(dir); }
    std::string read() const {
        std::ifstream input(file);
        std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        return text;
    }
    std::string dir;
    std::string file;
};

struct TokenTest {
    TokenTest() {
        reset_rci_test_state();
        transport = std::make_shared<FakeTransport>();
        set_rci_http_transport_for_tests(transport);
        set_rci_ndmc_for_tests([this](const std::vector<std::string>& args) {
            ndmc_calls.push_back(args.at(2));
            return ndmc_handler(args.at(2));
        });
        set_rci_clock_for_tests([this] { return clock_now; });
    }
    ~TokenTest() { reset_rci_test_state(); }

    std::shared_ptr<FakeTransport> transport;
    std::chrono::steady_clock::time_point clock_now{std::chrono::steady_clock::now()};
    std::vector<std::string> ndmc_calls;
    std::function<RciNdmcResult(const std::string&)> ndmc_handler = [](const std::string&) {
        return RciNdmcResult{};
    };
};

} // namespace

TEST_CASE("RCI tokens: parse the generated token and the token listing") {
    const auto generated = rci_detail::parse_generated_token(generate_output(kSampleToken));
    REQUIRE(generated.has_value());
    CHECK(generated->id == "7");
    CHECK(generated->value == kSampleToken);
    const auto inline_value =
        rci_detail::parse_generated_token("id: 3\nvalue: " + std::string(kSampleToken));
    REQUIRE(inline_value.has_value());
    CHECK(inline_value->value == kSampleToken);
    CHECK_FALSE(rci_detail::parse_generated_token("value: " + std::string(kSampleToken))
                    .has_value()); // no id: the stale-token cleanup would not know which to keep
    CHECK_FALSE(rci_detail::parse_generated_token(
                    "Command::Base error[7405602]: argument parse error.")
                    .has_value());
    CHECK_FALSE(rci_detail::parse_generated_token("value:\n  short\n").has_value());

    CHECK(rci_detail::parse_token_ids(kTokenListing, "keen-pbr") == std::vector<std::string>{"5"});
    CHECK(rci_detail::parse_token_ids(kTokenListing, "keen-pbr-test") ==
          std::vector<std::string>{"2"});
}

namespace {

RciNdmcResult generate_ok(const std::string& command) {
    if (command.rfind("authentication token generate", 0) == 0) {
        return RciNdmcResult{0, generate_output(kSampleToken)};
    }
    return RciNdmcResult{0, ""};
}

// 5.1.9: ndmc knows no token commands.
RciNdmcResult generate_unsupported(const std::string&) {
    return RciNdmcResult{0, "Command::Base error[7405602]: argument parse error."};
}

constexpr const char* kGenerate = "authentication token generate keen-pbr";

std::ptrdiff_t count_calls(const TokenTest& test, const std::string& command) {
    return std::count(test.ndmc_calls.begin(), test.ndmc_calls.end(), command);
}

std::ptrdiff_t count_posts(const TokenTest& test) {
    return std::count_if(test.transport->requests.begin(), test.transport->requests.end(),
                         [](const HttpTransportRequest& r) { return r.method == "POST"; });
}

void write_token_file(const TokenDir& dir) { std::ofstream(dir.file) << kSampleToken << "\n"; }

constexpr const char* kPostBody = R"({"show":{"version":{}}})";

} // namespace

TEST_CASE("RCI tokens: firmware without tokens uses tokenless HTTP and asks ndmc only once") {
    TokenTest test;
    TokenDir dir;
    configure_rci_token_file(dir.file, true);
    test.ndmc_handler = generate_unsupported;
    test.transport->handler = [](const HttpTransportRequest&) { return respond(200, "{\"a\":1}"); };

    CHECK(rci_get("show/version") == "{\"a\":1}");
    REQUIRE(test.transport->requests.size() == 1);
    CHECK(test.transport->requests[0].url == "http://127.0.0.1:79/rci/show/version");
    CHECK(token_header(test.transport->requests[0]).empty());
    CHECK(test.ndmc_calls == std::vector<std::string>{kGenerate});
    CHECK_FALSE(std::filesystem::exists(dir.file));

    // No token support is remembered: no more ndmc calls, even much later.
    test.transport->requests.clear();
    test.clock_now += std::chrono::hours(1);
    CHECK(rci_get("show/dns-proxy") == "{\"a\":1}");
    CHECK(test.transport->requests.size() == 1);
    CHECK(test.ndmc_calls.size() == 1);
}

TEST_CASE("RCI tokens: the service creates a token first, replaces old keen-pbr tokens and saves it") {
    TokenTest test;
    TokenDir dir;
    configure_rci_token_file(dir.file, true);
    test.ndmc_handler = [](const std::string& command) {
        if (command == "show authentication token") return RciNdmcResult{0, kTokenListing};
        return generate_ok(command);
    };
    // Tokenless access would still work on 5.2; the token is used regardless.
    test.transport->handler = [](const HttpTransportRequest&) { return respond(200); };

    CHECK(rci_post(kPostBody) == "{}");
    // One request, with the new token.
    REQUIRE(test.transport->requests.size() == 1);
    CHECK(token_header(test.transport->requests[0]) == kSampleToken);
    CHECK(test.transport->requests[0].method == "POST");
    // Created and saved first; old keen-pbr tokens go one by one afterwards,
    // each delete checked against a fresh listing.
    CHECK(test.ndmc_calls == std::vector<std::string>{kGenerate, "show authentication token",
                                                      "authentication token delete 5",
                                                      "show authentication token"});
    CHECK(dir.read() == std::string(kSampleToken) + "\n");
    struct stat st{};
    REQUIRE(::stat(dir.file.c_str(), &st) == 0);
    CHECK((st.st_mode & 0777) == 0600);

    // Later requests go straight to the token.
    test.transport->requests.clear();
    CHECK(rci_get("show/dns-proxy") == "{}");
    REQUIRE(test.transport->requests.size() == 1);
    CHECK(token_header(test.transport->requests[0]) == kSampleToken);
    CHECK(test.ndmc_calls.size() == 4);
}

TEST_CASE("RCI tokens: a saved token is reused without ndmc") {
    TokenTest test;
    TokenDir dir;
    write_token_file(dir);
    configure_rci_token_file(dir.file, true);
    test.transport->handler = [](const HttpTransportRequest& request) {
        return token_header(request) == kSampleToken ? respond(200) : respond(403);
    };

    CHECK(rci_get("show/version") == "{}");
    REQUIRE(test.transport->requests.size() == 1);
    CHECK(token_header(test.transport->requests[0]) == kSampleToken);
    CHECK(test.ndmc_calls.empty());
}

TEST_CASE("RCI tokens: a rejected saved token falls back to tokenless HTTP") {
    TokenTest test;
    TokenDir dir;
    write_token_file(dir);
    // 5.2 Beta 4: a wrong token gets 403, no token at all still gets 200.
    test.transport->handler = [](const HttpTransportRequest& request) {
        return token_header(request).empty() ? respond(200) : respond(403);
    };

    SUBCASE("service whose ndmc is unavailable") {
        test.ndmc_handler = [](const std::string&) { return RciNdmcResult{127, ""}; };
        configure_rci_token_file(dir.file, true);
        CHECK(rci_get("show/version") == "{}");
        CHECK(test.transport->requests.size() == 2); // token 403, tokenless 200
        CHECK(test.ndmc_calls == std::vector<std::string>{kGenerate});
        // The refused token's file stays until a new token replaces it.
        CHECK(dir.read() == std::string(kSampleToken) + "\n");
    }
    SUBCASE("process that may not create tokens") {
        configure_rci_token_file(dir.file, false);
        CHECK(rci_get("show/version") == "{}");
        CHECK(test.transport->requests.size() == 2);
        CHECK(test.ndmc_calls.empty());
        CHECK(dir.read() == std::string(kSampleToken) + "\n");
    }
}

TEST_CASE("RCI tokens: firmware without tokens reports the refusal and creates none") {
    TokenTest test;
    TokenDir dir;
    configure_rci_token_file(dir.file, true);
    // 5.1.9: every HTTP request is refused and ndmc knows no token commands.
    test.ndmc_handler = generate_unsupported;
    test.transport->handler = [](const HttpTransportRequest&) { return respond(403); };

    CHECK_THROWS_AS((void)rci_get("show/version"), HttpError);
    const auto ndmc_calls = test.ndmc_calls.size();
    CHECK_THROWS_AS((void)rci_get("show/dns-proxy"), HttpError);
    CHECK(test.ndmc_calls.size() == ndmc_calls); // blocked for this run
    CHECK_FALSE(std::filesystem::exists(dir.file));
}

TEST_CASE("RCI tokens: the error names the missing admin permission") {
    TokenTest test;
    TokenDir dir;
    configure_rci_token_file(dir.file, true);
    test.ndmc_handler = [](const std::string& command) {
        if (command.rfind("authentication token generate", 0) == 0) {
            return RciNdmcResult{1, "Core::Security::Authenticator error: invalid username."};
        }
        return RciNdmcResult{0, ""};
    };
    test.transport->handler = [](const HttpTransportRequest&) { return respond(401); };

    try {
        (void)rci_get("show/version");
        FAIL("expected HttpError");
    } catch (const HttpError& e) {
        CHECK(std::string(e.what()).find("\"Web configurator\" permission") != std::string::npos);
        CHECK(e.status_code() == 401);
    }
}

TEST_CASE("RCI tokens: a process that may not create tokens says the service does") {
    TokenTest test;
    TokenDir dir;
    configure_rci_token_file(dir.file, false);
    test.transport->handler = [](const HttpTransportRequest&) { return respond(401); };

    try {
        (void)rci_get("show/version");
        FAIL("expected HttpError");
    } catch (const HttpError& e) {
        CHECK(std::string(e.what()).find("the keen-pbr service creates it") != std::string::npos);
        CHECK(e.status_code() == 401);
    }
    CHECK(test.ndmc_calls.empty());
}

TEST_CASE("RCI tokens: errors other than 401/403 are reported without creating a token") {
    TokenTest test;
    TokenDir dir;
    write_token_file(dir);
    test.transport->handler = [](const HttpTransportRequest&) { return respond(404); };

    SUBCASE("service") { configure_rci_token_file(dir.file, true); }
    SUBCASE("process that may not create tokens") { configure_rci_token_file(dir.file, false); }

    CHECK_THROWS_AS((void)rci_get("show/nothing"), HttpError);
    CHECK(test.transport->requests.size() == 1);
    CHECK(test.ndmc_calls.empty());
    CHECK(dir.read() == std::string(kSampleToken) + "\n");
}

TEST_CASE("RCI tokens: one-shot commands never create tokens") {
    TokenTest test; // not configured to create tokens
    test.transport->handler = [](const HttpTransportRequest&) { return respond(401); };

    CHECK_THROWS_AS((void)rci_get("show/version"), HttpError);
    CHECK(test.ndmc_calls.empty());
}

TEST_CASE("RCI tokens: a token refused right after creation stops further attempts") {
    TokenTest test;
    TokenDir dir;
    configure_rci_token_file(dir.file, true);
    test.ndmc_handler = generate_ok;
    test.transport->handler = [](const HttpTransportRequest& request) {
        return token_header(request).empty() ? respond(200) : respond(403);
    };

    CHECK(rci_get("show/version") == "{}");
    CHECK(test.transport->requests.size() == 2); // the new token, then tokenless
    CHECK(count_calls(test, kGenerate) == 1);

    // Tokenless access keeps working and no more tokens are made in this run.
    test.transport->requests.clear();
    test.clock_now += std::chrono::hours(1);
    CHECK(rci_get("show/dns-proxy") == "{}");
    CHECK(test.transport->requests.size() == 1);
    CHECK(token_header(test.transport->requests[0]).empty());
    CHECK(count_calls(test, kGenerate) == 1);
}

TEST_CASE("RCI tokens: commands that may not create tokens use the saved token only") {
    TokenTest test;
    TokenDir dir;
    write_token_file(dir);
    configure_rci_token_file(dir.file, false);
    test.transport->handler = [](const HttpTransportRequest& request) {
        return token_header(request).empty() ? respond(401) : respond(200, "{\"ok\":1}");
    };

    CHECK(rci_get("show/version") == "{\"ok\":1}");
    CHECK(test.ndmc_calls.empty());

    // The saved token is revoked: still no token is created.
    test.transport->handler = [](const HttpTransportRequest&) { return respond(403); };
    reset_rci_test_state();
    set_rci_http_transport_for_tests(test.transport);
    set_rci_ndmc_for_tests([&test](const std::vector<std::string>& args) {
        test.ndmc_calls.push_back(args.at(2));
        return RciNdmcResult{};
    });
    configure_rci_token_file(dir.file, false);
    CHECK_THROWS_AS((void)rci_get("show/version"), HttpError);
    CHECK(test.ndmc_calls.empty());
}

TEST_CASE("RCI tokens: a timeout on the working token neither searches nor creates a token") {
    TokenTest test;
    TokenDir dir;
    write_token_file(dir);
    configure_rci_token_file(dir.file, true);
    bool fail = false;
    test.transport->handler = [&fail](const HttpTransportRequest&) -> HttpTransportResponse {
        if (fail) throw HttpTransportError("timeout");
        return respond(200, "{}");
    };

    CHECK(rci_get("show/version") == "{}");
    fail = true;
    test.transport->requests.clear();
    CHECK_THROWS_AS((void)rci_get("show/version"), HttpError);
    CHECK(test.transport->requests.size() == 1); // no new search
    CHECK(test.ndmc_calls.empty());

    fail = false;
    CHECK(rci_get("show/version") == "{}");
    CHECK(token_header(test.transport->requests.back()) == kSampleToken);
}

TEST_CASE("RCI tokens: a saved token in a fresh process survives a timeout") {
    TokenTest test;
    TokenDir dir;
    write_token_file(dir);
    configure_rci_token_file(dir.file, true);
    test.transport->handler = [](const HttpTransportRequest&) -> HttpTransportResponse {
        throw HttpTransportError("timeout");
    };

    // The very first request times out: no tokenless attempt, no new token
    // (that would delete the token another instance may use), file untouched.
    CHECK_THROWS_AS((void)rci_get("show/version"), HttpError);
    REQUIRE(test.transport->requests.size() == 1);
    CHECK(token_header(test.transport->requests[0]) == kSampleToken);
    CHECK(test.ndmc_calls.empty());
    CHECK(dir.read() == std::string(kSampleToken) + "\n");

    test.transport->handler = [](const HttpTransportRequest& request) {
        return token_header(request) == kSampleToken ? respond(200, "{\"t\":1}") : respond(403);
    };
    test.transport->requests.clear();
    CHECK(rci_get("show/version") == "{\"t\":1}");
    REQUIRE(test.transport->requests.size() == 1);
    CHECK(token_header(test.transport->requests[0]) == kSampleToken);
    CHECK(test.ndmc_calls.empty());
}

TEST_CASE("RCI tokens: a timeout while proving a fresh token keeps the token") {
    TokenTest test;
    TokenDir dir;
    configure_rci_token_file(dir.file, true);
    test.ndmc_handler = generate_ok;
    test.transport->handler = [](const HttpTransportRequest&) -> HttpTransportResponse {
        throw HttpTransportError("timeout");
    };

    CHECK_THROWS_AS((void)rci_get("show/dns-proxy", {}), HttpError);
    CHECK(test.transport->requests.size() == 1); // not repeated, not tokenless
    CHECK(dir.read() == std::string(kSampleToken) + "\n");

    // The next request uses the token instead of creating another.
    test.transport->handler = [](const HttpTransportRequest& request) {
        return token_header(request).empty() ? respond(403) : respond(200, "{\"t\":1}");
    };
    CHECK(rci_get("show/dns-proxy") == "{\"t\":1}");
    CHECK(count_calls(test, kGenerate) == 1);
}

TEST_CASE("RCI tokens: a failed token creation is retried after ten minutes") {
    TokenTest test;
    TokenDir dir;
    configure_rci_token_file(dir.file, true);
    bool permitted = false;
    test.ndmc_handler = [&permitted](const std::string& command) {
        if (command.rfind("authentication token generate", 0) == 0 && !permitted) {
            return RciNdmcResult{1, "Core::Security::Authenticator error: invalid username."};
        }
        return generate_ok(command);
    };
    test.transport->handler = [](const HttpTransportRequest& request) {
        return token_header(request).empty() ? respond(200) : respond(200, "{\"t\":1}");
    };

    CHECK(rci_get("show/version") == "{}"); // tokenless meanwhile
    CHECK(test.transport->requests.size() == 1);
    CHECK(test.ndmc_calls == std::vector<std::string>{kGenerate});
    CHECK_FALSE(std::filesystem::exists(dir.file));

    // Within the interval: one request, no ndmc.
    permitted = true;
    test.clock_now += std::chrono::minutes(5);
    test.transport->requests.clear();
    CHECK(rci_get("show/version") == "{}");
    CHECK(test.transport->requests.size() == 1);
    CHECK(test.ndmc_calls.size() == 1);

    // After it: the token is created and used for this very request.
    test.clock_now += std::chrono::minutes(6);
    test.transport->requests.clear();
    CHECK(rci_get("show/version") == "{\"t\":1}");
    REQUIRE(test.transport->requests.size() == 1);
    CHECK(token_header(test.transport->requests[0]) == kSampleToken);
    CHECK(count_calls(test, kGenerate) == 2);
    CHECK(dir.read() == std::string(kSampleToken) + "\n");

    struct stat before{};
    REQUIRE(::stat(dir.file.c_str(), &before) == 0);
    CHECK(rci_get("show/version") == "{\"t\":1}");
    struct stat after{};
    REQUIRE(::stat(dir.file.c_str(), &after) == 0);
    CHECK(after.st_ino == before.st_ino); // written once
    CHECK(count_calls(test, kGenerate) == 2);
}

TEST_CASE("RCI tokens: a missing ndmc is not asked again") {
    TokenTest test;
    TokenDir dir;
    configure_rci_token_file(dir.file, true);
    test.ndmc_handler = [](const std::string&) { return RciNdmcResult{127, ""}; };
    test.transport->handler = [](const HttpTransportRequest&) { return respond(200); };

    CHECK(rci_get("show/version") == "{}");
    CHECK(test.ndmc_calls == std::vector<std::string>{kGenerate});
    for (int i = 0; i < 3; ++i) {
        test.clock_now += std::chrono::minutes(30);
        CHECK(rci_get("show/version") == "{}");
    }
    CHECK(test.ndmc_calls.size() == 1);
    CHECK_FALSE(std::filesystem::exists(dir.file));
}

TEST_CASE("RCI tokens: a POST is sent at most once while a way in is found") {
    TokenTest test;
    TokenDir dir;
    std::atomic<int> executed{0}; // POSTs answered with 2xx
    auto answer = [&executed](const HttpTransportRequest& request, long status) {
        if (request.method == "POST" && status >= 200 && status < 300) ++executed;
        return respond(status);
    };

    SUBCASE("a fresh token works") {
        configure_rci_token_file(dir.file, true);
        test.ndmc_handler = generate_ok;
        test.transport->handler = [&](const HttpTransportRequest& r) { return answer(r, 200); };
        (void)rci_post(kPostBody);
        CHECK(count_posts(test) == 1);
    }
    SUBCASE("a fresh token times out") {
        configure_rci_token_file(dir.file, true);
        test.ndmc_handler = generate_ok;
        test.transport->handler = [](const HttpTransportRequest&) -> HttpTransportResponse {
            throw HttpTransportError("timeout");
        };
        CHECK_THROWS_AS((void)rci_post(kPostBody), HttpError);
        CHECK(count_posts(test) == 1);
    }
    SUBCASE("a fresh token gets a server error") {
        configure_rci_token_file(dir.file, true);
        test.ndmc_handler = generate_ok;
        test.transport->handler = [&](const HttpTransportRequest& r) { return answer(r, 500); };
        CHECK_THROWS_AS((void)rci_post(kPostBody), HttpError);
        CHECK(count_posts(test) == 1);
    }
    SUBCASE("a saved token times out") {
        write_token_file(dir);
        configure_rci_token_file(dir.file, true);
        test.ndmc_handler = generate_ok;
        test.transport->handler = [](const HttpTransportRequest&) -> HttpTransportResponse {
            throw HttpTransportError("timeout");
        };
        CHECK_THROWS_AS((void)rci_post(kPostBody), HttpError);
        CHECK(count_posts(test) == 1);
        CHECK(test.ndmc_calls.empty());
    }
    SUBCASE("refusals do not run the request, so it runs once in the end") {
        write_token_file(dir);
        configure_rci_token_file(dir.file, true);
        test.ndmc_handler = generate_ok;
        test.transport->handler = [&](const HttpTransportRequest& r) {
            return answer(r, token_header(r).empty() ? 200 : 403);
        };
        (void)rci_post(kPostBody);
        CHECK(executed.load() == 1);
        CHECK(count_posts(test) == 3); // saved token, fresh token, tokenless
    }
}

TEST_CASE("RCI tokens: a refusal on the known way is not repeated by the search") {
    TokenTest test;
    TokenDir dir;
    write_token_file(dir);
    configure_rci_token_file(dir.file, true);
    test.ndmc_handler = [](const std::string&) { return RciNdmcResult{127, ""}; };
    bool revoked = false;
    test.transport->handler = [&revoked](const HttpTransportRequest& request) {
        if (token_header(request).empty()) return respond(200, "{\"plain\":1}");
        return revoked ? respond(403) : respond(200);
    };

    CHECK(rci_get("show/version") == "{}");
    revoked = true;
    test.transport->requests.clear();
    CHECK(rci_get("show/version") == "{\"plain\":1}");
    CHECK(test.transport->requests.size() == 2); // the token once, then tokenless
}

TEST_CASE("RCI client: rci_answers is true while RCI is up, token or not") {
    TokenTest test;
    long status = 200;
    bool timeout = false;
    test.transport->handler = [&](const HttpTransportRequest&) -> HttpTransportResponse {
        if (timeout) throw HttpTransportError("timeout");
        return respond(status);
    };

    CHECK(rci_answers());
    status = 401;
    {
        std::string note;
        CHECK(rci_answers({}, &note));
        CHECK_FALSE(note.empty());
    }
    status = 403;
    CHECK(rci_answers());
    CHECK(test.ndmc_calls.empty()); // never creates tokens here
    status = 500;
    {
        std::string note;
        CHECK_FALSE(rci_answers({}, &note));
        CHECK(note.find("500") != std::string::npos);
    }
    timeout = true;
    CHECK_FALSE(rci_answers());
}

TEST_CASE("RCI client: netfilter components the router lacks") {
    using rci_detail::missing_netfilter_components;
    const std::vector<std::string> none;
    CHECK(missing_netfilter_components(R"("base opkg-kmod-netfilter opkg-kmod-netfilter-addons")") ==
          none);
    CHECK(missing_netfilter_components(R"("base opkg-kmod-netfilter")") ==
          std::vector<std::string>{"opkg-kmod-netfilter-addons"});
    CHECK(missing_netfilter_components(R"(["base","opkg-kmod-netfilter-addons"])") ==
          std::vector<std::string>{"opkg-kmod-netfilter"});
    CHECK(missing_netfilter_components(R"("base, ndw")") ==
          std::vector<std::string>{"opkg-kmod-netfilter", "opkg-kmod-netfilter-addons"});

    // Not an answer naming components: a failure, never "everything is missing".
    CHECK_THROWS(missing_netfilter_components(R"({"error":"unauthorized"})"));
    CHECK_THROWS(missing_netfilter_components(""));
    CHECK_THROWS(missing_netfilter_components(R"("")"));
    CHECK_THROWS(missing_netfilter_components("[]"));
}

TEST_CASE("RCI tokens: a stale or linked temporary file does not decide the token file") {
    TokenTest test;
    TokenDir dir;
    const std::string outside = dir.dir + "/outside";
    std::ofstream(outside) << "keep\n";
    REQUIRE(::symlink(outside.c_str(), (dir.file + ".tmp").c_str()) == 0);
    configure_rci_token_file(dir.file, true);
    test.ndmc_handler = generate_ok;
    test.transport->handler = [](const HttpTransportRequest& request) {
        return token_header(request).empty() ? respond(401) : respond(200);
    };

    CHECK_NOTHROW((void)rci_get("show/version"));
    CHECK(dir.read() == std::string(kSampleToken) + "\n");
    std::ifstream untouched(outside);
    std::string line;
    std::getline(untouched, line);
    CHECK(line == "keep");
    struct stat info {};
    REQUIRE(::stat(dir.file.c_str(), &info) == 0);
    CHECK((info.st_mode & 0777) == 0600);
}

TEST_CASE("RCI client: concurrent requests on a known way in do not wait for each other") {
    TokenTest test;
    std::atomic<int> in_flight{0};
    std::atomic<int> peak{0};
    test.transport->handler = [&](const HttpTransportRequest&) {
        const int now = ++in_flight;
        int seen = peak.load();
        while (now > seen && !peak.compare_exchange_weak(seen, now)) {}
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        --in_flight;
        return respond(200, "{}");
    };
    CHECK(rci_get("show/version") == "{}"); // finds plain HTTP
    peak = 0;
    std::vector<std::thread> threads;
    for (int i = 0; i < 4; ++i) {
        threads.emplace_back([] { (void)rci_get("show/dns-proxy"); });
    }
    for (auto& thread : threads) thread.join();
    CHECK(peak.load() > 1);
}
