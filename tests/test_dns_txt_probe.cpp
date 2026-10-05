#include <doctest/doctest.h>

#include "../src/dns/dns_txt_probe.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

using namespace keen_pbr3;

namespace {

constexpr std::string_view kName = "config-hash.keen.pbr";

using Bytes = std::vector<std::uint8_t>;

void put16(Bytes& out, std::uint16_t v) {
    out.push_back(static_cast<std::uint8_t>(v >> 8));
    out.push_back(static_cast<std::uint8_t>(v & 0xFF));
}

void put_name(Bytes& out, std::string_view name) {
    std::size_t start = 0;
    while (start < name.size()) {
        std::size_t dot = name.find('.', start);
        if (dot == std::string_view::npos) dot = name.size();
        out.push_back(static_cast<std::uint8_t>(dot - start));
        out.insert(out.end(), name.begin() + static_cast<std::ptrdiff_t>(start),
                   name.begin() + static_cast<std::ptrdiff_t>(dot));
        start = dot + 1;
    }
    out.push_back(0);
}

// Response with one question (config-hash.keen.pbr TXT IN) and `answers`
// already-encoded answer records.
Bytes response(std::uint16_t id, std::uint16_t flags, std::uint16_t ancount,
               const Bytes& answers, std::string_view qname = kName,
               std::uint16_t qtype = 16) {
    Bytes out;
    put16(out, id);
    put16(out, flags);
    put16(out, 1);
    put16(out, ancount);
    put16(out, 0);
    put16(out, 0);
    put_name(out, qname);
    put16(out, qtype);
    put16(out, 1);
    out.insert(out.end(), answers.begin(), answers.end());
    return out;
}

// Answer record whose owner is a compression pointer to the question name.
Bytes txt_answer(const std::vector<std::string>& chunks, std::uint16_t type = 16) {
    Bytes rdata;
    for (const auto& chunk : chunks) {
        rdata.push_back(static_cast<std::uint8_t>(chunk.size()));
        rdata.insert(rdata.end(), chunk.begin(), chunk.end());
    }
    Bytes out{0xC0, 0x0C};
    put16(out, type);
    put16(out, 1);
    put16(out, 0);
    put16(out, 60);
    put16(out, static_cast<std::uint16_t>(rdata.size()));
    out.insert(out.end(), rdata.begin(), rdata.end());
    return out;
}

DnsTxtProbeResult parse(const Bytes& packet, std::uint16_t id = 0x1234) {
    return parse_dns_txt_response(packet.data(), packet.size(), id, kName);
}

} // namespace

TEST_CASE("dns txt probe: query packet layout") {
    const Bytes query = build_dns_txt_query(0xABCD, kName);
    const Bytes expected = {
        0xAB, 0xCD, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x0B, 'c', 'o', 'n', 'f', 'i', 'g', '-', 'h', 'a', 's', 'h',
        0x04, 'k', 'e', 'e', 'n',
        0x03, 'p', 'b', 'r',
        0x00, 0x00, 0x10, 0x00, 0x01};
    CHECK(query == expected);
}

TEST_CASE("dns txt probe: TXT answer is returned") {
    const std::string txt = "0123456789abcdef0123456789abcdef|1234|1700000000";
    const auto result = parse(response(0x1234, 0x8180, 1, txt_answer({txt})));
    CHECK(result.status == DnsTxtProbeStatus::Ok);
    CHECK(result.txt == txt);
}

TEST_CASE("dns txt probe: character-strings are concatenated") {
    const auto result = parse(response(0x1234, 0x8180, 1, txt_answer({"abc|", "12|", "34"})));
    CHECK(result.status == DnsTxtProbeStatus::Ok);
    CHECK(result.txt == "abc|12|34");
}

TEST_CASE("dns txt probe: first TXT answer wins and other types are skipped") {
    Bytes answers = txt_answer({"1.2.3.4"}, /*type=*/16);
    Bytes cname = txt_answer({"x"}, /*type=*/99);
    Bytes both = cname;
    both.insert(both.end(), answers.begin(), answers.end());
    const auto result = parse(response(0x1234, 0x8180, 2, both));
    CHECK(result.status == DnsTxtProbeStatus::Ok);
    CHECK(result.txt == "1.2.3.4");
}

TEST_CASE("dns txt probe: NXDOMAIN and empty NOERROR mean missing") {
    CHECK(parse(response(0x1234, 0x8183, 0, {})).status == DnsTxtProbeStatus::Missing);
    CHECK(parse(response(0x1234, 0x8180, 0, {})).status == DnsTxtProbeStatus::Missing);
}

TEST_CASE("dns txt probe: failures") {
    SUBCASE("servfail") {
        const auto r = parse(response(0x1234, 0x8182, 0, {}));
        CHECK(r.status == DnsTxtProbeStatus::QueryFailed);
        CHECK(r.error == "DNS rcode 2");
    }
    SUBCASE("refused") {
        CHECK(parse(response(0x1234, 0x8185, 0, {})).status == DnsTxtProbeStatus::QueryFailed);
    }
    SUBCASE("truncated") {
        const auto r = parse(response(0x1234, 0x8380, 1, txt_answer({"x"})));
        CHECK(r.status == DnsTxtProbeStatus::QueryFailed);
        CHECK(r.error == "DNS response truncated");
    }
    SUBCASE("transaction id mismatch") {
        const auto r = parse(response(0x4321, 0x8180, 1, txt_answer({"x"})));
        CHECK(r.status == DnsTxtProbeStatus::IdMismatch);
        CHECK(r.error == "DNS response id mismatch");
    }
    SUBCASE("question name mismatch") {
        const auto r = parse(response(0x1234, 0x8180, 1, txt_answer({"x"}), "other.example"));
        CHECK(r.status == DnsTxtProbeStatus::QueryFailed);
    }
    SUBCASE("question type mismatch") {
        const auto r = parse(response(0x1234, 0x8180, 0, {}, kName, /*qtype=*/1));
        CHECK(r.status == DnsTxtProbeStatus::QueryFailed);
    }
    SUBCASE("not a response") {
        CHECK(parse(response(0x1234, 0x0100, 0, {})).status == DnsTxtProbeStatus::QueryFailed);
    }
    SUBCASE("garbage and short packets") {
        CHECK(parse(Bytes{1, 2, 3}).status == DnsTxtProbeStatus::QueryFailed);
        CHECK(parse(Bytes{}).status == DnsTxtProbeStatus::QueryFailed);
    }
    SUBCASE("truncated answer record") {
        Bytes packet = response(0x1234, 0x8180, 1, txt_answer({"abcdef"}));
        packet.resize(packet.size() - 3);
        CHECK(parse(packet).status == DnsTxtProbeStatus::QueryFailed);
    }
    SUBCASE("chunk length overruns rdata") {
        Bytes answer = txt_answer({"abc"});
        answer[answer.size() - 4] = 9;  // length byte of the only chunk
        CHECK(parse(response(0x1234, 0x8180, 1, answer)).status == DnsTxtProbeStatus::QueryFailed);
    }
}

TEST_CASE("dns txt probe: unreachable address fails without throwing") {
    const auto bad = probe_dns_txt("not-an-ip", 53, kName, std::chrono::milliseconds(50));
    CHECK(bad.status == DnsTxtProbeStatus::QueryFailed);
    CHECK_FALSE(bad.error.empty());
}
