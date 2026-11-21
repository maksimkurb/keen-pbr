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
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>

using namespace keen_pbr3;
using rci_detail::build_scgi_request;
using rci_detail::parse_scgi_response;

namespace {

std::string nul_pairs(std::initializer_list<std::pair<const char*, const char*>> vars) {
    std::string out;
    for (const auto& [name, value] : vars) {
        out += name;
        out.push_back('\0');
        out += value;
        out.push_back('\0');
    }
    return out;
}

// One-shot SCGI server on a unix socket: records the request, answers with a
// canned response, then closes (optionally with a reset, as NDM does).
class FakeScgiServer {
public:
    FakeScgiServer(std::string response, bool reset_after)
        : response_(std::move(response)), reset_after_(reset_after) {
        char pattern[] = "/tmp/keen-pbr-rci-XXXXXX";
        REQUIRE(::mkdtemp(pattern) != nullptr);
        dir_ = pattern;
        path_ = dir_ + "/ndm.scgi.socket";
        listen_fd_ = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        REQUIRE(listen_fd_ >= 0);
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, path_.c_str(), sizeof(addr.sun_path) - 1);
        REQUIRE(::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
        REQUIRE(::listen(listen_fd_, 1) == 0);
        thread_ = std::thread([this] { serve(); });
    }

    ~FakeScgiServer() {
        ::shutdown(listen_fd_, SHUT_RDWR);
        if (thread_.joinable()) thread_.join();
        ::close(listen_fd_);
        std::filesystem::remove_all(dir_);
    }

    const std::string& path() const { return path_; }
    std::string request() {
        if (thread_.joinable()) thread_.join();
        return request_;
    }

private:
    void serve() {
        const int fd = ::accept(listen_fd_, nullptr, nullptr);
        if (fd < 0) return;
        char buffer[4096];
        for (;;) {
            const ssize_t n = ::recv(fd, buffer, sizeof(buffer), 0);
            if (n <= 0) break; // the client half-closes after the request
            request_.append(buffer, static_cast<size_t>(n));
        }
        (void)::send(fd, response_.data(), response_.size(), MSG_NOSIGNAL);
        if (reset_after_) {
            linger lg{1, 0};
            ::setsockopt(fd, SOL_SOCKET, SO_LINGER, &lg, sizeof(lg));
        }
        ::close(fd);
    }

    std::string response_;
    bool reset_after_;
    std::string dir_;
    std::string path_;
    int listen_fd_{-1};
    std::thread thread_;
    std::string request_;
};

struct RciTestState {
    RciTestState() {
        reset_rci_test_state();
        // Nothing listens on port 1: the HTTP fallback fails fast.
        set_rci_http_base_for_tests("http://127.0.0.1:1");
    }
    ~RciTestState() { reset_rci_test_state(); }
};

} // namespace

TEST_CASE("RCI SCGI: GET request carries the CGI variables NDM requires") {
    const std::string request = build_scgi_request("GET", "/rci/show/version", "");
    const std::string headers = nul_pairs({
        {"CONTENT_LENGTH", "0"},
        {"SCGI", "1"},
        {"REQUEST_METHOD", "GET"},
        {"REQUEST_URI", "/rci/show/version"},
        {"NORMALIZED_URI", "/rci/show/version"},
        {"HTTP_HOST", "127.0.0.1:79"},
        {"SERVER_NAME", "127.0.0.1"},
        {"SERVER_PORT", "79"},
        {"SERVER_PROTOCOL", "HTTP/1.1"},
        {"REMOTE_ADDR", "127.0.0.1"},
        {"AUTH_ALLOWED", "1"},
    });
    CHECK(request == std::to_string(headers.size()) + ":" + headers + ",");
}

TEST_CASE("RCI SCGI: POST request appends the body and its content type") {
    const std::string body = R"({"show":{"version":{}}})";
    const std::string request = build_scgi_request("POST", "/rci/", body);
    CHECK(request.find(nul_pairs({{"CONTENT_LENGTH", "23"}})) != std::string::npos);
    CHECK(request.find(nul_pairs({{"CONTENT_TYPE", "application/json"}})) != std::string::npos);
    CHECK(request.size() > body.size());
    CHECK(request.compare(request.size() - body.size() - 1, std::string::npos, "," + body) == 0);
}

TEST_CASE("RCI SCGI: parses the response NDM sends") {
    const std::string raw =
        "Status: 200 OK\r\nContent-Type: application/json\r\nContent-Length: 17\r\n\r\n"
        "{\"title\":\"5.1.9\"}";
    const auto parsed = parse_scgi_response(raw, false);
    REQUIRE(parsed.has_value());
    CHECK(parsed->status == 200);
    CHECK(parsed->body == "{\"title\":\"5.1.9\"}");

    CHECK_FALSE(parse_scgi_response(raw.substr(0, raw.size() - 3), false).has_value());
    CHECK_FALSE(parse_scgi_response("Status: 200 OK\r\nContent-Le", false).has_value());
}

TEST_CASE("RCI SCGI: keeps the status and X-Detail of an error") {
    const auto parsed = parse_scgi_response(
        "Status: 400 Bad Request\r\nX-Detail: 0xcc56, missing host header\r\n"
        "Content-Length: 4\r\n\r\nnope",
        false);
    REQUIRE(parsed.has_value());
    CHECK(parsed->status == 400);
    CHECK(parsed->detail == "0xcc56, missing host header");
}

TEST_CASE("RCI SCGI: a response without Content-Length ends at end of stream") {
    const std::string raw = "Status: 200 OK\r\n\r\n{}";
    CHECK_FALSE(parse_scgi_response(raw, false).has_value());
    const auto parsed = parse_scgi_response(raw, true);
    REQUIRE(parsed.has_value());
    CHECK(parsed->body == "{}");
}

TEST_CASE("RCI client: answers from the SCGI socket, even when NDM resets afterwards") {
    RciTestState state;
    FakeScgiServer server(
        "Status: 200 OK\r\nContent-Type: application/json\r\nContent-Length: 2\r\n\r\n{}", true);
    set_rci_scgi_socket_path_for_tests(server.path());

    CHECK(rci_get("show/dns-proxy") == "{}");
    const std::string request = server.request();
    CHECK(request.find(nul_pairs({{"REQUEST_URI", "/rci/show/dns-proxy"}})) != std::string::npos);
}

TEST_CASE("RCI client: POST goes to /rci/ over the socket") {
    RciTestState state;
    FakeScgiServer server("Status: 200 OK\r\nContent-Length: 11\r\n\r\n{\"show\":{}}", false);
    set_rci_scgi_socket_path_for_tests(server.path());

    CHECK(rci_post(R"({"show":{}})") == "{\"show\":{}}");
    const std::string request = server.request();
    CHECK(request.find(nul_pairs({{"REQUEST_METHOD", "POST"}, {"REQUEST_URI", "/rci/"}})) !=
          std::string::npos);
    CHECK(request.size() >= 11);
    CHECK(request.substr(request.size() - 11) == R"({"show":{}})");
}

TEST_CASE("RCI client: a rejected SCGI request falls back to HTTP and reports both") {
    RciTestState state;
    FakeScgiServer server(
        "Status: 400 Bad Request\r\nX-Detail: 0xcc56, missing host header\r\n"
        "Content-Length: 0\r\n\r\n",
        false);
    set_rci_scgi_socket_path_for_tests(server.path());

    try {
        (void)rci_get("show/version");
        FAIL("expected HttpError");
    } catch (const HttpError& e) {
        const std::string message = e.what();
        CHECK(message.find("SCGI socket: status 400 (0xcc56, missing host header)") !=
              std::string::npos);
    }
}

TEST_CASE("RCI client: without the socket only HTTP is tried") {
    RciTestState state;
    set_rci_scgi_socket_path_for_tests("/nonexistent/ndm.scgi.socket");
    try {
        (void)rci_get("show/version");
        FAIL("expected HttpError");
    } catch (const HttpError& e) {
        CHECK(std::string(e.what()).find("SCGI") == std::string::npos);
    }
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
        set_rci_scgi_socket_path_for_tests("/nonexistent/ndm.scgi.socket");
        set_rci_ndmc_for_tests([this](const std::vector<std::string>& args) {
            ndmc_calls.push_back(args.at(2));
            return ndmc_handler(args.at(2));
        });
    }
    ~TokenTest() { reset_rci_test_state(); }

    std::shared_ptr<FakeTransport> transport;
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

TEST_CASE("RCI tokens: tokenless HTTP is used while the router allows it") {
    TokenTest test;
    TokenDir dir;
    configure_rci_token_file(dir.file);
    test.transport->handler = [](const HttpTransportRequest&) { return respond(200, "{\"a\":1}"); };

    CHECK(rci_get("show/version") == "{\"a\":1}");
    REQUIRE(test.transport->requests.size() == 1);
    CHECK(test.transport->requests[0].url == "http://127.0.0.1:79/rci/show/version");
    CHECK(token_header(test.transport->requests[0]).empty());
    CHECK(test.ndmc_calls.empty());
    CHECK_FALSE(std::filesystem::exists(dir.file));
}

TEST_CASE("RCI tokens: 401 creates a token, replaces old keen-pbr tokens and saves it") {
    TokenTest test;
    TokenDir dir;
    configure_rci_token_file(dir.file);
    test.ndmc_handler = [](const std::string& command) {
        if (command == "show authentication token") return RciNdmcResult{0, kTokenListing};
        if (command == "authentication token generate keen-pbr") {
            return RciNdmcResult{0, generate_output(kSampleToken)};
        }
        return RciNdmcResult{0, ""};
    };
    test.transport->handler = [](const HttpTransportRequest& request) {
        return token_header(request) == kSampleToken ? respond(200) : respond(401);
    };

    CHECK(rci_post(R"({"show":{"version":{}}})") == "{}");
    // Created and saved first; old keen-pbr tokens go one by one afterwards,
    // each delete checked against a fresh listing.
    CHECK(test.ndmc_calls == std::vector<std::string>{"authentication token generate keen-pbr",
                                                      "show authentication token",
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
    { std::ofstream(dir.file) << kSampleToken << "\n"; }
    configure_rci_token_file(dir.file);
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
    { std::ofstream(dir.file) << kSampleToken << "\n"; }
    configure_rci_token_file(dir.file);
    // 5.2 Beta 4: a wrong token gets 403, no token at all still gets 200.
    test.transport->handler = [](const HttpTransportRequest& request) {
        return token_header(request).empty() ? respond(200) : respond(403);
    };

    CHECK(rci_get("show/version") == "{}");
    CHECK(test.transport->requests.size() == 2);
    CHECK(test.ndmc_calls.empty());
}

TEST_CASE("RCI tokens: firmware without tokens falls through to the SCGI socket once") {
    TokenTest test;
    TokenDir dir;
    configure_rci_token_file(dir.file);
    // 5.1.9: every HTTP request is refused and ndmc knows no token commands.
    test.ndmc_handler = [](const std::string&) {
        return RciNdmcResult{0, "Command::Base error[7405602]: argument parse error."};
    };
    test.transport->handler = [](const HttpTransportRequest&) { return respond(403); };

    {
        FakeScgiServer server("Status: 200 OK\r\nContent-Length: 2\r\n\r\n{}", true);
        set_rci_scgi_socket_path_for_tests(server.path());
        CHECK(rci_get("show/version") == "{}");
    }
    const auto http_calls = test.transport->requests.size();
    const auto ndmc_calls = test.ndmc_calls.size();
    {
        FakeScgiServer server("Status: 200 OK\r\nContent-Length: 2\r\n\r\n[]", true);
        set_rci_scgi_socket_path_for_tests(server.path());
        CHECK(rci_get("show/dns-proxy") == "[]");
    }
    CHECK(test.transport->requests.size() == http_calls);
    CHECK(test.ndmc_calls.size() == ndmc_calls);
    CHECK_FALSE(std::filesystem::exists(dir.file));
}

TEST_CASE("RCI tokens: the error names the missing admin permission") {
    TokenTest test;
    TokenDir dir;
    configure_rci_token_file(dir.file);
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

TEST_CASE("RCI tokens: errors other than 401/403 are reported without creating a token") {
    TokenTest test;
    TokenDir dir;
    configure_rci_token_file(dir.file);
    test.transport->handler = [](const HttpTransportRequest&) { return respond(404); };

    CHECK_THROWS_AS((void)rci_get("show/nothing"), HttpError);
    CHECK(test.ndmc_calls.empty());
}

TEST_CASE("RCI tokens: one-shot commands never create tokens") {
    TokenTest test; // no configure_rci_token_file()
    test.transport->handler = [](const HttpTransportRequest&) { return respond(401); };

    CHECK_THROWS_AS((void)rci_get("show/version"), HttpError);
    CHECK(test.ndmc_calls.empty());
}

namespace {
HttpTransportResponse version_52() {
    return respond(200, R"({"release":"5.02.B.4.0-0","title":"5.2 Beta 4"})");
}
bool is_version(const HttpTransportRequest& request) {
    return request.url.size() >= 17 && request.url.compare(request.url.size() - 17, 17, "/rci/show/version") == 0;
}
} // namespace

TEST_CASE("RCI tokens: KeeneticOS 5.2 switches to a token while tokenless access still works") {
    TokenTest test;
    TokenDir dir;
    configure_rci_token_file(dir.file);
    test.ndmc_handler = [](const std::string& command) {
        if (command.rfind("authentication token generate", 0) == 0) {
            return RciNdmcResult{0, generate_output(kSampleToken)};
        }
        return RciNdmcResult{0, ""};
    };
    test.transport->handler = [](const HttpTransportRequest& request) {
        return is_version(request) ? version_52() : respond(200, "{\"dns\":1}");
    };

    CHECK(rci_get("show/dns-proxy") == "{\"dns\":1}");
    CHECK(dir.read() == std::string(kSampleToken) + "\n");
    REQUIRE(test.transport->requests.size() == 3); // plain, version probe, token check
    CHECK(token_header(test.transport->requests[2]) == kSampleToken);

    test.transport->requests.clear();
    CHECK(rci_get("show/dns-proxy") == "{\"dns\":1}");
    REQUIRE(test.transport->requests.size() == 1);
    CHECK(token_header(test.transport->requests[0]) == kSampleToken);
}

TEST_CASE("RCI tokens: older firmware keeps tokenless access and creates no token") {
    TokenTest test;
    TokenDir dir;
    configure_rci_token_file(dir.file);
    test.transport->handler = [](const HttpTransportRequest&) {
        return respond(200, R"({"release":"4.03.C.6.3-1","title":"4.3.6.3"})");
    };

    CHECK_NOTHROW((void)rci_get("show/version"));
    CHECK(test.ndmc_calls.empty());
    CHECK_FALSE(std::filesystem::exists(dir.file));
}

TEST_CASE("RCI tokens: a token refused right after creation stops further attempts") {
    TokenTest test;
    TokenDir dir;
    configure_rci_token_file(dir.file);
    test.ndmc_handler = [](const std::string& command) {
        if (command.rfind("authentication token generate", 0) == 0) {
            return RciNdmcResult{0, generate_output(kSampleToken)};
        }
        return RciNdmcResult{0, ""};
    };
    test.transport->handler = [](const HttpTransportRequest& request) {
        if (!token_header(request).empty()) return respond(403);
        return is_version(request) ? version_52() : respond(200);
    };

    CHECK(rci_get("show/version") == R"({"release":"5.02.B.4.0-0","title":"5.2 Beta 4"})");
    const auto generated = std::count(test.ndmc_calls.begin(), test.ndmc_calls.end(),
                                      "authentication token generate keen-pbr");
    CHECK(generated == 1);

    // Tokenless access keeps working and no more tokens are made in this run.
    test.transport->requests.clear();
    CHECK(rci_get("show/dns-proxy") == "{}");
    CHECK(test.transport->requests.size() == 1);
    CHECK(std::count(test.ndmc_calls.begin(), test.ndmc_calls.end(),
                     "authentication token generate keen-pbr") == 1);
}

TEST_CASE("RCI tokens: commands that may not create tokens use the saved token only") {
    TokenTest test;
    TokenDir dir;
    {
        std::ofstream(dir.file) << kSampleToken << "\n";
    }
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
    set_rci_scgi_socket_path_for_tests("/nonexistent/ndm.scgi.socket");
    configure_rci_token_file(dir.file, false);
    CHECK_THROWS_AS((void)rci_get("show/version"), HttpError);
    CHECK(test.ndmc_calls.empty());
}

TEST_CASE("RCI tokens: a timeout on the working token neither searches nor creates a token") {
    TokenTest test;
    TokenDir dir;
    {
        std::ofstream(dir.file) << kSampleToken << "\n";
    }
    configure_rci_token_file(dir.file);
    bool fail = false;
    test.transport->handler = [&fail](const HttpTransportRequest&) -> HttpTransportResponse {
        if (fail) throw HttpTransportError("timeout");
        return respond(200, "{}");
    };

    CHECK(rci_get("show/version") == "{}");
    fail = true;
    test.transport->requests.clear();
    CHECK_THROWS_AS((void)rci_get("show/version"), HttpError);
    CHECK(test.transport->requests.size() == 1); // no plain/SCGI search
    CHECK(test.ndmc_calls.empty());

    fail = false;
    CHECK(rci_get("show/version") == "{}");
    CHECK(token_header(test.transport->requests.back()) == kSampleToken);
}

TEST_CASE("RCI tokens: a timeout while proving a fresh token keeps the token") {
    TokenTest test;
    TokenDir dir;
    configure_rci_token_file(dir.file);
    test.ndmc_handler = [](const std::string& command) {
        if (command.rfind("authentication token generate", 0) == 0) {
            return RciNdmcResult{0, generate_output(kSampleToken)};
        }
        return RciNdmcResult{0, ""};
    };
    test.transport->handler = [](const HttpTransportRequest& request) -> HttpTransportResponse {
        if (!token_header(request).empty()) throw HttpTransportError("timeout");
        return is_version(request) ? version_52() : respond(200);
    };

    CHECK_NOTHROW((void)rci_get("show/dns-proxy"));
    CHECK(dir.read() == std::string(kSampleToken) + "\n");

    // The next request tries the token again instead of creating another.
    test.transport->handler = [](const HttpTransportRequest& request) {
        return token_header(request).empty() ? respond(403) : respond(200, "{\"t\":1}");
    };
    CHECK(rci_get("show/dns-proxy") == "{\"t\":1}");
    CHECK(std::count(test.ndmc_calls.begin(), test.ndmc_calls.end(),
                     "authentication token generate keen-pbr") == 1);
}

TEST_CASE("RCI tokens: a failed version probe is retried, not remembered") {
    TokenTest test;
    TokenDir dir;
    configure_rci_token_file(dir.file);
    test.ndmc_handler = [](const std::string& command) {
        if (command.rfind("authentication token generate", 0) == 0) {
            return RciNdmcResult{0, generate_output(kSampleToken)};
        }
        return RciNdmcResult{0, ""};
    };
    bool probe_fails = true;
    // The request itself is "show/dns-proxy"; only the probe (show/version) fails.
    test.transport->handler = [&probe_fails](const HttpTransportRequest& request) -> HttpTransportResponse {
        if (!token_header(request).empty()) return respond(200, "{\"t\":1}");
        if (is_version(request)) {
            if (probe_fails) throw HttpTransportError("timeout");
            return version_52();
        }
        return respond(200, "{}");
    };

    CHECK(rci_get("show/dns-proxy") == "{}");
    CHECK(test.ndmc_calls.empty());
    probe_fails = false;
    CHECK(rci_get("show/dns-proxy") == "{}"); // asks again, now creates the token
    CHECK(dir.read() == std::string(kSampleToken) + "\n");
    CHECK(rci_get("show/dns-proxy") == "{\"t\":1}");
}

TEST_CASE("RCI tokens: a stale or linked temporary file does not decide the token file") {
    TokenTest test;
    TokenDir dir;
    const std::string outside = dir.dir + "/outside";
    std::ofstream(outside) << "keep\n";
    REQUIRE(::symlink(outside.c_str(), (dir.file + ".tmp").c_str()) == 0);
    configure_rci_token_file(dir.file);
    test.ndmc_handler = [](const std::string& command) {
        if (command.rfind("authentication token generate", 0) == 0) {
            return RciNdmcResult{0, generate_output(kSampleToken)};
        }
        return RciNdmcResult{0, ""};
    };
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
