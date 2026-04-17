#include <doctest/doctest.h>

#include "../src/dns/keenetic_dns.hpp"
#include "../src/config/config.hpp"

#include <chrono>
#include <stdexcept>

using namespace keen_pbr3;

namespace {

struct KeeneticDnsTestStateGuard {
    KeeneticDnsTestStateGuard() { reset_keenetic_dns_test_state(); }
    ~KeeneticDnsTestStateGuard() { reset_keenetic_dns_test_state(); }
};

} // namespace

TEST_CASE("keenetic dns: parse address from RCI System policy") {
    SUBCASE("extracts first dns_server from real System payload shape") {
        const std::string json = R"({
          "proxy-status": [
            {
              "proxy-name": "System",
              "proxy-config": "rpc_port = 54321\nrpc_ttl = 10000\nrpc_wait = 10000\ntimeout = 7000\nproceed = 500\nstat_file = /var/ndnproxymain.stat\nstat_time = 10000\nrr_port = 40901\ndns_server = 203.0.113.10 provider.example\ndns_server = 203.0.113.11 provider.example\ndns_server = 127.0.0.1:40508 . # https://dns.example/dns-query@dnsm\ndns_server = 127.0.0.1:40509 . # https://resolver.example/uncensored@dnsm\nstatic_a = my.keenetic.net 78.47.125.180 1\nnorebind_ctl = on\ndns_tcp_port = 53\ndns_udp_port = 53\n"
            },
            {
              "proxy-name": "Policy0",
              "proxy-config": "timeout = 7000\ndns_server = 127.0.0.1:40524 . # https://dns.example/dns-query@dnsm\n"
            }
          ]
        })";

        CHECK(extract_keenetic_dns_address_from_rci(json) == "203.0.113.10");
    }

    SUBCASE("ignores routing domain suffix after dns_server address") {
        const std::string json = R"({
          "proxy-status": [
            {
              "proxy-name": "System",
              "proxy-config": "dns_server = 203.0.113.11 provider.example\n"
            }
          ]
        })";

        CHECK(extract_keenetic_dns_address_from_rci(json) == "203.0.113.11");
    }

    SUBCASE("extracts ipv4 dns_server line with trailing doh comment") {
        const std::string json = R"({
          "proxy-status": [
            {"proxy-name":"Guest","proxy-config":"dns_server = 8.8.8.8\n"},
            {"proxy-name":"System","proxy-config":"dns_server = 127.0.0.1:40500 # https://dns.example/dns-query@dnsm\n"}
          ]
        })";

        CHECK(extract_keenetic_dns_address_from_rci(json) == "127.0.0.1:40500");
    }

    SUBCASE("extracts first dns_server directive from realistic multi-line policy") {
        const std::string json = R"({
          "proxy-status": [
            {
              "proxy-name": "System",
              "proxy-enabled": true,
              "proxy-config": "# system dns policy\ncache-size = 2048\nlisten = 127.0.0.1:53\n  dns_server = 127.0.0.1:40500   # https://dns.adguard-dns.com/dns-query@adguard\nbootstrap_server = 1.1.1.1\n"
            },
            {
              "proxy-name": "Guest",
              "proxy-config": "dns_server = 9.9.9.9\n"
            }
          ]
        })";

        CHECK(extract_keenetic_dns_address_from_rci(json) == "127.0.0.1:40500");
    }

    SUBCASE("extracts bracketed ipv6 address with port") {
        const std::string json = R"({
          "proxy-status": [
            {
              "proxy-name": "System",
              "proxy-config": "dns_server = [2001:db8::53]:853 # tls://resolver.example\n"
            }
          ]
        })";

        CHECK(extract_keenetic_dns_address_from_rci(json) == "[2001:db8::53]:853");
    }

    SUBCASE("extracts bare ipv6 address followed by routing domain") {
        const std::string json = R"({
          "proxy-status": [
            {
              "proxy-name": "System",
              "proxy-config": "dns_server = 2001:db8::8888 provider.example\ndns_server = 127.0.0.1:40508 . # https://dns.example/dns-query@dnsm\n"
            }
          ]
        })";

        CHECK(extract_keenetic_dns_address_from_rci(json) == "2001:db8::8888");
    }

    SUBCASE("extracts plain dns_server line with dot routing suffix") {
        const std::string json = R"({
          "proxy-status": [
            {
              "proxy-name": "System",
              "proxy-config": "dns_server = 198.51.100.10 .\ndns_server = 198.51.100.11 .\n"
            }
          ]
        })";

        CHECK(extract_keenetic_dns_address_from_rci(json) == "198.51.100.10");
    }

    SUBCASE("extracts localhost dot server with dot comment") {
        const std::string json = R"({
          "proxy-status": [
            {
              "proxy-name": "System",
              "proxy-config": "dns_server = 127.0.0.1:40500 . # tls.resolver.example\ndns_server = 127.0.0.1:40508 . # https://resolver.example/uncensored@dnsm\n"
            }
          ]
        })";

        CHECK(extract_keenetic_dns_address_from_rci(json) == "127.0.0.1:40500");
    }
}

TEST_CASE("keenetic dns: invalid RCI response is rejected") {
    SUBCASE("empty proxy-status array") {
        CHECK_THROWS_AS(extract_keenetic_dns_address_from_rci(R"({"proxy-status":[]})"),
                        KeeneticDnsError);
    }

    SUBCASE("missing proxy-status array") {
        CHECK_THROWS_AS(extract_keenetic_dns_address_from_rci(R"({"status":[]})"),
                        KeeneticDnsError);
    }

    SUBCASE("System policy without proxy-config string") {
        CHECK_THROWS_AS(extract_keenetic_dns_address_from_rci(
                            R"({"proxy-status":[{"proxy-name":"System","proxy-config":{}}]})"),
                        KeeneticDnsError);
    }

    SUBCASE("System policy without dns_server directive") {
        CHECK_THROWS_AS(extract_keenetic_dns_address_from_rci(
                            R"({"proxy-status":[{"proxy-name":"System","proxy-config":"listen = 127.0.0.1:53\ncache-size = 2048\n"}]})"),
                        KeeneticDnsError);
    }

    SUBCASE("System policy with invalid dns_server address") {
        CHECK_THROWS_AS(extract_keenetic_dns_address_from_rci(
                            R"({"proxy-status":[{"proxy-name":"System","proxy-config":"dns_server = not-an-ip"}]})"),
                        KeeneticDnsError);
    }
}

TEST_CASE("keenetic dns: cache refresh semantics") {
    KeeneticDnsTestStateGuard guard;

    auto now = std::chrono::steady_clock::time_point{};
    set_keenetic_dns_now_fn_for_tests([&now]() {
        return now;
    });

    int fetch_count = 0;
    set_keenetic_dns_fetcher_for_tests([&fetch_count]() {
        ++fetch_count;
        return std::string(R"({
          "proxy-status": [
            {
              "proxy-name": "System",
              "proxy-config": "dns_server = 203.0.113.10 provider.example\n"
            }
          ]
        })");
    });

    SUBCASE("uses cached value while ttl is fresh") {
        CHECK(resolve_keenetic_dns_address() == "203.0.113.10");
        CHECK(fetch_count == 1);

        now += std::chrono::minutes(4);
        CHECK(resolve_keenetic_dns_address() == "203.0.113.10");
        CHECK(fetch_count == 1);
    }

    SUBCASE("falls back to cached value when stale refresh fails") {
        CHECK(resolve_keenetic_dns_address() == "203.0.113.10");
        CHECK(fetch_count == 1);

        now += std::chrono::minutes(6);
        set_keenetic_dns_fetcher_for_tests([&fetch_count]() -> std::string {
            ++fetch_count;
            throw KeeneticDnsError("simulated RCI outage");
        });

        const KeeneticDnsRefreshResult result = refresh_keenetic_dns_address_cache(false);
        CHECK(result.status == KeeneticDnsRefreshStatus::FETCH_FAILED_USED_CACHE);
        REQUIRE(result.address.has_value());
        CHECK(*result.address == "203.0.113.10");
        CHECK(fetch_count == 2);
        CHECK(resolve_keenetic_dns_address() == "203.0.113.10");
    }

    SUBCASE("reports updated only when forced refresh changes address") {
        CHECK(resolve_keenetic_dns_address() == "203.0.113.10");
        CHECK(fetch_count == 1);

        set_keenetic_dns_fetcher_for_tests([&fetch_count]() {
            ++fetch_count;
            return std::string(R"({
              "proxy-status": [
                {
                  "proxy-name": "System",
                  "proxy-config": "dns_server = 203.0.113.11 provider.example\n"
                }
              ]
            })");
        });

        const KeeneticDnsRefreshResult result = refresh_keenetic_dns_address_cache(true);
        CHECK(result.status == KeeneticDnsRefreshStatus::UPDATED);
        REQUIRE(result.address.has_value());
        CHECK(*result.address == "203.0.113.11");
        CHECK(resolve_keenetic_dns_address() == "203.0.113.11");
    }

    SUBCASE("fails without cache when initial fetch fails") {
        set_keenetic_dns_fetcher_for_tests([]() -> std::string {
            throw KeeneticDnsError("simulated initial failure");
        });

        const KeeneticDnsRefreshResult result = refresh_keenetic_dns_address_cache(true);
        CHECK(result.status == KeeneticDnsRefreshStatus::FETCH_FAILED_NO_CACHE);
        CHECK_FALSE(result.address.has_value());
        CHECK_THROWS_AS(resolve_keenetic_dns_address(true), KeeneticDnsError);
    }
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
