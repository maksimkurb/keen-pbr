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

} // namespace

TEST_CASE("http client enforces configured max response size for remote file [network]") {
    CurlGlobalGuard curl_guard;

    SUBCASE("download fails when limit is 30 bytes") {
        keen_pbr3::HttpClient client;
        client.set_timeout(std::chrono::seconds(15));
        client.set_max_response_size(30);

        CHECK_THROWS_AS(client.download(kReadmeUrl), keen_pbr3::HttpError);
    }

    SUBCASE("download succeeds when limit is 10 MiB") {
        keen_pbr3::HttpClient client;
        client.set_timeout(std::chrono::seconds(15));
        client.set_max_response_size(10 * 1024 * 1024);

        const std::string body = client.download(kReadmeUrl);

        CHECK_FALSE(body.empty());
        CHECK(body.size() > 30);
    }
}
