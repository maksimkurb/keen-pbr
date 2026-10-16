#include <doctest/doctest.h>

#include "fake_dns_server.hpp"
#include "../src/dns/dns_client.hpp"
#include "../src/dns/dns_txt_probe.hpp"

#include <chrono>

using namespace keen_pbr3;
using namespace fake_dns;
using std::chrono::milliseconds;

namespace {
constexpr milliseconds kTimeout{2000};

DnsQueryResult q(const FakeDnsServer& s, std::uint16_t type = kDnsTypeA) {
    return dns_query("127.0.0.1", s.port(), "Host.Example.", type, kDnsClassIN, kTimeout);
}
}

TEST_CASE("build_dns_query: EDNS OPT") {
    const Bytes with = build_dns_query(0x1234, "a.b", kDnsTypeA, kDnsClassIN, true);
    const Bytes without = build_dns_query(0x1234, "a.b", kDnsTypeA, kDnsClassIN, false);
    CHECK(with[10] == 0); CHECK(with[11] == 1);
    CHECK(without[10] == 0); CHECK(without[11] == 0);
    CHECK(with.size() == without.size() + 11);
    const Bytes opt{0x00, 0x00, 0x29, 0x04, 0xD0, 0, 0, 0, 0, 0, 0};
    CHECK(Bytes(with.end() - 11, with.end()) == opt);
}

TEST_CASE("dns_query: A and AAAA answers") {
    FakeDnsServer s([](const Bytes& qr, bool) {
        return make_reply(qr, 0, {a_rr("192.0.2.10"), aaaa_rr("2001:db8::1")});
    });
    auto a = q(s);
    CHECK(a.status == DnsQueryStatus::Ok);
    REQUIRE(a.addresses.size() == 1);
    CHECK(a.addresses[0] == "192.0.2.10");
    CHECK_FALSE(a.via_tcp);
    auto aaaa = q(s, kDnsTypeAAAA);
    CHECK(aaaa.status == DnsQueryStatus::Ok);
    REQUIRE(aaaa.addresses.size() == 1);
    CHECK(aaaa.addresses[0] == "2001:db8::1");
}

TEST_CASE("dns_query: rcode mapping") {
    auto status_for = [](std::uint8_t rcode) {
        FakeDnsServer s([rcode](const Bytes& qr, bool) { return make_reply(qr, rcode); });
        return q(s);
    };
    CHECK(status_for(0).status == DnsQueryStatus::NoData);
    CHECK(status_for(3).status == DnsQueryStatus::NxDomain);
    CHECK(status_for(2).status == DnsQueryStatus::ServFail);
    CHECK(status_for(5).status == DnsQueryStatus::Refused);
    auto other = status_for(4);
    CHECK(other.status == DnsQueryStatus::OtherRcode);
    CHECK(other.rcode == 4);
    CHECK(std::string(dns_query_status_name(DnsQueryStatus::NxDomain)) == "nxdomain");
}

TEST_CASE("dns_query: wrong id ignored") {
    FakeDnsServer s([](const Bytes& qr, bool) {
        const int wrong = ((qr[0] << 8 | qr[1]) + 1) & 0xFFFF;
        return std::vector<Bytes>{make_reply(qr, 0, {a_rr("192.0.2.99")}, false, wrong),
                                  make_reply(qr, 0, {a_rr("192.0.2.10")})};
    });
    auto r = q(s);
    CHECK(r.status == DnsQueryStatus::Ok);
    REQUIRE(r.addresses.size() == 1);
    CHECK(r.addresses[0] == "192.0.2.10");
}

TEST_CASE("dns_query: TC falls back to TCP") {
    FakeDnsServer s([](const Bytes& qr, bool tcp) {
        return tcp ? make_reply(qr, 0, {a_rr("192.0.2.10")}) : make_reply(qr, 0, {}, true);
    });
    auto r = q(s);
    CHECK(r.status == DnsQueryStatus::Ok);
    CHECK(r.via_tcp);
    REQUIRE(r.addresses.size() == 1);
}

TEST_CASE("dns_query: timeout") {
    FakeDnsServer s([](const Bytes&, bool) { return Bytes{}; });
    const auto t0 = std::chrono::steady_clock::now();
    auto r = dns_query("127.0.0.1", s.port(), "x.example", kDnsTypeA, kDnsClassIN, milliseconds(200));
    CHECK(r.status == DnsQueryStatus::Timeout);
    CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(1));
}

TEST_CASE("dns_query: closed port and invalid address") {
    const auto port = unused_udp_port();
    auto r = dns_query("127.0.0.1", port, "x.example", kDnsTypeA, kDnsClassIN, kTimeout);
    CHECK(r.status == DnsQueryStatus::NetworkError);
    CHECK(dns_query("not-an-ip", 53, "x.example", kDnsTypeA, kDnsClassIN, kTimeout).status ==
          DnsQueryStatus::NetworkError);
}

TEST_CASE("dns_probe_responding") {
    FakeDnsServer s([](const Bytes& qr, bool) { return make_reply(qr, 5); });
    CHECK(dns_probe_responding("127.0.0.1", s.port(), kTimeout));
    CHECK_FALSE(dns_probe_responding("127.0.0.1", unused_udp_port(), kTimeout));
}

TEST_CASE("probe_dns_txt: answered and rcode") {
    FakeDnsServer nx([](const Bytes& qr, bool) { return make_reply(qr, 3); });
    auto r = probe_dns_txt("127.0.0.1", nx.port(), "x.keen.pbr", kTimeout);
    CHECK(r.status == DnsTxtProbeStatus::Missing);
    CHECK(r.answered);
    REQUIRE(r.rcode.has_value());
    CHECK(*r.rcode == 3);

    FakeDnsServer silent([](const Bytes&, bool) { return Bytes{}; });
    auto t = probe_dns_txt("127.0.0.1", silent.port(), "x.keen.pbr", milliseconds(200));
    CHECK_FALSE(t.answered);
    CHECK_FALSE(t.rcode.has_value());
}
