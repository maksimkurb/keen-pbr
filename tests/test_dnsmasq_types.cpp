#include "../src/daemon/dnsmasq_types.hpp"

#include <doctest/doctest.h>

#include <set>
#include <string>

namespace keen_pbr3 {

TEST_CASE("dnsmasq_types: valid selectors parse and round-trip") {
    for (const char* s : {"uci:cfg01411c", "uci:lan_2", "entware:default",
                          "systemd:dnsmasq.service", "systemd:dnsmasq@lan.service",
                          "test:netns"}) {
        const auto sel = parse_dnsmasq_selector(s);
        REQUIRE(sel.has_value());
        CHECK(sel->str() == s);
    }
}

TEST_CASE("dnsmasq_types: invalid selectors are rejected") {
    for (const std::string s : {std::string(""), std::string("uci:"), std::string("uci:a-b"),
                                std::string("uci:a.b"), std::string("entware:other"),
                                std::string("systemd:dnsmasq"), std::string("ftp:x"),
                                "uci:" + std::string(65, 'a')}) {
        CHECK_FALSE(parse_dnsmasq_selector(s).has_value());
    }
    CHECK(parse_dnsmasq_selector("uci:" + std::string(64, 'a')).has_value());
}

TEST_CASE("dnsmasq_types: endpoints parse") {
    auto e = parse_dnsmasq_endpoint("127.0.0.1:53");
    REQUIRE(e);
    CHECK(e->address == "127.0.0.1");
    CHECK(e->port == 53);

    e = parse_dnsmasq_endpoint("127.0.0.1");
    REQUIRE(e);
    CHECK(e->port == 53);

    e = parse_dnsmasq_endpoint("[::1]:5353");
    REQUIRE(e);
    CHECK(e->address == "::1");
    CHECK(e->port == 5353);
    CHECK(e->str() == "[::1]:5353");

    e = parse_dnsmasq_endpoint("::1");
    REQUIRE(e);
    CHECK(e->port == 53);

    e = parse_dnsmasq_endpoint("fe80::1");
    REQUIRE(e);
    CHECK(e->address == "fe80::1");
    CHECK(e->port == 53);

    CHECK(parse_dnsmasq_endpoint("192.168.1.1:53")->str() == "192.168.1.1:53");
}

TEST_CASE("dnsmasq_types: invalid endpoints are rejected") {
    for (const char* s : {"1.2.3.4:0", "1.2.3.4:65536", "[::1", "host:53", "1.2.3.4:+5", ""}) {
        CHECK_FALSE(parse_dnsmasq_endpoint(s).has_value());
    }
}

TEST_CASE("dnsmasq_types: error code names are unique and prefixed") {
    CHECK(std::string(dnsmasq_error_code_name(DnsmasqErrorCode::None)).empty());
    std::set<std::string> names;
    for (int i = static_cast<int>(DnsmasqErrorCode::NoHook);
         i <= static_cast<int>(DnsmasqErrorCode::ProbeFailed); ++i) {
        const std::string n = dnsmasq_error_code_name(static_cast<DnsmasqErrorCode>(i));
        CHECK(n.rfind("DNSMASQ_", 0) == 0);
        CHECK(names.insert(n).second);
    }
}

} // namespace keen_pbr3
