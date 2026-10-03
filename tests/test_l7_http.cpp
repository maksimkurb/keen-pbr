#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "../src/l7/http_host.hpp"

using namespace keen_pbr3;
using namespace keen_pbr3::l7;

TEST_CASE("l7: HTTP Host simple GET with Host header") {
    std::string payload = "GET / HTTP/1.1\r\nHost: Example.COM:8080\r\n\r\n";
    ByteView view(reinterpret_cast<const uint8_t*>(payload.data()), payload.size());

    std::string host_out;
    auto status = http_host(view, host_out);
    CHECK(status == ParseStatus::Found);
    CHECK(host_out == "example.com");
}

TEST_CASE("l7: HTTP Host header name case-insensitive") {
    std::string payload = "GET / HTTP/1.1\r\nhOsT: example.com\r\n\r\n";
    ByteView view(reinterpret_cast<const uint8_t*>(payload.data()), payload.size());

    std::string host_out;
    auto status = http_host(view, host_out);
    CHECK(status == ParseStatus::Found);
    CHECK(host_out == "example.com");
}

TEST_CASE("l7: HTTP Host with tabs around value") {
    std::string payload = "GET / HTTP/1.1\r\nHost: \t\t example.com \t\t\r\n\r\n";
    ByteView view(reinterpret_cast<const uint8_t*>(payload.data()), payload.size());

    std::string host_out;
    auto status = http_host(view, host_out);
    CHECK(status == ParseStatus::Found);
    CHECK(host_out == "example.com");
}

TEST_CASE("l7: HTTP POST request") {
    std::string payload = "POST /api HTTP/1.1\r\nHost: api.example.com\r\n\r\n";
    ByteView view(reinterpret_cast<const uint8_t*>(payload.data()), payload.size());

    std::string host_out;
    auto status = http_host(view, host_out);
    CHECK(status == ParseStatus::Found);
    CHECK(host_out == "api.example.com");
}

TEST_CASE("l7: HTTP HEAD request") {
    std::string payload = "HEAD / HTTP/1.1\r\nHost: example.com\r\n\r\n";
    ByteView view(reinterpret_cast<const uint8_t*>(payload.data()), payload.size());

    std::string host_out;
    auto status = http_host(view, host_out);
    CHECK(status == ParseStatus::Found);
    CHECK(host_out == "example.com");
}

TEST_CASE("l7: HTTP Host missing") {
    std::string payload = "GET / HTTP/1.1\r\n\r\n";
    ByteView view(reinterpret_cast<const uint8_t*>(payload.data()), payload.size());

    std::string host_out;
    auto status = http_host(view, host_out);
    CHECK(status == ParseStatus::NotMatched);
}

TEST_CASE("l7: HTTP headers incomplete without Host") {
    std::string payload = "GET / HTTP/1.1\r\nContent-Length: 100\r\n";
    ByteView view(reinterpret_cast<const uint8_t*>(payload.data()), payload.size());

    std::string host_out;
    auto status = http_host(view, host_out);
    CHECK(status == ParseStatus::NeedMore);
}

TEST_CASE("l7: HTTP SSH protocol") {
    std::string payload = "SSH-2.0-OpenSSH_7.4\r\n";
    ByteView view(reinterpret_cast<const uint8_t*>(payload.data()), payload.size());

    std::string host_out;
    auto status = http_host(view, host_out);
    CHECK(status == ParseStatus::NotMatched);
}

TEST_CASE("l7: HTTP Host IPv4 literal") {
    std::string payload = "GET / HTTP/1.1\r\nHost: 1.2.3.4\r\n\r\n";
    ByteView view(reinterpret_cast<const uint8_t*>(payload.data()), payload.size());

    std::string host_out;
    auto status = http_host(view, host_out);
    CHECK(status == ParseStatus::NotMatched);
}

TEST_CASE("l7: HTTP Host IPv6 literal") {
    std::string payload = "GET / HTTP/1.1\r\nHost: [::1]:80\r\n\r\n";
    ByteView view(reinterpret_cast<const uint8_t*>(payload.data()), payload.size());

    std::string host_out;
    auto status = http_host(view, host_out);
    CHECK(status == ParseStatus::NotMatched);
}

TEST_CASE("l7: HTTP Host IPv6 no port") {
    std::string payload = "GET / HTTP/1.1\r\nHost: [::1]\r\n\r\n";
    ByteView view(reinterpret_cast<const uint8_t*>(payload.data()), payload.size());

    std::string host_out;
    auto status = http_host(view, host_out);
    CHECK(status == ParseStatus::NotMatched);
}

TEST_CASE("l7: HTTP Host domain with trailing dot") {
    std::string payload = "GET / HTTP/1.1\r\nHost: example.com.\r\n\r\n";
    ByteView view(reinterpret_cast<const uint8_t*>(payload.data()), payload.size());

    std::string host_out;
    auto status = http_host(view, host_out);
    CHECK(status == ParseStatus::Found);
    CHECK(host_out == "example.com");
}

TEST_CASE("l7: HTTP Host with port") {
    std::string payload = "GET / HTTP/1.1\r\nHost: example.com:443\r\n\r\n";
    ByteView view(reinterpret_cast<const uint8_t*>(payload.data()), payload.size());

    std::string host_out;
    auto status = http_host(view, host_out);
    CHECK(status == ParseStatus::Found);
    CHECK(host_out == "example.com");
}

TEST_CASE("l7: HTTP multiple headers before Host") {
    std::string payload = "GET / HTTP/1.1\r\nUser-Agent: curl\r\nAccept: */*\r\nHost: example.com\r\n\r\n";
    ByteView view(reinterpret_cast<const uint8_t*>(payload.data()), payload.size());

    std::string host_out;
    auto status = http_host(view, host_out);
    CHECK(status == ParseStatus::Found);
    CHECK(host_out == "example.com");
}

TEST_CASE("l7: HTTP DELETE request") {
    std::string payload = "DELETE /resource HTTP/1.1\r\nHost: api.example.com\r\n\r\n";
    ByteView view(reinterpret_cast<const uint8_t*>(payload.data()), payload.size());

    std::string host_out;
    auto status = http_host(view, host_out);
    CHECK(status == ParseStatus::Found);
    CHECK(host_out == "api.example.com");
}

TEST_CASE("l7: HTTP OPTIONS request") {
    std::string payload = "OPTIONS / HTTP/1.1\r\nHost: example.com\r\n\r\n";
    ByteView view(reinterpret_cast<const uint8_t*>(payload.data()), payload.size());

    std::string host_out;
    auto status = http_host(view, host_out);
    CHECK(status == ParseStatus::Found);
    CHECK(host_out == "example.com");
}

TEST_CASE("l7: HTTP PATCH request") {
    std::string payload = "PATCH /api HTTP/1.1\r\nHost: example.com\r\n\r\n";
    ByteView view(reinterpret_cast<const uint8_t*>(payload.data()), payload.size());

    std::string host_out;
    auto status = http_host(view, host_out);
    CHECK(status == ParseStatus::Found);
    CHECK(host_out == "example.com");
}

TEST_CASE("l7: HTTP CONNECT request") {
    std::string payload = "CONNECT example.com:443 HTTP/1.1\r\nHost: example.com\r\n\r\n";
    ByteView view(reinterpret_cast<const uint8_t*>(payload.data()), payload.size());

    std::string host_out;
    auto status = http_host(view, host_out);
    CHECK(status == ParseStatus::Found);
    CHECK(host_out == "example.com");
}

TEST_CASE("l7: HTTP search limit 4096 bytes") {
    std::string payload = "GET / HTTP/1.1\r\n";
    payload.append(4050, 'X');  // Add 4050 bytes of padding
    payload += "Host: example.com\r\n\r\n";

    ByteView view(reinterpret_cast<const uint8_t*>(payload.data()), payload.size());

    std::string host_out;
    auto status = http_host(view, host_out);
    CHECK(status == ParseStatus::NotMatched);
}

TEST_CASE("l7: HTTP lowercase conversion") {
    std::string payload = "GET / HTTP/1.1\r\nHost: ExAmPlE.CoM\r\n\r\n";
    ByteView view(reinterpret_cast<const uint8_t*>(payload.data()), payload.size());

    std::string host_out;
    auto status = http_host(view, host_out);
    CHECK(status == ParseStatus::Found);
    CHECK(host_out == "example.com");
}

TEST_CASE("l7: HTTP headers incomplete in buffer") {
    std::string payload = "GET / HTTP/1.1\r\nHost: example.com\r\n";
    ByteView view(reinterpret_cast<const uint8_t*>(payload.data()), payload.size());

    std::string host_out;
    auto status = http_host(view, host_out);
    CHECK(status == ParseStatus::Found);  // Host fully present before headers end
    CHECK(host_out == "example.com");
}

TEST_CASE("l7: HTTP invalid method") {
    std::string payload = "INVALID / HTTP/1.1\r\nHost: example.com\r\n\r\n";
    ByteView view(reinterpret_cast<const uint8_t*>(payload.data()), payload.size());

    std::string host_out;
    auto status = http_host(view, host_out);
    CHECK(status == ParseStatus::NotMatched);
}

TEST_CASE("l7: HTTP Host with spaces in header line") {
    std::string payload = "GET / HTTP/1.1\r\nHost   :   example.com   \r\n\r\n";
    ByteView view(reinterpret_cast<const uint8_t*>(payload.data()), payload.size());

    std::string host_out;
    auto status = http_host(view, host_out);
    CHECK((status == ParseStatus::NotMatched || status == ParseStatus::Found));
}

TEST_CASE("l7: HTTP Host is never taken from the body") {
    const std::string req = "POST / HTTP/1.1\r\nX-A: b\r\n\r\nHost: evil.com\r\n";
    std::string host;
    CHECK(http_host(ByteView(reinterpret_cast<const uint8_t*>(req.data()), req.size()), host) ==
          ParseStatus::NotMatched);
}
