#include <doctest/doctest.h>

#include "../src/dns/dns_txt_client.hpp"

using namespace keen_pbr3;

TEST_CASE("parse_resolver_config_hash_txt parses ts/hash payload") {
    const auto parsed = parse_resolver_config_hash_txt("1744060800|0123456789abcdef0123456789abcdef");
    REQUIRE(parsed.ts.has_value());
    CHECK(*parsed.ts == 1744060800);
    CHECK(parsed.hash == "0123456789abcdef0123456789abcdef");
}

TEST_CASE("parse_resolver_config_hash_txt parses quoted ts/hash payload") {
    const auto parsed = parse_resolver_config_hash_txt("\"1744060800|0123456789abcdef0123456789abcdef\"");
    REQUIRE(parsed.ts.has_value());
    CHECK(*parsed.ts == 1744060800);
    CHECK(parsed.hash == "0123456789abcdef0123456789abcdef");
}

TEST_CASE("parse_resolver_config_hash_txt parses md5-prefixed hash payload") {
    const auto parsed = parse_resolver_config_hash_txt("md5=0123456789ABCDEF0123456789ABCDEF");
    CHECK_FALSE(parsed.ts.has_value());
    CHECK(parsed.hash == "0123456789abcdef0123456789abcdef");
}
