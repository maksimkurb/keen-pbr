#include <doctest/doctest.h>

#include "../src/netfilter/nfqueue.hpp"

#include <arpa/inet.h>
#include <linux/if_ether.h>
#include <linux/netfilter.h>
#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter/nfnetlink_queue.h>
#include <linux/netlink.h>

#include <array>
#include <cstring>
#include <vector>

using namespace keen_pbr3;
using namespace keen_pbr3::nfnl;

namespace {

MsgView one_message(const MsgBuilder& builder) {
    MsgView result;
    REQUIRE(for_each_msg(ByteView(builder.data(), builder.size()), [&](const MsgView& message) {
        result = message;
        return false;
    }));
    return result;
}

MsgBuilder packet_message(uint32_t id, std::size_t captured_payload,
                          std::size_t declared_payload) {
    MsgBuilder builder;
    builder.begin(static_cast<uint16_t>((NFNL_SUBSYS_QUEUE << 8) | NFQNL_MSG_PACKET),
                  NLM_F_REQUEST, 1, AF_UNSPEC, 9053);
    nfqnl_msg_packet_hdr header{};
    header.packet_id = htonl(id);
    header.hw_protocol = htons(ETH_P_IP);
    header.hook = NF_INET_FORWARD;
    builder.put(NFQA_PACKET_HDR, &header, sizeof(header));
    builder.put_u32_be(NFQA_MARK, 0x12345678);
    builder.put_u32_be(13, static_cast<uint32_t>(declared_payload));
    std::vector<uint8_t> payload(captured_payload, 0);
    if (captured_payload >= 4) {
        payload[0] = 0x45;
        payload[2] = static_cast<uint8_t>(declared_payload >> 8);
        payload[3] = static_cast<uint8_t>(declared_payload);
    }
    builder.put(NFQA_PAYLOAD, payload.data(), payload.size());
    builder.end();
    return builder;
}

} // namespace

TEST_CASE("nfqueue: parse packet attributes and cap length") {
    const auto builder = packet_message(0x10203040, 20, 20);
    const auto message = one_message(builder);
    QueuedPacket packet;
    REQUIRE(parse_nfqueue_packet(message, packet));
    CHECK(packet.packet_id == 0x10203040);
    CHECK(packet.hw_protocol == ETH_P_IP);
    CHECK(packet.hook == NF_INET_FORWARD);
    CHECK(packet.mark == 0x12345678);
    CHECK(packet.payload.size() == 20);
    CHECK_FALSE(packet.truncated);
}

TEST_CASE("nfqueue: truncated payload is reported without changing bytes") {
    const auto builder = packet_message(7, 12, 40);
    const auto message = one_message(builder);
    QueuedPacket packet;
    REQUIRE(parse_nfqueue_packet(message, packet));
    CHECK(packet.truncated);
    CHECK(packet.payload.size() == 12);
    CHECK(packet.payload[0] == 0x45);
}

TEST_CASE("nfqueue: malformed packet cannot be parsed") {
    MsgBuilder builder;
    builder.begin(static_cast<uint16_t>((NFNL_SUBSYS_QUEUE << 8) | NFQNL_MSG_PACKET),
                  NLM_F_REQUEST, 1, AF_UNSPEC, 9053);
    const uint8_t short_header[] = {1, 2, 3};
    builder.put(NFQA_PACKET_HDR, short_header, sizeof(short_header));
    builder.end();
    const auto message = one_message(builder);
    QueuedPacket packet;
    CHECK_FALSE(parse_nfqueue_packet(message, packet));
}

TEST_CASE("nfqueue: verdict replacement preserves packet id and payload") {
    MsgBuilder builder;
    const uint8_t replacement[] = {1, 2, 3, 4};
    build_nfqueue_verdict(builder, 9053, 42, NF_ACCEPT, replacement, sizeof(replacement));
    const auto message = one_message(builder);
    CHECK(message.type == ((NFNL_SUBSYS_QUEUE << 8) | NFQNL_MSG_VERDICT));
    Attr table[NFQA_MAX + 1];
    REQUIRE(parse_attrs(message.attrs, table, NFQA_MAX + 1));
    REQUIRE(table[NFQA_VERDICT_HDR].payload.size() == sizeof(nfqnl_msg_verdict_hdr));
    nfqnl_msg_verdict_hdr header{};
    std::memcpy(&header, table[NFQA_VERDICT_HDR].payload.data(), sizeof(header));
    CHECK(ntohl(header.id) == 42);
    CHECK(ntohl(header.verdict) == NF_ACCEPT);
    CHECK(table[NFQA_PAYLOAD].payload.size() == sizeof(replacement));
    CHECK(std::memcmp(table[NFQA_PAYLOAD].payload.data(), replacement,
                      sizeof(replacement)) == 0);
}

TEST_CASE("nfqueue: odd payload keeps netlink length before trailing padding") {
    const std::array<uint8_t, 35> replacement{};
    MsgBuilder builder;
    build_nfqueue_verdict(builder, 9053, 42, NF_ACCEPT, replacement.data(), replacement.size());
    const auto message = one_message(builder);
    CHECK(message.raw.size() == 71);
    CHECK(builder.size() == 72);
}

TEST_CASE("nfqueue: fail-open params set only supported flag") {
    NfQueueOptions options;
    options.queue_num = 9053;
    options.copy_range = 2048;
    options.queue_maxlen = 1024;
    options.fail_open = true;
    MsgBuilder builder;
    build_nfqueue_params(builder, 5, options);
    const auto message = one_message(builder);
    Attr table[NFQA_MAX + 1];
    REQUIRE(parse_attrs(message.attrs, table, NFQA_MAX + 1));
    CHECK(attr_u32_be(table[NFQA_CFG_QUEUE_MAXLEN]) == 1024);
    CHECK(attr_u32_be(table[NFQA_CFG_FLAGS]) == NFQA_CFG_F_FAIL_OPEN);
    CHECK(attr_u32_be(table[NFQA_CFG_MASK]) == NFQA_CFG_F_FAIL_OPEN);
}

TEST_CASE("nfqueue: replacement capability owner decision is conservative") {
    constexpr uint64_t initial_user = 0xEFFFFFFDU;
    constexpr uint64_t initial_net = 0xEFFFFFF9U;
    CHECK(nfqueue_initial_owner_decision_for_test(7, initial_user, 0));
    CHECK_FALSE(nfqueue_initial_owner_decision_for_test(7, 123, initial_net));
    CHECK(nfqueue_initial_owner_decision_for_test(-1, 0, initial_net));
    CHECK_FALSE(nfqueue_initial_owner_decision_for_test(-1, 0, 123));
    CHECK_FALSE(nfqueue_initial_owner_decision_for_test(-1, 0, 0));
}
