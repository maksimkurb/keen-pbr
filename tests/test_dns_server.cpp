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
