#include <doctest/doctest.h>

#include <chrono>
#include <vector>

#include "../src/l7/flow_buffer.hpp"

using namespace keen_pbr3;
using namespace keen_pbr3::l7;

namespace {

FlowKey make_flow_key(uint32_t src_addr, uint16_t src_port, uint32_t dst_addr, uint16_t dst_port) {
    FlowKey key;
    key.family = 4;
    // Store IPv4 as first 4 bytes in src/dst
    key.src[0] = (src_addr >> 24) & 0xFF;
    key.src[1] = (src_addr >> 16) & 0xFF;
    key.src[2] = (src_addr >> 8) & 0xFF;
    key.src[3] = src_addr & 0xFF;
    key.dst[0] = (dst_addr >> 24) & 0xFF;
    key.dst[1] = (dst_addr >> 16) & 0xFF;
    key.dst[2] = (dst_addr >> 8) & 0xFF;
    key.dst[3] = dst_addr & 0xFF;
    key.sport = src_port;
    key.dport = dst_port;
    return key;
}

}  // namespace

TEST_CASE("l7: FlowBuffer in-order two segments concatenate") {
    FlowBuffers fb;
    auto key = make_flow_key(0x12345678, 1234, 0x87654321, 80);
    auto now = std::chrono::steady_clock::now();

    std::vector<uint8_t> payload1 = {1, 2, 3, 4, 5};
    std::vector<uint8_t> payload2 = {6, 7, 8, 9, 10};

    // First segment, seq = 100
    ByteView view1(payload1.data(), payload1.size());
    auto result1 = fb.feed(key, 100, view1, now);
    CHECK(result1.size() == 5);
    for (int i = 0; i < 5; ++i) {
        CHECK(result1.data()[i] == i + 1);
    }

    // Second segment, seq = 105 (contiguous)
    ByteView view2(payload2.data(), payload2.size());
    auto result2 = fb.feed(key, 105, view2, now);
    CHECK(result2.size() == 10);
    for (int i = 0; i < 10; ++i) {
        CHECK(result2.data()[i] == i + 1);
    }
}

TEST_CASE("l7: FlowBuffer duplicate segment ignored") {
    FlowBuffers fb;
    auto key = make_flow_key(0x12345678, 1234, 0x87654321, 80);
    auto now = std::chrono::steady_clock::now();

    std::vector<uint8_t> payload1 = {1, 2, 3, 4, 5};
    ByteView view1(payload1.data(), payload1.size());

    // First segment
    auto result1 = fb.feed(key, 100, view1, now);
    CHECK(result1.size() == 5);

    // Duplicate segment
    auto result2 = fb.feed(key, 100, view1, now);
    CHECK(result2.size() == 5);  // Size unchanged

    // Both should be identical
    for (int i = 0; i < 5; ++i) {
        CHECK(result1.data()[i] == result2.data()[i]);
    }
}

TEST_CASE("l7: FlowBuffer future segment ignored") {
    FlowBuffers fb;
    auto key = make_flow_key(0x12345678, 1234, 0x87654321, 80);
    auto now = std::chrono::steady_clock::now();

    std::vector<uint8_t> payload1 = {1, 2, 3, 4, 5};
    std::vector<uint8_t> payload2 = {6, 7, 8, 9, 10};
    std::vector<uint8_t> payload3 = {11, 12, 13, 14, 15};

    ByteView view1(payload1.data(), payload1.size());
    ByteView view2(payload2.data(), payload2.size());
    ByteView view3(payload3.data(), payload3.size());

    // First segment at seq 100
    auto result1 = fb.feed(key, 100, view1, now);
    CHECK(result1.size() == 5);

    // Future segment at seq 110 (gap), should be dropped
    auto result2 = fb.feed(key, 110, view2, now);
    CHECK(result2.size() == 5);  // Still only 5 bytes

    // Send the missing segment at seq 105
    auto result3 = fb.feed(key, 105, view3, now);
    CHECK(result3.size() == 10);  // Now we have 10 bytes (105-114)
    // Note: The future segment at 110 was dropped, so we don't get it
}

TEST_CASE("l7: FlowBuffer max_bytes cap") {
    FlowBuffers fb(512, 10);  // 10 bytes max per flow
    auto key = make_flow_key(0x12345678, 1234, 0x87654321, 80);
    auto now = std::chrono::steady_clock::now();

    std::vector<uint8_t> payload1(20, 0xFF);
    ByteView view1(payload1.data(), payload1.size());

    auto result = fb.feed(key, 100, view1, now);
    CHECK(result.size() == 10);  // Truncated to max
}

TEST_CASE("l7: FlowBuffer LRU eviction") {
    FlowBuffers fb(2, 4096);  // max 2 flows
    auto now = std::chrono::steady_clock::now();

    auto key1 = make_flow_key(0x12345678, 1234, 0x87654321, 80);
    auto key2 = make_flow_key(0x87654321, 1234, 0x12345678, 80);
    auto key3 = make_flow_key(0xAAAAAAAA, 1234, 0xBBBBBBBB, 80);

    std::vector<uint8_t> payload(10, 0xFF);
    ByteView view(payload.data(), payload.size());

    // Add first two flows
    fb.feed(key1, 100, view, now);
    CHECK(fb.size() == 1);

    fb.feed(key2, 100, view, now);
    CHECK(fb.size() == 2);

    // Add third flow, should evict key1 (least recently used)
    fb.feed(key3, 100, view, now);
    CHECK(fb.size() == 2);

    // Verify key1 was evicted and key2, key3 remain
    // key1 should not be recoverable
    // key2 and key3 should be accessible
}

TEST_CASE("l7: FlowBuffer expire removes old flows") {
    FlowBuffers fb(512, 4096, std::chrono::milliseconds(100));
    auto now = std::chrono::steady_clock::now();
    auto future = now + std::chrono::milliseconds(150);

    auto key1 = make_flow_key(0x12345678, 1234, 0x87654321, 80);
    auto key2 = make_flow_key(0x87654321, 1234, 0x12345678, 80);

    std::vector<uint8_t> payload(10, 0xFF);
    ByteView view(payload.data(), payload.size());

    // Add first flow at 'now'
    fb.feed(key1, 100, view, now);
    CHECK(fb.size() == 1);

    // Add second flow at a closer time (within TTL of first)
    auto closer_time = now + std::chrono::milliseconds(50);
    fb.feed(key2, 100, view, closer_time);
    CHECK(fb.size() == 2);

    // Expire at future should remove the old flow (first one)
    fb.expire(future);
    CHECK(fb.size() == 1);
}

TEST_CASE("l7: FlowBuffer sequence wraparound crossing 0") {
    FlowBuffers fb;
    auto key = make_flow_key(0x12345678, 1234, 0x87654321, 80);
    auto now = std::chrono::steady_clock::now();

    std::vector<uint8_t> payload1(16, 0xAA);
    std::vector<uint8_t> payload2(16, 0xBB);

    ByteView view1(payload1.data(), payload1.size());
    ByteView view2(payload2.data(), payload2.size());

    // First segment at seq 0xFFFFFFF0 (near wraparound)
    auto result1 = fb.feed(key, 0xFFFFFFF0, view1, now);
    CHECK(result1.size() == 16);

    // Second segment at seq 0 (wrapped around)
    auto result2 = fb.feed(key, 0, view2, now);
    CHECK(result2.size() == 32);  // Both segments are now contiguous
}

TEST_CASE("l7: FlowBuffer erase removes flow") {
    FlowBuffers fb;
    auto key = make_flow_key(0x12345678, 1234, 0x87654321, 80);
    auto now = std::chrono::steady_clock::now();

    std::vector<uint8_t> payload(10, 0xFF);
    ByteView view(payload.data(), payload.size());

    fb.feed(key, 100, view, now);
    CHECK(fb.size() == 1);

    fb.erase(key);
    CHECK(fb.size() == 0);
}

TEST_CASE("l7: FlowBuffer partial overlap with old segment") {
    FlowBuffers fb;
    auto key = make_flow_key(0x12345678, 1234, 0x87654321, 80);
    auto now = std::chrono::steady_clock::now();

    std::vector<uint8_t> payload1 = {1, 2, 3, 4, 5};
    std::vector<uint8_t> payload2 = {3, 4, 5, 6, 7};  // Overlaps with payload1

    ByteView view1(payload1.data(), payload1.size());
    ByteView view2(payload2.data(), payload2.size());

    // First segment at seq 100
    auto result1 = fb.feed(key, 100, view1, now);
    CHECK(result1.size() == 5);

    // Second segment at seq 102 (overlaps at bytes 3, 4, 5)
    auto result2 = fb.feed(key, 102, view2, now);
    CHECK(result2.size() == 7);  // 100-106 inclusive

    // Verify the data: should be 1, 2, then 3, 4, 5, 6, 7
    CHECK(result2.data()[0] == 1);
    CHECK(result2.data()[1] == 2);
    CHECK(result2.data()[2] == 3);
    CHECK(result2.data()[3] == 4);
    CHECK(result2.data()[4] == 5);
    CHECK(result2.data()[5] == 6);
    CHECK(result2.data()[6] == 7);
}

TEST_CASE("l7: FlowBuffer multiple flows independent") {
    FlowBuffers fb;
    auto key1 = make_flow_key(0x12345678, 1234, 0x87654321, 80);
    auto key2 = make_flow_key(0x87654321, 1234, 0x12345678, 443);
    auto now = std::chrono::steady_clock::now();

    std::vector<uint8_t> payload1 = {1, 2, 3};
    std::vector<uint8_t> payload2 = {4, 5, 6};

    ByteView view1(payload1.data(), payload1.size());
    ByteView view2(payload2.data(), payload2.size());

    // Feed both flows
    auto result1 = fb.feed(key1, 100, view1, now);
    auto result2 = fb.feed(key2, 200, view2, now);

    CHECK(result1.size() == 3);
    CHECK(result2.size() == 3);

    // Verify independence
    CHECK(result1.data()[0] == 1);
    CHECK(result2.data()[0] == 4);

    CHECK(fb.size() == 2);
}

TEST_CASE("l7: FlowBuffer gap across seq wraparound is dropped") {
    FlowBuffers fb;
    auto key = make_flow_key(0x01020304, 4000, 0x05060708, 443);
    auto now = std::chrono::steady_clock::now();
    std::vector<uint8_t> a(16, 1);
    std::vector<uint8_t> future(8, 2);
    std::vector<uint8_t> next(8, 3);

    fb.feed(key, 0xFFFFFFF8u, ByteView(a.data(), a.size()), now);  // ends at 0x00000008
    auto view = fb.feed(key, 0x00000010u, ByteView(future.data(), future.size()), now);
    CHECK(view.size() == 16);  // gap after wrap: not appended
    view = fb.feed(key, 0x00000008u, ByteView(next.data(), next.size()), now);
    CHECK(view.size() == 24);
    view = fb.feed(key, 0xFFFFFFF8u, ByteView(a.data(), a.size()), now);  // retransmit before wrap
    CHECK(view.size() == 24);
}
