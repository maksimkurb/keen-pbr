#include <doctest/doctest.h>

#include "../src/config/addr_spec.hpp"

using namespace keen_pbr3;

TEST_CASE("parse_addr_spec: empty string") {
    auto s = parse_addr_spec("");
    CHECK(s.addrs.empty());
    CHECK_FALSE(s.negate);
}

TEST_CASE("parse_addr_spec: single CIDR") {
    auto s = parse_addr_spec("192.168.1.0/24");
    REQUIRE(s.addrs.size() == 1);
    CHECK(s.addrs[0] == "192.168.1.0/24");
    CHECK_FALSE(s.negate);
}

TEST_CASE("parse_addr_spec: comma-separated CIDRs") {
    auto s = parse_addr_spec("192.168.1.0/24,10.0.0.0/8");
    REQUIRE(s.addrs.size() == 2);
    CHECK(s.addrs[0] == "192.168.1.0/24");
    CHECK(s.addrs[1] == "10.0.0.0/8");
    CHECK_FALSE(s.negate);
}

TEST_CASE("parse_addr_spec: three CIDRs") {
    auto s = parse_addr_spec("192.168.1.0/24,10.0.0.0/8,172.16.0.0/12");
    REQUIRE(s.addrs.size() == 3);
    CHECK(s.addrs[0] == "192.168.1.0/24");
    CHECK(s.addrs[1] == "10.0.0.0/8");
    CHECK(s.addrs[2] == "172.16.0.0/12");
    CHECK_FALSE(s.negate);
}

TEST_CASE("parse_addr_spec: negated single CIDR") {
    auto s = parse_addr_spec("!192.168.1.0/24");
    REQUIRE(s.addrs.size() == 1);
    CHECK(s.addrs[0] == "192.168.1.0/24");
    CHECK(s.negate);
}

TEST_CASE("parse_addr_spec: negated multi-CIDR") {
    auto s = parse_addr_spec("!10.0.0.0/8,172.16.0.0/12");
    REQUIRE(s.addrs.size() == 2);
    CHECK(s.addrs[0] == "10.0.0.0/8");
    CHECK(s.addrs[1] == "172.16.0.0/12");
    CHECK(s.negate);
}

TEST_CASE("parse_addr_spec: bare '!'") {
    auto s = parse_addr_spec("!");
    CHECK(s.addrs.empty());
    CHECK(s.negate);
}

TEST_CASE("parse_addr_spec: IPv6 CIDR preserved") {
    auto s = parse_addr_spec("2001:db8::/32");
    REQUIRE(s.addrs.size() == 1);
    CHECK(s.addrs[0] == "2001:db8::/32");
    CHECK_FALSE(s.negate);
}

TEST_CASE("parse_addr_spec: negated IPv6 CIDR") {
    auto s = parse_addr_spec("!2001:db8::/32");
    REQUIRE(s.addrs.size() == 1);
    CHECK(s.addrs[0] == "2001:db8::/32");
    CHECK(s.negate);
}

// --- invalid input tests ---

TEST_CASE("parse_addr_spec: invalid IPv4 octet") {
    CHECK_THROWS_AS(parse_addr_spec("999.0.0.0/24"), std::invalid_argument);
}

TEST_CASE("parse_addr_spec: not an IP address") {
    CHECK_THROWS_AS(parse_addr_spec("not-an-ip/24"), std::invalid_argument);
}

TEST_CASE("parse_addr_spec: IPv4 prefix too large") {
    CHECK_THROWS_AS(parse_addr_spec("192.168.1.0/33"), std::invalid_argument);
}

TEST_CASE("parse_addr_spec: negative prefix") {
    CHECK_THROWS_AS(parse_addr_spec("192.168.1.0/-1"), std::invalid_argument);
}

TEST_CASE("parse_addr_spec: non-numeric prefix") {
    CHECK_THROWS_AS(parse_addr_spec("192.168.1.0/abc"), std::invalid_argument);
}

TEST_CASE("parse_addr_spec: IPv6 prefix too large") {
    CHECK_THROWS_AS(parse_addr_spec("2001:db8::/129"), std::invalid_argument);
}

TEST_CASE("parse_addr_spec: invalid CIDR in list") {
    CHECK_THROWS_AS(parse_addr_spec("192.168.1.0/24,999.0.0.0/8"), std::invalid_argument);
}

TEST_CASE("parse_addr_spec: invalid CIDR in negated spec") {
    CHECK_THROWS_AS(parse_addr_spec("!192.168.1.0/33"), std::invalid_argument);
}

TEST_CASE("parse_addr_spec: bare IPv4 host address is valid") {
    CHECK_NOTHROW(parse_addr_spec("10.0.0.1"));
}

TEST_CASE("parse_addr_spec: bare IPv6 host address is valid") {
    CHECK_NOTHROW(parse_addr_spec("2001:db8::1"));
}

TEST_CASE("parse_addr_spec: spaces around commas are trimmed") {
    auto s = parse_addr_spec("192.168.1.0/24, 10.0.0.0/8");
    REQUIRE(s.addrs.size() == 2);
    CHECK(s.addrs[0] == "192.168.1.0/24");
    CHECK(s.addrs[1] == "10.0.0.0/8");
    CHECK_FALSE(s.negate);
}
