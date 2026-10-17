#include <doctest/doctest.h>

#include "../src/dns/dns_server.hpp"

using namespace keen_pbr3;

// ---------------------------------------------------------------------------
// IPv4 bare
// ---------------------------------------------------------------------------

TEST_CASE("parse: bare IPv4 -> port 53") {
    auto r = parse_dns_address_str("8.8.8.8");
    CHECK(r.ip   == "8.8.8.8");
    CHECK(r.port == 53);
}

TEST_CASE("parse: IPv4:port -> parsed port") {
    auto r = parse_dns_address_str("8.8.8.8:5353");
    CHECK(r.ip   == "8.8.8.8");
    CHECK(r.port == 5353);
}

TEST_CASE("parse: IPv4 port 1 -> valid") {
    auto r = parse_dns_address_str("1.2.3.4:1");
    CHECK(r.ip   == "1.2.3.4");
    CHECK(r.port == 1);
}

TEST_CASE("parse: IPv4 port 65535 -> valid") {
    auto r = parse_dns_address_str("1.2.3.4:65535");
    CHECK(r.ip   == "1.2.3.4");
    CHECK(r.port == 65535);
}

TEST_CASE("parse: IPv4 port 0 -> DnsError") {
    CHECK_THROWS_AS(parse_dns_address_str("1.2.3.4:0"), DnsError);
}

TEST_CASE("parse: IPv4 port 65536 -> DnsError") {
    CHECK_THROWS_AS(parse_dns_address_str("1.2.3.4:65536"), DnsError);
}

// ---------------------------------------------------------------------------
// IPv6 bare
// ---------------------------------------------------------------------------

TEST_CASE("parse: bare IPv6 -> port 53") {
    auto r = parse_dns_address_str("::1");
    CHECK(r.ip   == "::1");
    CHECK(r.port == 53);
}

TEST_CASE("parse: [IPv6]:port -> parsed port") {
    auto r = parse_dns_address_str("[::1]:5353");
    CHECK(r.ip   == "::1");
    CHECK(r.port == 5353);
}

TEST_CASE("parse: bracketed IPv6 without port -> port 53") {
    auto r = parse_dns_address_str("[::1]");
    CHECK(r.ip   == "::1");
    CHECK(r.port == 53);
}

// ---------------------------------------------------------------------------
// Invalid inputs
// ---------------------------------------------------------------------------

TEST_CASE("parse: empty string -> DnsError") {
    CHECK_THROWS_AS(parse_dns_address_str(""), DnsError);
}

TEST_CASE("parse: invalid IP -> DnsError") {
    CHECK_THROWS_AS(parse_dns_address_str("not-an-ip"), DnsError);
}

TEST_CASE("parse: valid IP + non-numeric port -> DnsError") {
    CHECK_THROWS_AS(parse_dns_address_str("8.8.8.8:abc"), DnsError);
}

TEST_CASE("parse: [IPv6] without closing bracket -> DnsError") {
    CHECK_THROWS_AS(parse_dns_address_str("[::1"), DnsError);
}

TEST_CASE("parse_doh_url: splits host, port and path") {
    const auto plain = parse_doh_url("https://dns.google/dns-query");
    CHECK(plain.secure);
    CHECK(plain.host == "dns.google");
    CHECK(plain.port == 443);
    CHECK(plain.path == "/dns-query");
    CHECK(plain.ip.empty());

    const auto with_port = parse_doh_url("https://dns.example:8443/q?x=1");
    CHECK(with_port.port == 8443);
    CHECK(with_port.path == "/q?x=1");

    const auto v4 = parse_doh_url("https://1.1.1.1/dns-query");
    CHECK(v4.ip == "1.1.1.1");
    CHECK(v4.host == "1.1.1.1");

    const auto v6 = parse_doh_url("https://[2606:4700:4700::1111]:444/dns-query");
    CHECK(v6.ip == "2606:4700:4700::1111");
    CHECK(v6.port == 444);

    CHECK(parse_doh_url("https://dns.example").path == "/");
    CHECK(parse_doh_url("https://dns.example?x=1").path == "/?x=1");
}

TEST_CASE("parse_doh_url: only https unless plain http is allowed") {
    CHECK_THROWS_AS(parse_doh_url("http://dns.example/dns-query"), DnsError);
    const auto http = parse_doh_url("http://127.0.0.1:8080/dns-query", true);
    CHECK_FALSE(http.secure);
    CHECK(http.port == 8080);
    CHECK(parse_doh_url("http://localhost/dns-query", true).port == 80);
}

TEST_CASE("parse_doh_url: malformed URLs are rejected") {
    for (const char* bad : {"", "dns.example", "https://", "https:///x", "https://u@dns.example/x",
                            "https://dns.example:/x", "https://dns.example:0/x",
                            "https://dns.example:70000/x", "https://dns.example:abc/x",
                            "https://dns exam.ple/x", "https://dns.example/x#frag",
                            "https://[::1/x", "https://[nope]/x", "https://::1/x",
                            "https://dns.ex!ample/x"}) {
        CAPTURE(bad);
        CHECK_THROWS_AS(parse_doh_url(bad), DnsError);
    }
}

TEST_CASE("parse_doh_server: keeps the URL and the detour, has no IP address") {
    const auto server = parse_doh_server("g", "https://dns.google/dns-query", std::string("vpn"));
    CHECK(server.is_doh());
    CHECK(server.doh_url == "https://dns.google/dns-query");
    CHECK(server.detour == std::optional<std::string>("vpn"));
    CHECK(server.resolved_ip.empty());
    CHECK(server.port == 443);
    CHECK_FALSE(parse_dns_server("p", "8.8.8.8", std::nullopt).is_doh());
    CHECK_THROWS_AS(parse_doh_server("g", "http://x/y", std::nullopt), DnsError);
}
