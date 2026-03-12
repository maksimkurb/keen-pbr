#include <doctest/doctest.h>

#include "../src/dns/dns_probe_server.hpp"

#include <array>

using namespace keen_pbr3;

namespace {

std::vector<uint8_t> make_query(uint16_t id, uint16_t flags,
                                const std::string& label, uint16_t qtype) {
    return {
        static_cast<uint8_t>((id >> 8) & 0xFF), static_cast<uint8_t>(id & 0xFF),
        static_cast<uint8_t>((flags >> 8) & 0xFF), static_cast<uint8_t>(flags & 0xFF),
        0x00, 0x01, // qdcount
        0x00, 0x00, // ancount
        0x00, 0x00, // nscount
        0x00, 0x00, // arcount
        static_cast<uint8_t>(label.size()),
        static_cast<uint8_t>(label[0]),
        static_cast<uint8_t>(label[1]),
        static_cast<uint8_t>(label[2]),
        0x03, 'c', 'o', 'm',
        0x00,
        static_cast<uint8_t>((qtype >> 8) & 0xFF), static_cast<uint8_t>(qtype & 0xFF),
        0x00, 0x01,
    };
}

std::vector<uint8_t> make_query_with_ecs(uint16_t id, const std::string& label,
                                         const std::array<uint8_t, 3>& ecs_addr_prefix) {
    auto packet = make_query(id, 0x0100, label, 1);
    packet[11] = 0x01; // arcount = 1

    packet.push_back(0x00); // root name
    packet.push_back(0x00);
    packet.push_back(0x29); // OPT
    packet.push_back(0x10);
    packet.push_back(0x00); // UDP payload size
    packet.push_back(0x00);
    packet.push_back(0x00);
    packet.push_back(0x00);
    packet.push_back(0x00); // ttl / flags
    packet.push_back(0x00);
    packet.push_back(0x0B); // rdlen = 11

    packet.push_back(0x00);
    packet.push_back(0x08); // ECS option
    packet.push_back(0x00);
    packet.push_back(0x07); // option len = 7
    packet.push_back(0x00);
    packet.push_back(0x01); // family = IPv4
    packet.push_back(0x18); // source prefix = 24
    packet.push_back(0x00); // scope prefix = 0
    packet.insert(packet.end(), ecs_addr_prefix.begin(), ecs_addr_prefix.end());
    return packet;
}

} // namespace

TEST_CASE("dns probe listen address parses ipv4 host and port") {
    auto parsed = parse_dns_probe_listen_address("127.0.0.88:5353");
    CHECK(parsed.ip == "127.0.0.88");
    CHECK(parsed.port == 5353);
}

TEST_CASE("dns probe listen address rejects ipv6") {
    CHECK_THROWS_AS(parse_dns_probe_listen_address("[::1]:53"), DnsError);
}

TEST_CASE("dns probe settings derive answer IPv4 from listen") {
    auto parsed = parse_dns_probe_server_settings("127.0.0.88:53", nullptr);
    CHECK(parsed.bind_ip == "127.0.0.88");
    CHECK(parsed.answer_ipv4 == "127.0.0.88");
}

TEST_CASE("dns probe query parser extracts qname") {
    auto packet = make_query(0x1234, 0x0100, "www", 1);
    auto query = parse_dns_probe_query(packet);
    CHECK(query.id == 0x1234);
    CHECK(query.flags == 0x0100);
    CHECK(query.name == "www.com");
    CHECK(!query.ecs.has_value());
}

TEST_CASE("dns probe query parser extracts ECS") {
    auto packet = make_query_with_ecs(0x1234, "www", {192, 0, 2});
    auto query = parse_dns_probe_query(packet);
    REQUIRE(query.ecs.has_value());
    CHECK(*query.ecs == "192.0.2.0/24");
}

TEST_CASE("dns probe response returns synthetic A record") {
    auto packet = make_query(0x1234, 0x0100, "www", 28);
    auto query = parse_dns_probe_query(packet);
    auto response = build_dns_probe_response(query, "127.0.0.88");

    REQUIRE(response.size() >= 33);
    CHECK(response[0] == 0x12);
    CHECK(response[1] == 0x34);
    CHECK(response[2] == 0x81);
    CHECK(response[3] == 0x00);
    CHECK(response[6] == 0x00);
    CHECK(response[7] == 0x01);
    CHECK(response[10] == 0x00);
    CHECK(response[11] == 0x00);

    CHECK(response[response.size() - 4] == 127);
    CHECK(response[response.size() - 3] == 0);
    CHECK(response[response.size() - 2] == 0);
    CHECK(response[response.size() - 1] == 88);
}

TEST_CASE("dns probe query rejects malformed packets") {
    std::vector<uint8_t> packet = {0x12, 0x34, 0x01, 0x00};
    CHECK_THROWS_AS(parse_dns_probe_query(packet), DnsError);
}
