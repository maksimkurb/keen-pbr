#include <doctest/doctest.h>

#include "../src/intercept/dns_tcp_reassembly.hpp"

#include <chrono>
#include <cstdint>
#include <vector>

using namespace keen_pbr3;

namespace {

using Bytes = std::vector<uint8_t>;
using Clock = std::chrono::steady_clock;
using Messages = DnsTcpReassembler::Messages;

l7::FlowKey key(uint16_t dport = 40000) {
    l7::FlowKey k;
    k.family = 4;
    k.src = {8, 8, 8, 8};
    k.dst = {192, 168, 1, 10};
    k.sport = 53;
    k.dport = dport;
    return k;
}

// Length-prefixed message of `len` bytes, every byte (after the prefix) = `fill`.
Bytes framed(std::size_t len, uint8_t fill) {
    Bytes b{static_cast<uint8_t>(len >> 8), static_cast<uint8_t>(len & 0xFF)};
    b.insert(b.end(), len, fill);
    return b;
}

Bytes slice(const Bytes& b, std::size_t from, std::size_t to) {
    return Bytes(b.begin() + static_cast<std::ptrdiff_t>(from), b.begin() + static_cast<std::ptrdiff_t>(to));
}

ByteView v(const Bytes& b) { return ByteView(b.data(), b.size()); }

bool is_message(ByteView m, std::size_t len, uint8_t fill) {
    if (m.size() != len) return false;
    for (std::size_t i = 0; i < len; ++i) {
        if (m.data()[i] != fill) return false;
    }
    return true;
}

}  // namespace

TEST_CASE("dns tcp reassembly: a message inside one segment is returned as a view of it") {
    DnsTcpReassembler r;
    Messages out;
    const Bytes seg = framed(100, 0xAA);
    r.feed(key(), 1000, v(seg), Clock::now(), out);
    REQUIRE(out.count == 1);
    CHECK(is_message(out.view[0], 100, 0xAA));
    CHECK(out.view[0].data() == seg.data() + 2);
    CHECK(r.buffered_flows() == 0);
}

TEST_CASE("dns tcp reassembly: a message split over two segments completes once") {
    DnsTcpReassembler r;
    Messages out;
    const Bytes all = framed(300, 0x11);
    const auto now = Clock::now();
    r.feed(key(), 5000, v(slice(all, 0, 120)), now, out);
    CHECK(out.count == 0);
    CHECK(r.buffered_flows() == 1);
    r.feed(key(), 5000 + 120, v(slice(all, 120, all.size())), now, out);
    REQUIRE(out.count == 1);
    CHECK(is_message(out.view[0], 300, 0x11));
    CHECK(r.buffered_flows() == 0);
    // Nothing is learned a second time from a retransmission of the tail.
    r.feed(key(), 5000 + 120, v(slice(all, 120, all.size())), now, out);
    CHECK(out.count == 0);
}

TEST_CASE("dns tcp reassembly: three segments, and a length prefix split across segments") {
    DnsTcpReassembler r;
    Messages out;
    const Bytes all = framed(500, 0x22);
    const auto now = Clock::now();
    r.feed(key(), 1, v(slice(all, 0, 1)), now, out);  // first prefix byte only
    CHECK(out.count == 0);
    r.feed(key(), 2, v(slice(all, 1, 250)), now, out);
    CHECK(out.count == 0);
    r.feed(key(), 251, v(slice(all, 250, all.size())), now, out);
    REQUIRE(out.count == 1);
    CHECK(is_message(out.view[0], 500, 0x22));
}

TEST_CASE("dns tcp reassembly: several messages in one segment, and a tail plus a new message") {
    DnsTcpReassembler r;
    Messages out;
    Bytes a = framed(40, 1);
    const Bytes b = framed(50, 2);
    const Bytes c = framed(60, 3);
    Bytes both = a;
    both.insert(both.end(), b.begin(), b.end());
    const auto now = Clock::now();
    r.feed(key(), 10, v(both), now, out);
    REQUIRE(out.count == 2);
    CHECK(is_message(out.view[0], 40, 1));
    CHECK(is_message(out.view[1], 50, 2));

    // Message c split; its tail shares a segment with a complete message d.
    const Bytes d = framed(70, 4);
    Bytes tail = slice(c, 20, c.size());
    tail.insert(tail.end(), d.begin(), d.end());
    uint32_t seq = 10 + static_cast<uint32_t>(both.size());
    r.feed(key(), seq, v(slice(c, 0, 20)), now, out);
    CHECK(out.count == 0);
    r.feed(key(), seq + 20, v(tail), now, out);
    REQUIRE(out.count == 2);
    CHECK(is_message(out.view[0], 60, 3));
    CHECK(is_message(out.view[1], 70, 4));
}

TEST_CASE("dns tcp reassembly: retransmission is ignored, partial overlap keeps the new bytes") {
    DnsTcpReassembler r;
    Messages out;
    const Bytes all = framed(200, 0x33);
    const auto now = Clock::now();
    r.feed(key(), 100, v(slice(all, 0, 80)), now, out);
    r.feed(key(), 100, v(slice(all, 0, 80)), now, out);  // exact retransmission
    CHECK(out.count == 0);
    r.feed(key(), 100 + 40, v(slice(all, 40, all.size())), now, out);  // overlaps the first 40 bytes
    REQUIRE(out.count == 1);
    CHECK(is_message(out.view[0], 200, 0x33));
}

TEST_CASE("dns tcp reassembly: a gap poisons the flow instead of corrupting it") {
    DnsTcpReassembler r;
    Messages out;
    const Bytes all = framed(300, 0x44);
    const auto now = Clock::now();
    r.feed(key(), 1, v(slice(all, 0, 100)), now, out);
    r.feed(key(), 1 + 200, v(slice(all, 200, all.size())), now, out);  // out of order: 100..200 missing
    CHECK(out.count == 0);
    r.feed(key(), 1 + 100, v(slice(all, 100, 200)), now, out);  // the missing part arrives late
    CHECK(out.count == 0);
    CHECK(r.buffered_flows() == 0);
    // Another connection is not affected.
    const Bytes ok = framed(30, 9);
    r.feed(key(40001), 7, v(ok), now, out);
    CHECK(out.count == 1);
    // Closing (FIN/RST) clears the poison: a new stream on the same tuple works.
    r.close(key());
    r.feed(key(), 9000, v(ok), now, out);
    CHECK(out.count == 1);
}

TEST_CASE("dns tcp reassembly: invalid or oversize lengths are rejected") {
    DnsTcpReassembler r(std::chrono::seconds(2), 16, 4, /*max_message=*/1000);
    Messages out;
    const auto now = Clock::now();
    Bytes tiny = framed(5, 1);  // shorter than a DNS header
    r.feed(key(1), 1, v(tiny), now, out);
    CHECK(out.count == 0);
    Bytes big = framed(1001, 1);
    r.feed(key(2), 1, v(big), now, out);
    CHECK(out.count == 0);
    CHECK(r.buffered_flows() == 0);
    Bytes fits = framed(1000, 1);
    r.feed(key(3), 1, v(slice(fits, 0, 500)), now, out);
    r.feed(key(3), 501, v(slice(fits, 500, fits.size())), now, out);
    CHECK(out.count == 1);
    // The protocol maximum is 65535 and is accepted.
    DnsTcpReassembler r2;
    const Bytes max = framed(65535, 7);
    r2.feed(key(), 1, v(slice(max, 0, 30000)), now, out);
    r2.feed(key(), 30001, v(slice(max, 30000, max.size())), now, out);
    REQUIRE(out.count == 1);
    CHECK(is_message(out.view[0], 65535, 7));
}

TEST_CASE("dns tcp reassembly: flow cap evicts the least recently fed flow") {
    DnsTcpReassembler r(std::chrono::seconds(60), /*max_flows=*/4, /*max_buffered_flows=*/4);
    Messages out;
    auto now = Clock::now();
    const Bytes all = framed(100, 5);
    for (uint16_t p = 1; p <= 4; ++p) {
        now += std::chrono::milliseconds(1);
        r.feed(key(p), 1, v(slice(all, 0, 50)), now, out);
    }
    CHECK(r.flows() == 4);
    now += std::chrono::milliseconds(1);
    r.feed(key(5), 1, v(slice(all, 0, 50)), now, out);  // evicts flow 1
    CHECK(r.flows() == 4);
    now += std::chrono::milliseconds(1);
    r.feed(key(1), 51, v(slice(all, 50, all.size())), now, out);  // its state is gone
    CHECK(out.count == 0);
    r.feed(key(3), 51, v(slice(all, 50, all.size())), now, out);  // flow 3 survived
    CHECK(out.count == 1);
}

TEST_CASE("dns tcp reassembly: only a bounded number of flows buffer at once") {
    DnsTcpReassembler r(std::chrono::seconds(60), 16, /*max_buffered_flows=*/2);
    Messages out;
    auto now = Clock::now();
    const Bytes all = framed(100, 5);
    for (uint16_t p = 1; p <= 3; ++p) {
        now += std::chrono::milliseconds(1);
        r.feed(key(p), 1, v(slice(all, 0, 50)), now, out);
    }
    CHECK(r.buffered_flows() == 2);
    now += std::chrono::milliseconds(1);
    r.feed(key(1), 51, v(slice(all, 50, all.size())), now, out);  // oldest buffer was dropped
    CHECK(out.count == 0);
    r.feed(key(3), 51, v(slice(all, 50, all.size())), now, out);
    CHECK(out.count == 1);
}

TEST_CASE("dns tcp reassembly: idle flows expire after the ttl") {
    DnsTcpReassembler r(std::chrono::milliseconds(2000));
    Messages out;
    auto now = Clock::now();
    const Bytes all = framed(100, 5);
    r.feed(key(), 1, v(slice(all, 0, 50)), now, out);
    CHECK(r.flows() == 1);
    now += std::chrono::milliseconds(2500);
    r.feed(key(2), 1, v(framed(30, 1)), now, out);  // any feed sweeps expired flows
    CHECK(r.flows() == 1);
    CHECK(r.buffered_flows() == 0);
    r.feed(key(), 51, v(slice(all, 50, all.size())), now, out);
    CHECK(out.count == 0);
}

TEST_CASE("dns tcp reassembly: more complete messages than the per-segment limit are counted") {
    DnsTcpReassembler r;
    Messages out;
    Bytes seg;
    for (int i = 0; i < 20; ++i) {
        const Bytes m = framed(20, static_cast<uint8_t>(i));
        seg.insert(seg.end(), m.begin(), m.end());
    }
    r.feed(key(), 1, v(seg), Clock::now(), out);
    CHECK(out.count == DnsTcpReassembler::kMaxMessagesPerSegment);
    CHECK(out.dropped == 4);
}
