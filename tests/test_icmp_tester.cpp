#include <doctest/doctest.h>

#include "../src/health/icmp_tester.hpp"

#include <arpa/inet.h>
#include <netinet/icmp6.h>
#include <netinet/ip_icmp.h>

#include <cstring>

using namespace keen_pbr3;

namespace {

sockaddr_storage ipv4_address(const char* value) {
    sockaddr_storage storage{};
    auto& address = reinterpret_cast<sockaddr_in&>(storage);
    address.sin_family = AF_INET;
    REQUIRE(inet_pton(AF_INET, value, &address.sin_addr) == 1);
    return storage;
}

sockaddr_storage ipv6_address(const char* value) {
    sockaddr_storage storage{};
    auto& address = reinterpret_cast<sockaddr_in6&>(storage);
    address.sin6_family = AF_INET6;
    REQUIRE(inet_pton(AF_INET6, value, &address.sin6_addr) == 1);
    return storage;
}

} // namespace

TEST_CASE("ICMP reply matching requires the exact IPv4 pong") {
    icmphdr reply{};
    reply.type = ICMP_ECHOREPLY;
    reply.un.echo.id = htons(123);
    reply.un.echo.sequence = htons(7);
    const auto expected = ipv4_address("1.1.1.1");

    CHECK(icmp_detail::reply_matches(AF_INET,
        reinterpret_cast<const unsigned char*>(&reply), sizeof(reply),
        expected, expected, 123, 7));
    CHECK_FALSE(icmp_detail::reply_matches(AF_INET,
        reinterpret_cast<const unsigned char*>(&reply), sizeof(reply),
        expected, expected, 123, 8));

    const auto wrong_source = ipv4_address("1.0.0.1");
    CHECK_FALSE(icmp_detail::reply_matches(AF_INET,
        reinterpret_cast<const unsigned char*>(&reply), sizeof(reply),
        wrong_source, expected, 123, 7));

    reply.type = ICMP_DEST_UNREACH;
    CHECK_FALSE(icmp_detail::reply_matches(AF_INET,
        reinterpret_cast<const unsigned char*>(&reply), sizeof(reply),
        expected, expected, 123, 7));
}

TEST_CASE("ICMP reply matching validates IPv6 identifier and packet size") {
    icmp6_hdr reply{};
    reply.icmp6_type = ICMP6_ECHO_REPLY;
    reply.icmp6_id = htons(321);
    reply.icmp6_seq = htons(9);
    const auto expected = ipv6_address("2001:db8::1");

    CHECK(icmp_detail::reply_matches(AF_INET6,
        reinterpret_cast<const unsigned char*>(&reply), sizeof(reply),
        expected, expected, 321, 9));
    CHECK_FALSE(icmp_detail::reply_matches(AF_INET6,
        reinterpret_cast<const unsigned char*>(&reply), sizeof(reply) - 1,
        expected, expected, 321, 9));
    CHECK_FALSE(icmp_detail::reply_matches(AF_INET6,
        reinterpret_cast<const unsigned char*>(&reply), sizeof(reply),
        expected, expected, 320, 9));
}
