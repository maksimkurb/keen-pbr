#include <doctest/doctest.h>

#include "../src/http/http_client.hpp"
#include "../src/http/curl_runtime.hpp"
#include "../src/health/url_tester.hpp"

namespace {

class FakeTransport final : public keen_pbr3::HttpTransport {
public:
    keen_pbr3::HttpTransportRequest request;
    keen_pbr3::HttpTransportResponse response;
    bool fail{false};
    int calls{0};
    keen_pbr3::HttpTransportResponse perform(const keen_pbr3::HttpTransportRequest& value) override {
        request = value;
        ++calls;
        if (fail) throw keen_pbr3::HttpTransportError("transport unavailable");
        return response;
    }
};

constexpr const char* kReadmeUrl =
    "https://raw.githubusercontent.com/maksimkurb/keen-pbr/refs/heads/main/README.md";

bool is_network_unavailable(const keen_pbr3::HttpError& error) {
    const std::string message = error.what();
    return message.find("Couldn't resolve host name") != std::string::npos ||
           message.find("Could not resolve host") != std::string::npos ||
           message.find("Couldn't connect to server") != std::string::npos ||
           message.find("Timeout was reached") != std::string::npos;
}

} // namespace

TEST_CASE("http client rejects non-HTTP initial protocols") {
    keen_pbr3::CurlRuntime curl_runtime;
    keen_pbr3::HttpClient client;
    CHECK_THROWS_AS(client.download("file:///etc/hosts"), keen_pbr3::HttpError);
    CHECK_THROWS_AS(client.download_conditional("file:///etc/hosts"), keen_pbr3::HttpError);
}

TEST_CASE("http client enforces configured max response size for remote file [network]") {
    keen_pbr3::CurlRuntime curl_runtime;

    SUBCASE("download fails when limit is 30 bytes") {
        keen_pbr3::HttpClient client;
        client.set_timeout(std::chrono::seconds(15));
        client.set_max_response_size(30);

        try {
            (void)client.download(kReadmeUrl);
            FAIL("Expected HttpError");
        } catch (const keen_pbr3::HttpError& error) {
            if (is_network_unavailable(error)) {
                INFO("Skipping network-dependent assertion");
                INFO(error.what());
                return;
            }
        }
    }

    SUBCASE("download succeeds when limit is 10 MiB") {
        keen_pbr3::HttpClient client;
        client.set_timeout(std::chrono::seconds(15));
        client.set_max_response_size(10 * 1024 * 1024);

        std::string body;
        try {
            body = client.download(kReadmeUrl);
        } catch (const keen_pbr3::HttpError& error) {
            if (is_network_unavailable(error)) {
                INFO("Skipping network-dependent assertion");
                INFO(error.what());
                return;
            }
            throw;
        }

        CHECK_FALSE(body.empty());
        CHECK(body.size() > 30);
    }
}

TEST_CASE("conditional download captures validators only from final redirect response") {
    std::string etag;
    std::string last_modified;
    keen_pbr3::detail::capture_response_header_line("HTTP/1.1 302 Found\r\n", etag, last_modified);
    keen_pbr3::detail::capture_response_header_line("ETag: \"redirect-poison\"\r\n", etag, last_modified);
    keen_pbr3::detail::capture_response_header_line("Last-Modified: Mon, 01 Jan 2024 00:00:00 GMT\r\n", etag, last_modified);
    keen_pbr3::detail::capture_response_header_line("HTTP/1.1 200 OK\r\n", etag, last_modified);
    keen_pbr3::detail::capture_response_header_line("ETag: \"final\"\r\n", etag, last_modified);

    CHECK(etag == "\"final\"");
    CHECK(last_modified.empty());
}

TEST_CASE("http client builds conditional transport request and maps errors") {
    auto transport = std::make_shared<FakeTransport>();
    transport->response = {304, {}, {{"etag", "new"}, {"last-modified", "now"}}, std::chrono::milliseconds(7)};
    keen_pbr3::HttpClient client(transport);
    client.set_timeout(std::chrono::seconds(3));
    client.set_user_agent("test-agent");
    const auto result = client.download_conditional("https://example.test/a", "old", "yesterday", {42});
    CHECK(result.not_modified);
    CHECK(result.etag == "new");
    CHECK(transport->request.timeout_ms == 3000);
    CHECK(transport->request.fwmark == 42);
    CHECK(transport->request.headers.size() == 2);
    transport->fail = true;
    CHECK_THROWS_AS(client.download("https://example.test/a"), keen_pbr3::HttpError);
}

TEST_CASE("url tester uses discard transport probes and retry policy") {
    auto transport = std::make_shared<FakeTransport>();
    transport->response = {204, {}, {}, std::chrono::milliseconds(12)};
    keen_pbr3::URLTester tester(transport);
    keen_pbr3::RetryConfig retry;
    retry.attempts = 1;
    const auto result = tester.test("https://example.test/health", 77, 456, retry);
    CHECK(result.success);
    CHECK(result.latency_ms == 12);
    CHECK(transport->request.discard_body);
    CHECK(transport->request.timeout_ms == 456);
    CHECK(transport->request.fwmark == 77);
    CHECK(transport->request.max_redirects == 3);
}
