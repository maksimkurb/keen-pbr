#include <doctest/doctest.h>

#include "../src/dns/dns_wire.hpp"

#include <netinet/in.h>

#include <algorithm>
#include <array>
#include <string>
#include <vector>

using namespace keen_pbr3;
using namespace keen_pbr3::dns_wire;

namespace {

using Bytes = std::vector<uint8_t>;

void put16(Bytes& b, uint16_t v) {
    b.push_back(static_cast<uint8_t>(v >> 8));
    b.push_back(static_cast<uint8_t>(v & 0xFF));
}

void put32(Bytes& b, uint32_t v) {
    put16(b, static_cast<uint16_t>(v >> 16));
    put16(b, static_cast<uint16_t>(v & 0xFFFF));
}

Bytes enc(const std::string& name) {
    Bytes b;
    std::size_t start = 0;
    while (start < name.size()) {
        std::size_t dot = name.find('.', start);
        if (dot == std::string::npos) dot = name.size();
        b.push_back(static_cast<uint8_t>(dot - start));
        b.insert(b.end(), name.begin() + static_cast<std::ptrdiff_t>(start),
                 name.begin() + static_cast<std::ptrdiff_t>(dot));
        start = dot + 1;
    }
    b.push_back(0);
    return b;
}

Bytes header(uint16_t id, uint16_t flags, uint16_t qd, uint16_t an) {
    Bytes b;
    put16(b, id);
    put16(b, flags);
    put16(b, qd);
    put16(b, an);
    put16(b, 0);
    put16(b, 0);
    return b;
}

Bytes question(const std::string& name, uint16_t qtype) {
    Bytes b = enc(name);
    put16(b, qtype);
    put16(b, 1);
    return b;
}

void append(Bytes& dst, const Bytes& src) {
    dst.insert(dst.end(), src.begin(), src.end());
}

// RR with arbitrary (already encoded) owner bytes
Bytes rr(const Bytes& owner, uint16_t type, uint32_t ttl, const Bytes& rdata) {
    Bytes b = owner;
    put16(b, type);
    put16(b, 1);
    put32(b, ttl);
    put16(b, static_cast<uint16_t>(rdata.size()));
    append(b, rdata);
    return b;
}

const Bytes kPtrQname = {0xC0, 0x0C};

ByteView view(const Bytes& b) {
    return ByteView(b.data(), b.size());
}

Bytes simple_a_response(const std::string& qname = "example.com") {
    Bytes m = header(0x1234, 0x8180, 1, 1);
    append(m, question(qname, 1));
    append(m, rr(kPtrQname, 1, 300, {1, 2, 3, 4}));
    return m;
}

Bytes ipv4_udp(const Bytes& payload, uint16_t sport, uint16_t dport) {
    Bytes p(20, 0);
    p[0] = 0x45;
    p[1] = 0x10; // TOS
    const uint16_t total = static_cast<uint16_t>(20 + 8 + payload.size());
    p[2] = static_cast<uint8_t>(total >> 8);
    p[3] = static_cast<uint8_t>(total & 0xFF);
    p[4] = 0xAB;
    p[5] = 0xCD; // id
    p[8] = 55;   // ttl
    p[9] = IPPROTO_UDP;
    const uint8_t src[4] = {8, 8, 8, 8};
    const uint8_t dst[4] = {192, 168, 1, 10};
    for (int i = 0; i < 4; ++i) {
        p[12 + i] = src[i];
        p[16 + i] = dst[i];
    }
    put16(p, sport);
    put16(p, dport);
    put16(p, static_cast<uint16_t>(8 + payload.size()));
    put16(p, 0);
    append(p, payload);
    return p;
}

// Builds IPv6 packet with optional single extension header of `ext_type` (8 bytes, followed by UDP).
Bytes ipv6_packet(const Bytes& payload, uint16_t sport, uint16_t dport, int ext_type = -1) {
    Bytes p(40, 0);
    p[0] = 0x60;
    p[1] = 0x0A;
    const std::size_t ext_len = ext_type >= 0 ? 8 : 0;
    const uint16_t plen = static_cast<uint16_t>(ext_len + 8 + payload.size());
    p[4] = static_cast<uint8_t>(plen >> 8);
    p[5] = static_cast<uint8_t>(plen & 0xFF);
    p[6] = ext_type >= 0 ? static_cast<uint8_t>(ext_type) : IPPROTO_UDP;
    p[7] = 60;
    for (int i = 0; i < 16; ++i) {
        p[8 + i] = static_cast<uint8_t>(0x20 + i);
        p[24 + i] = static_cast<uint8_t>(0x30 + i);
    }
    if (ext_type >= 0) {
        Bytes e(8, 0);
        e[0] = IPPROTO_UDP;
        e[1] = 0;
        append(p, e);
    }
    put16(p, sport);
    put16(p, dport);
    put16(p, static_cast<uint16_t>(8 + payload.size()));
    put16(p, 0);
    append(p, payload);
    return p;
}

// Returns true if the UDP checksum of a packet validates.
bool udp_checksum_ok(const Bytes& pkt, const PacketLayout& l) {
    const std::size_t udp_len = pkt.size() - l.l3_header_len;
    uint32_t sum = 0;
    auto add_words = [&](const uint8_t* d, std::size_t n) {
        for (std::size_t i = 0; i + 1 < n; i += 2) sum += (d[i] << 8) | d[i + 1];
    };
    if (l.ip_version == 4) {
        add_words(pkt.data() + 12, 8);
    } else {
        add_words(pkt.data() + 8, 32);
    }
    sum += static_cast<uint32_t>(udp_len) + IPPROTO_UDP;
    return inet_checksum(pkt.data() + l.l3_header_len, udp_len, sum) == 0;
}

} // namespace

TEST_CASE("dns_wire: parse A answer with compressed name") {
    const Bytes m = simple_a_response();
    ParsedResponse r;
    REQUIRE(parse_response(view(m), r));
    CHECK(r.id == 0x1234);
    CHECK(r.rcode == 0);
    CHECK(r.qname == "example.com");
    CHECK(r.qtype == 1);
    CHECK(r.qclass == 1);
    REQUIRE(r.addresses.size() == 1);
    CHECK(r.addresses[0].family == 4);
    CHECK(r.addresses[0].ttl == 300);
    CHECK(r.addresses[0].addr[0] == 1);
    CHECK(r.addresses[0].addr[3] == 4);
    CHECK(r.cname_chain.empty());
}

TEST_CASE("dns_wire: parse CNAME chain") {
    Bytes m = header(1, 0x8180, 1, 4);
    append(m, question("www.a.com", 1));
    append(m, rr(kPtrQname, 5, 60, enc("CDN.b.net")));
    append(m, rr(enc("cdn.b.net"), 5, 60, enc("edge.c.org")));
    append(m, rr(enc("edge.c.org"), 1, 30, {1, 2, 3, 4}));
    append(m, rr(enc("edge.c.org"), 1, 30, {5, 6, 7, 8}));
    ParsedResponse r;
    REQUIRE(parse_response(view(m), r));
    const std::vector<std::string> expected = {"www.a.com", "cdn.b.net", "edge.c.org"};
    CHECK(r.cname_chain == expected);
    REQUIRE(r.addresses.size() == 2);
    CHECK(r.addresses[1].addr[0] == 5);
}

TEST_CASE("dns_wire: parse AAAA answer") {
    Bytes m = header(1, 0x8180, 1, 1);
    append(m, question("v6.example.com", 28));
    Bytes addr(16, 0);
    addr[0] = 0x20;
    addr[1] = 0x01;
    addr[15] = 0x01;
    append(m, rr(kPtrQname, 28, 120, addr));
    ParsedResponse r;
    REQUIRE(parse_response(view(m), r));
    REQUIRE(r.addresses.size() == 1);
    CHECK(r.addresses[0].family == 6);
    CHECK(r.addresses[0].addr[0] == 0x20);
    CHECK(r.addresses[0].addr[15] == 0x01);
    CHECK(r.addresses[0].ttl == 120);
}

TEST_CASE("dns_wire: uppercase qname is lowercased") {
    const Bytes m = simple_a_response("ExAmPlE.CoM");
    ParsedResponse r;
    REQUIRE(parse_response(view(m), r));
    CHECK(r.qname == "example.com");
}

TEST_CASE("dns_wire: malformed responses are rejected") {
    ParsedResponse r;
    SUBCASE("pointer loop") {
        Bytes m = header(1, 0x8180, 1, 0);
        m.push_back(0xC0);
        m.push_back(0x0C); // points at itself
        put16(m, 1);
        put16(m, 1);
        CHECK_FALSE(parse_response(view(m), r));
    }
    SUBCASE("pointer beyond message") {
        Bytes m = header(1, 0x8180, 1, 0);
        m.push_back(0xC3);
        m.push_back(0xFF);
        put16(m, 1);
        put16(m, 1);
        CHECK_FALSE(parse_response(view(m), r));
    }
    SUBCASE("label longer than 63") {
        Bytes m = header(1, 0x8180, 1, 0);
        m.push_back(64);
        for (int i = 0; i < 64; ++i) m.push_back('a');
        m.push_back(0);
        put16(m, 1);
        put16(m, 1);
        CHECK_FALSE(parse_response(view(m), r));
    }
    SUBCASE("truncated RR") {
        Bytes m = simple_a_response();
        m.resize(m.size() - 2);
        CHECK_FALSE(parse_response(view(m), r));
    }
    SUBCASE("too short") {
        const Bytes m = {0, 1, 2};
        CHECK_FALSE(parse_response(view(m), r));
    }
    SUBCASE("query, not response") {
        Bytes m = simple_a_response();
        m[2] = 0x01;
        CHECK_FALSE(parse_response(view(m), r));
    }
    SUBCASE("qdcount 2") {
        Bytes m = simple_a_response();
        m[5] = 2;
        CHECK_FALSE(parse_response(view(m), r));
    }
}

TEST_CASE("dns_wire: NXDOMAIN response") {
    Bytes m = header(7, 0x8183, 1, 0);
    append(m, question("nope.example.com", 1));
    ParsedResponse r;
    REQUIRE(parse_response(view(m), r));
    CHECK(r.rcode == 3);
    CHECK(r.addresses.empty());
}

TEST_CASE("dns_wire: parse_packet_layout IPv4") {
    const Bytes dns = simple_a_response();
    SUBCASE("UDP") {
        const Bytes p = ipv4_udp(dns, 53, 40000);
        auto l = parse_packet_layout(view(p));
        REQUIRE(l);
        CHECK(l->ip_version == 4);
        CHECK(l->l4_proto == IPPROTO_UDP);
        CHECK(l->l3_header_len == 20);
        CHECK(l->l4_header_len == 8);
        CHECK(l->payload_offset == 28);
        CHECK(l->payload_len == dns.size());
    }
    SUBCASE("IHL 6 with options") {
        Bytes p = ipv4_udp(dns, 53, 40000);
        p[0] = 0x46;
        p.insert(p.begin() + 20, {0, 0, 0, 0});
        const uint16_t total = static_cast<uint16_t>(p.size());
        p[2] = static_cast<uint8_t>(total >> 8);
        p[3] = static_cast<uint8_t>(total & 0xFF);
        auto l = parse_packet_layout(view(p));
        REQUIRE(l);
        CHECK(l->l3_header_len == 24);
        CHECK(l->payload_offset == 32);
        CHECK(l->payload_len == dns.size());
    }
    SUBCASE("fragments rejected") {
        Bytes p = ipv4_udp(dns, 53, 40000);
        p[6] = 0x20; // MF
        CHECK_FALSE(parse_packet_layout(view(p)));
        p[6] = 0x00;
        p[7] = 0x08; // offset
        CHECK_FALSE(parse_packet_layout(view(p)));
    }
    SUBCASE("truncated") {
        const Bytes p = ipv4_udp(dns, 53, 40000);
        CHECK_FALSE(parse_packet_layout(ByteView(p.data(), 10)));
        CHECK_FALSE(parse_packet_layout(ByteView(p.data(), 24)));
        CHECK_FALSE(parse_packet_layout(ByteView(p.data(), p.size() - 1)));
        CHECK_FALSE(parse_packet_layout(ByteView()));
    }
    SUBCASE("TCP data offset 8") {
        Bytes p(20, 0);
        p[0] = 0x45;
        p[9] = IPPROTO_TCP;
        Bytes tcp(32, 0);
        tcp[12] = 0x80;
        append(p, tcp);
        append(p, {0, 3, 0xAA, 0xBB, 0xCC});
        p[2] = static_cast<uint8_t>(p.size() >> 8);
        p[3] = static_cast<uint8_t>(p.size() & 0xFF);
        auto l = parse_packet_layout(view(p));
        REQUIRE(l);
        CHECK(l->l4_proto == IPPROTO_TCP);
        CHECK(l->l4_header_len == 32);
        CHECK(l->payload_offset == 52);
        CHECK(l->payload_len == 5);
    }
}

TEST_CASE("dns_wire: parse_packet_layout IPv6") {
    const Bytes dns = simple_a_response();
    SUBCASE("UDP") {
        const Bytes p = ipv6_packet(dns, 53, 40000);
        auto l = parse_packet_layout(view(p));
        REQUIRE(l);
        CHECK(l->ip_version == 6);
        CHECK(l->l3_header_len == 40);
        CHECK(l->payload_offset == 48);
        CHECK(l->payload_len == dns.size());
    }
    SUBCASE("hop-by-hop") {
        const Bytes p = ipv6_packet(dns, 53, 40000, 0);
        auto l = parse_packet_layout(view(p));
        REQUIRE(l);
        CHECK(l->l3_header_len == 48);
        CHECK(l->l4_proto == IPPROTO_UDP);
        CHECK(l->payload_offset == 56);
        CHECK(l->payload_len == dns.size());
    }
    SUBCASE("fragment header rejected") {
        const Bytes p = ipv6_packet(dns, 53, 40000, 44);
        CHECK_FALSE(parse_packet_layout(view(p)));
    }
    SUBCASE("truncated") {
        const Bytes p = ipv6_packet(dns, 53, 40000);
        CHECK_FALSE(parse_packet_layout(ByteView(p.data(), 39)));
        CHECK_FALSE(parse_packet_layout(ByteView(p.data(), p.size() - 1)));
    }
}

TEST_CASE("dns_wire: tcp_single_message") {
    Bytes p = {0x00, 0x03, 0xAA, 0xBB, 0xCC};
    auto v = tcp_single_message(view(p));
    REQUIRE(v);
    CHECK(v->size() == 3);
    CHECK((*v)[0] == 0xAA);

    Bytes bad = {0x00, 0x09, 0xAA, 0xBB};
    CHECK_FALSE(tcp_single_message(view(bad)));
    CHECK_FALSE(tcp_single_message(ByteView(p.data(), 1)));
}

TEST_CASE("dns_wire: is_marker_name") {
    CHECK(is_marker_name("check.keen.pbr", "check.keen.pbr"));
    CHECK(is_marker_name("x.check.keen.pbr", "check.keen.pbr"));
    CHECK(is_marker_name("X.Check.Keen.PBR", "check.keen.pbr"));
    CHECK_FALSE(is_marker_name("notcheck.keen.pbr", "check.keen.pbr"));
    CHECK_FALSE(is_marker_name("keen.pbr", "check.keen.pbr"));
    CHECK_FALSE(is_marker_name("", "check.keen.pbr"));
}

TEST_CASE("dns_wire: build_marker_packet IPv4") {
    Bytes dns = header(0xBEEF, 0x8183 | 0x0100, 1, 0); // QR RD RA NXDOMAIN
    dns[2] = 0x81;
    dns[3] = 0x83;
    append(dns, question("check.keen.pbr", 1));
    const Bytes orig = ipv4_udp(dns, 53, 40000);

    auto layout = parse_packet_layout(view(orig));
    REQUIRE(layout);
    ParsedResponse resp;
    REQUIRE(parse_response(ByteView(orig.data() + layout->payload_offset, layout->payload_len), resp));
    CHECK(resp.rcode == 3);

    auto out = build_marker_packet(view(orig), *layout, resp, {127, 0, 0, 88});
    REQUIRE(out);
    auto ol = parse_packet_layout(view(*out));
    REQUIRE(ol);
    ParsedResponse r2;
    REQUIRE(parse_response(ByteView(out->data() + ol->payload_offset, ol->payload_len), r2));
    CHECK(r2.rcode == 0);
    CHECK(r2.id == 0xBEEF);
    CHECK((r2.flags & 0x0100) != 0);
    CHECK((r2.flags & 0x0400) != 0);
    CHECK(r2.qname == "check.keen.pbr");
    REQUIRE(r2.addresses.size() == 1);
    CHECK(r2.addresses[0].family == 4);
    CHECK(r2.addresses[0].addr[0] == 127);
    CHECK(r2.addresses[0].addr[3] == 88);

    // header checksum validates
    CHECK(inet_checksum(out->data(), 20) == 0);
    CHECK(udp_checksum_ok(*out, *ol));
    // untouched fields
    for (std::size_t i : {1u, 4u, 5u, 6u, 7u, 8u, 9u}) CHECK((*out)[i] == orig[i]);
    for (std::size_t i = 12; i < 24; ++i) CHECK((*out)[i] == orig[i]); // addrs + ports
    CHECK(((*out)[2] << 8 | (*out)[3]) == static_cast<int>(out->size()));
}

TEST_CASE("dns_wire: build_marker_packet keeps question case (0x20 randomization)") {
    Bytes dns = header(0x0101, 0x8183, 1, 0);
    const Bytes q = question("cHeCk.KeEn.PbR", 1);
    append(dns, q);
    const Bytes orig = ipv4_udp(dns, 53, 40000);

    auto layout = parse_packet_layout(view(orig));
    REQUIRE(layout);
    ParsedResponse resp;
    REQUIRE(parse_response(ByteView(orig.data() + layout->payload_offset, layout->payload_len), resp));
    CHECK(is_marker_name(resp.qname, "check.keen.pbr"));

    auto out = build_marker_packet(view(orig), *layout, resp, {127, 0, 0, 88});
    REQUIRE(out);
    const Bytes out_q(out->begin() + 28 + 12, out->begin() + 28 + 12 + static_cast<std::ptrdiff_t>(q.size()));
    CHECK(out_q == q);
}

TEST_CASE("dns_wire: build_marker_packet IPv6 AAAA gives NODATA") {
    Bytes dns = header(0x0042, 0x8183, 1, 0);
    append(dns, question("check.keen.pbr", 28));
    const Bytes orig = ipv6_packet(dns, 53, 41000);

    auto layout = parse_packet_layout(view(orig));
    REQUIRE(layout);
    ParsedResponse resp;
    REQUIRE(parse_response(ByteView(orig.data() + layout->payload_offset, layout->payload_len), resp));

    auto out = build_marker_packet(view(orig), *layout, resp, {127, 0, 0, 88});
    REQUIRE(out);
    auto ol = parse_packet_layout(view(*out));
    REQUIRE(ol);
    ParsedResponse r2;
    REQUIRE(parse_response(ByteView(out->data() + ol->payload_offset, ol->payload_len), r2));
    CHECK(r2.rcode == 0);
    CHECK(r2.addresses.empty());
    CHECK(r2.qtype == 28);
    CHECK(udp_checksum_ok(*out, *ol));
    for (std::size_t i = 8; i < 44; ++i) CHECK((*out)[i] == orig[i]);
}

TEST_CASE("dns_wire: build_marker_packet rejects IPv6 extension headers and TCP") {
    Bytes dns = header(1, 0x8183, 1, 0);
    append(dns, question("check.keen.pbr", 1));
    const Bytes orig = ipv6_packet(dns, 53, 41000, 0);
    auto layout = parse_packet_layout(view(orig));
    REQUIRE(layout);
    ParsedResponse resp;
    REQUIRE(parse_response(ByteView(orig.data() + layout->payload_offset, layout->payload_len), resp));
    CHECK_FALSE(build_marker_packet(view(orig), *layout, resp, {127, 0, 0, 88}));

    PacketLayout tcp = *layout;
    tcp.l4_proto = IPPROTO_TCP;
    CHECK_FALSE(build_marker_packet(view(orig), tcp, resp, {127, 0, 0, 88}));
}

TEST_CASE("dns_wire: invalid qname with non-ASCII byte is rejected") {
    // Build a DNS response with a qname containing an invalid byte (0xFF)
    Bytes m = header(0x1234, 0x8180, 1, 1);
    // Manually encode qname with invalid byte
    Bytes qname_with_invalid = {5, 'e', 'x', 'a', 'm', 'p', 0xFF, 3, 'c', 'o', 'm', 0};
    append(m, qname_with_invalid);
    put16(m, 1);  // qtype A
    put16(m, 1);  // qclass IN
    append(m, rr(kPtrQname, 1, 300, {1, 2, 3, 4}));

    ParsedResponse r;
    // Should reject the response because qname contains invalid byte
    CHECK_FALSE(parse_response(view(m), r));
}

TEST_CASE("dns_wire: valid CNAME chain with underscores and hyphens are kept") {
    Bytes m = header(0x1234, 0x8180, 1, 2);
    append(m, question("example.com", 1));
    // CNAME record with valid special characters
    Bytes cname_owner = enc("example.com");
    Bytes cname_target = enc("test_v2.example.com");
    append(m, rr(cname_owner, 5, 300, cname_target));
    append(m, rr(kPtrQname, 1, 300, {1, 2, 3, 4}));

    ParsedResponse r;
    REQUIRE(parse_response(view(m), r));
    CHECK(r.qname == "example.com");
    CHECK(r.cname_chain.size() == 2);
    CHECK(std::find(r.cname_chain.begin(), r.cname_chain.end(), "test_v2.example.com") != r.cname_chain.end());
}

TEST_CASE("dns_wire: invalid CNAME target is skipped (not learned)") {
    Bytes m = header(0x1234, 0x8180, 1, 2);
    append(m, question("example.com", 1));
    // CNAME record with invalid byte in target - manually encode name with invalid byte in label
    Bytes cname_owner = enc("example.com");
    // Create target with invalid byte: test<0xFF>.com where the label is "test" + byte 0xFF (length 5)
    Bytes cname_target = {5, 't', 'e', 's', 't', 0xFF, 3, 'c', 'o', 'm', 0};
    append(m, rr(cname_owner, 5, 300, cname_target));
    append(m, rr(kPtrQname, 1, 300, {1, 2, 3, 4}));

    ParsedResponse r;
    REQUIRE(parse_response(view(m), r));
    CHECK(r.qname == "example.com");
    // The invalid target should not be in the CNAME chain (owner is valid, target is not)
    // The owner gets added because it's valid, but the target is invalid so won't be added
    CHECK(r.cname_chain.size() == 1);
    CHECK(r.cname_chain[0] == "example.com");
}

TEST_CASE("dns_wire: inet_checksum RFC 1071 vector") {
    const uint8_t data[] = {0x00, 0x01, 0xf2, 0x03, 0xf4, 0xf5, 0xf6, 0xf7};
    CHECK(inet_checksum(data, sizeof(data)) == 0x220d);
}

TEST_CASE("dns_wire: CNAME chain capped at 32, addresses still parsed") {
    // Build a DNS response with many CNAME records to exceed the cap.
    // Use format: qname -> target0 -> target1 -> ... -> targetN -> A records
    Bytes m = header(0x1234, 0x8180, 1, 200 + 2);
    append(m, question("www.example.com", 1));

    // Add 200 distinct CNAME records.
    for (int i = 0; i < 200; ++i) {
        std::string target_name = "cname" + std::to_string(i) + ".example.com";
        if (i == 0) {
            append(m, rr(kPtrQname, 5, 60, enc(target_name)));
        } else {
            std::string prev_name = "cname" + std::to_string(i - 1) + ".example.com";
            append(m, rr(enc(prev_name), 5, 60, enc(target_name)));
        }
    }

    // Add two A records to verify they are still parsed.
    append(m, rr(enc("cname199.example.com"), 1, 30, {1, 2, 3, 4}));
    append(m, rr(enc("cname199.example.com"), 1, 30, {5, 6, 7, 8}));

    ParsedResponse r;
    REQUIRE(parse_response(view(m), r));
    // CNAME chain should be capped at 32.
    CHECK(r.cname_chain.size() == 32);
    // Addresses should still be parsed.
    REQUIRE(r.addresses.size() == 2);
    CHECK(r.addresses[0].addr[0] == 1);
    CHECK(r.addresses[1].addr[0] == 5);
}

TEST_CASE("dns_wire: CNAME duplicates collapse within cap") {
    // Build a DNS response with duplicate CNAME records.
    Bytes m = header(0x1234, 0x8180, 1, 5);
    append(m, question("www.example.com", 1));

    // Chain: www.example.com -> a.com -> b.com -> a.com (duplicate) -> c.com -> A record
    append(m, rr(kPtrQname, 5, 60, enc("a.com")));
    append(m, rr(enc("a.com"), 5, 60, enc("b.com")));
    append(m, rr(enc("b.com"), 5, 60, enc("a.com")));  // duplicate
    append(m, rr(enc("a.com"), 5, 60, enc("c.com")));
    append(m, rr(enc("c.com"), 1, 30, {1, 2, 3, 4}));

    ParsedResponse r;
    REQUIRE(parse_response(view(m), r));
    // Should contain: www.example.com, a.com, b.com, c.com (without duplicate a.com from third CNAME)
    CHECK(r.cname_chain.size() == 4);
    CHECK(r.cname_chain[0] == "www.example.com");
    CHECK(r.cname_chain[1] == "a.com");
    CHECK(r.cname_chain[2] == "b.com");
    CHECK(r.cname_chain[3] == "c.com");
    REQUIRE(r.addresses.size() == 1);
    CHECK(r.addresses[0].addr[0] == 1);
}

TEST_CASE("dns_wire: parse_question_only extracts qname and qtype without parsing answers") {
    // Build a DNS response with a truncated/garbage answer section.
    Bytes m = header(0x1234, 0x8180, 1, 1);
    append(m, question("example.com", 1));  // A query
    // Add garbage answer section that would fail full parse
    append(m, Bytes{0xFF, 0xFF, 0xFF});

    ParsedResponse r;
    // parse_question_only should succeed even with garbage answers
    REQUIRE(parse_question_only(view(m), r));
    CHECK(r.qname == "example.com");
    CHECK(r.qtype == 1);  // A query
    CHECK(r.addresses.empty());  // Answers not parsed
}

TEST_CASE("dns_wire: parse_question_only handles AAAA queries") {
    Bytes m = header(0x1234, 0x8180, 1, 1);
    append(m, question("example.com", 28));  // AAAA query (type 28)
    append(m, rr(kPtrQname, 28, 300, {0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}));

    ParsedResponse r;
    REQUIRE(parse_question_only(view(m), r));
    CHECK(r.qname == "example.com");
    CHECK(r.qtype == 28);  // AAAA
    CHECK(r.addresses.empty());  // Answers not parsed
}

TEST_CASE("dns_wire: is_learnable_address filters non-routable addresses") {
    std::array<uint8_t, 16> addr{};

    // Test IPv4 addresses
    // 0.0.0.0: not learnable
    addr = {};
    CHECK_FALSE(is_learnable_address(4, addr.data()));

    // 127.0.0.1 (loopback): not learnable
    addr = {};
    addr[0] = 127;
    addr[3] = 1;
    CHECK_FALSE(is_learnable_address(4, addr.data()));

    // 127.255.255.255 (loopback): not learnable
    addr = {};
    addr[0] = 127;
    addr[1] = 255;
    addr[2] = 255;
    addr[3] = 255;
    CHECK_FALSE(is_learnable_address(4, addr.data()));

    // 1.2.3.4: learnable
    addr = {};
    addr[0] = 1;
    addr[1] = 2;
    addr[2] = 3;
    addr[3] = 4;
    CHECK(is_learnable_address(4, addr.data()));

    // 8.8.8.8: learnable
    addr = {};
    addr[0] = 8;
    addr[1] = 8;
    addr[2] = 8;
    addr[3] = 8;
    CHECK(is_learnable_address(4, addr.data()));

    // Test IPv6 addresses
    // :: (all zeros / unspecified): not learnable
    addr = {};
    CHECK_FALSE(is_learnable_address(6, addr.data()));

    // ::1 (loopback): not learnable
    addr = {};
    addr[15] = 1;
    CHECK_FALSE(is_learnable_address(6, addr.data()));

    // 2001:db8::1: learnable
    addr = {};
    addr[0] = 0x20;
    addr[1] = 0x01;
    addr[2] = 0x0d;
    addr[3] = 0xb8;
    addr[15] = 1;
    CHECK(is_learnable_address(6, addr.data()));
}
