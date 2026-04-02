#include <doctest/doctest.h>

#include "../src/http/http_client.hpp"

#include <curl/curl.h>

namespace {

constexpr const char* kReadmeUrl =
    "https://raw.githubusercontent.com/maksimkurb/keen-pbr/refs/heads/main/README.md";

struct CurlGlobalGuard {
    CurlGlobalGuard() { curl_global_init(CURL_GLOBAL_DEFAULT); }
    ~CurlGlobalGuard() { curl_global_cleanup(); }
};

bool is_network_unavailable(const keen_pbr3::HttpError& error) {
    const std::string message = error.what();
    return message.find("Couldn't resolve host name") != std::string::npos ||
           message.find("Could not resolve host") != std::string::npos ||
           message.find("Couldn't connect to server") != std::string::npos;
}

} // namespace

TEST_CASE("http client enforces configured max response size for remote file [network]") {
    CurlGlobalGuard curl_guard;

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
