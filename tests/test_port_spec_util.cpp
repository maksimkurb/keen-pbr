#include <doctest/doctest.h>

#include "../src/firewall/port_spec_util.hpp"

#include <stdexcept>
#include <string>

using namespace keen_pbr3;

TEST_CASE("parse_port_value validates bounds and digits") {
    int value = 0;
    CHECK(parse_port_value("1", value));
    CHECK(value == 1);
    CHECK(parse_port_value("65535", value));
    CHECK(value == 65535);

    CHECK_FALSE(parse_port_value("", value));
    CHECK_FALSE(parse_port_value("0", value));
    CHECK_FALSE(parse_port_value("65536", value));
    CHECK_FALSE(parse_port_value("12x", value));
}

TEST_CASE("parse_port_range parses single and range tokens") {
    int lo = 0;
    int hi = 0;

    CHECK(parse_port_range("443", lo, hi));
    CHECK(lo == 443);
    CHECK(hi == 443);

    CHECK(parse_port_range("8000-9000", lo, hi));
    CHECK(lo == 8000);
    CHECK(hi == 9000);

    CHECK_FALSE(parse_port_range("9000-8000", lo, hi));
    CHECK_FALSE(parse_port_range("80-90-100", lo, hi));
    CHECK_FALSE(parse_port_range("-80", lo, hi));
    CHECK_FALSE(parse_port_range("80-", lo, hi));
}

TEST_CASE("classify_port_spec detects single, range and list") {
    CHECK(classify_port_spec("443") == PortSpecKind::Single);
    CHECK(classify_port_spec("8000-9000") == PortSpecKind::Range);
    CHECK(classify_port_spec("80,443") == PortSpecKind::List);
    CHECK(classify_port_spec("80,8000-9000") == PortSpecKind::List);
}

TEST_CASE("split_port_spec_tokens validates list token boundaries") {
    auto tokens = split_port_spec_tokens("80,443,8080-8090");
    REQUIRE(tokens.size() == 3);
    CHECK(tokens[0] == "80");
    CHECK(tokens[1] == "443");
    CHECK(tokens[2] == "8080-8090");

    CHECK_THROWS_AS(split_port_spec_tokens(",80"), std::invalid_argument);
    CHECK_THROWS_AS(split_port_spec_tokens("80,"), std::invalid_argument);
    CHECK_THROWS_AS(split_port_spec_tokens("80,,443"), std::invalid_argument);
}

TEST_CASE("normalize_port_spec_for_iptables converts range syntax and validates") {
    CHECK(normalize_port_spec_for_iptables("443") == "443");
    CHECK(normalize_port_spec_for_iptables("8000-9000") == "8000:9000");
    CHECK(normalize_port_spec_for_iptables("80,443,1000-2000") == "80,443,1000:2000");

    CHECK_THROWS_AS(normalize_port_spec_for_iptables("0"), std::invalid_argument);
    CHECK_THROWS_AS(normalize_port_spec_for_iptables("65536"), std::invalid_argument);
    CHECK_THROWS_AS(normalize_port_spec_for_iptables("abc"), std::invalid_argument);
    CHECK_THROWS_AS(normalize_port_spec_for_iptables("9000-1000"), std::invalid_argument);
}
