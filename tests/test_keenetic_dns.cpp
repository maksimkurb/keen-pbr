#include <doctest/doctest.h>

#include "../src/dns/keenetic_dns.hpp"
#include "../src/config/config.hpp"

using namespace keen_pbr3;

TEST_CASE("keenetic dns: parse address from RCI System policy") {
    const std::string json = R"({
      "proxy-status": [
        {"proxy-name":"Guest","proxy-config":"dns_server = 8.8.8.8\n"},
        {"proxy-name":"System","proxy-config":"dns_server = 127.0.0.1:40500 # https://dns.example/dns-query@dnsm\n"}
      ]
    })";

    CHECK(extract_keenetic_dns_address_from_rci(json) == "127.0.0.1:40500");
}

TEST_CASE("keenetic dns: invalid RCI response is rejected") {
    CHECK_THROWS_AS(extract_keenetic_dns_address_from_rci(R"({"proxy-status":[]})"),
                    KeeneticDnsError);
    CHECK_THROWS_AS(extract_keenetic_dns_address_from_rci(R"({"proxy-status":[{"proxy-name":"System","proxy-config":"dns_server = not-an-ip"}]})"),
                    KeeneticDnsError);
}

#ifndef USE_KEENETIC_API
TEST_CASE("config: type=keenetic rejected when feature disabled") {
    const std::string json = R"({
      "dns": {
        "servers": [
          {"tag": "keenetic-dns", "type": "keenetic"}
        ]
      }
    })";
    CHECK_THROWS_AS(parse_and_validate_config(json), ConfigValidationError);
}
#endif

