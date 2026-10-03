#include <doctest/doctest.h>

#include "../src/netfilter/nl_msg.hpp"

#include <linux/netfilter/nfnetlink.h>
#include <linux/netfilter/nfnetlink_queue.h>
#include <linux/netlink.h>
#include <sys/socket.h>

#include <cerrno>
#include <cstring>
#include <vector>

using namespace keen_pbr3;
using namespace keen_pbr3::nfnl;

#if !defined(__BYTE_ORDER__) || __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "test_nfnetlink golden bytes assume a little-endian host"
#endif

namespace {

std::vector<uint8_t> to_vec(const MsgBuilder& b) {
    return std::vector<uint8_t>(b.data(), b.data() + b.size());
}

ByteView view(const std::vector<uint8_t>& v) { return ByteView(v.data(), v.size()); }

} // namespace

TEST_CASE("nfnetlink: single message golden bytes") {
    MsgBuilder b;
    const uint16_t type = (NFNL_SUBSYS_QUEUE << 8) | NFQNL_MSG_CONFIG;
    b.begin(type, NLM_F_REQUEST | NLM_F_ACK, 7, AF_UNSPEC, 9053);
    const uint8_t cmd[4] = {1, 0, 0, 2};
    b.put(1, cmd, sizeof(cmd));
    b.put_u32_be(2, 0xFFFF);
    b.end();

    const std::vector<uint8_t> expected = {
        0x24, 0x00, 0x00, 0x00,  // nlmsg_len = 36
        0x02, 0x03,              // type = 0x0302
        0x05, 0x00,              // flags = REQUEST|ACK
        0x07, 0x00, 0x00, 0x00,  // seq
        0x00, 0x00, 0x00, 0x00,  // pid
        0x00, 0x00, 0x23, 0x5D,  // nfgenmsg: family, version, res_id (BE 9053)
        0x08, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x02,
        0x08, 0x00, 0x02, 0x00, 0x00, 0x00, 0xFF, 0xFF,
    };
    CHECK(to_vec(b) == expected);
}

TEST_CASE("nfnetlink: attribute padding") {
    MsgBuilder b;
    b.begin(0x0100, NLM_F_REQUEST, 1, AF_UNSPEC, 0);
    const uint8_t five[5] = {1, 2, 3, 4, 5};
    b.put(3, five, sizeof(five));
    b.put_u8(4, 0xAA);
    b.end();

    const std::vector<uint8_t> expected = {
        0x28, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00,
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x09, 0x00, 0x03, 0x00, 1, 2, 3, 4, 5, 0, 0, 0,  // len 9, padded to 12
        0x05, 0x00, 0x04, 0x00, 0xAA, 0, 0, 0,
    };
    CHECK(to_vec(b) == expected);
}

TEST_CASE("nfnetlink: nested attribute") {
    MsgBuilder b;
    b.begin(0x0100, 0, 0, AF_UNSPEC, 0);
    const std::size_t tok = b.nest_begin(1);
    b.put_u32_be(2, 0x01020304);
    b.nest_end(tok);
    b.end();

    const std::vector<uint8_t> expected = {
        0x20, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x0C, 0x00, 0x01, 0x80,  // len 12, type 1 | NLA_F_NESTED
        0x08, 0x00, 0x02, 0x00, 0x01, 0x02, 0x03, 0x04,
    };
    CHECK(to_vec(b) == expected);
}

TEST_CASE("nfnetlink: put_strz includes NUL and padding") {
    MsgBuilder b;
    b.begin(0x0100, 0, 0, AF_UNSPEC, 0);
    b.put_strz(1, "kpbr4d_x");
    b.end();

    const std::vector<uint8_t> expected = {
        0x24, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x0D, 0x00, 0x01, 0x00,  // len 4 + 9
        'k', 'p', 'b', 'r', '4', 'd', '_', 'x', 0x00, 0, 0, 0,
    };
    CHECK(to_vec(b) == expected);
}

TEST_CASE("nfnetlink: batch begin/end") {
    MsgBuilder b;
    b.batch_begin(1);
    b.begin(0x0A00, NLM_F_REQUEST, 2, 1, 0);
    b.end();
    b.batch_end(3);

    const std::vector<uint8_t> expected = {
        // batch begin
        0x14, 0x00, 0x00, 0x00, 0x10, 0x00, 0x01, 0x00,
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x0A,
        // inner message
        0x14, 0x00, 0x00, 0x00, 0x00, 0x0A, 0x01, 0x00,
        0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x01, 0x00, 0x00, 0x00,
        // batch end
        0x14, 0x00, 0x00, 0x00, 0x11, 0x00, 0x01, 0x00,
        0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x0A,
    };
    CHECK(to_vec(b) == expected);

    std::vector<MsgView> msgs;
    CHECK(for_each_msg(ByteView(b.data(), b.size()), [&](const MsgView& m) {
        msgs.push_back(m);
        return true;
    }));
    REQUIRE(msgs.size() == 3);
    CHECK(msgs[0].type == NFNL_MSG_BATCH_BEGIN);
    CHECK(msgs[0].res_id == NFNL_SUBSYS_NFTABLES);
    CHECK(msgs[2].type == NFNL_MSG_BATCH_END);
    CHECK(msgs[2].res_id == NFNL_SUBSYS_NFTABLES);
    CHECK(msgs[1].seq == 2);
}

TEST_CASE("nfnetlink: round trip parse") {
    MsgBuilder b;
    b.begin(0x0301, NLM_F_REQUEST, 11, 2, 77);
    b.put_u16_be(1, 0xBEEF);
    const std::size_t tok = b.nest_begin(2);
    b.put_u32_be(1, 0xCAFEBABE);
    b.put_u64_be(2, 0x0102030405060708ULL);
    b.nest_end(tok);
    b.put_strz(3, "abc");
    b.end();

    int msgs = 0;
    const bool ok = for_each_msg(ByteView(b.data(), b.size()), [&](const MsgView& m) {
        ++msgs;
        CHECK(m.type == 0x0301);
        CHECK(m.seq == 11);
        CHECK(m.family == 2);
        CHECK(m.res_id == 77);

        Attr table[4];
        CHECK(parse_attrs(m.attrs, table, 4));
        CHECK(attr_u16_be(table[1]) == 0xBEEF);
        CHECK(table[3].payload.size() == 4);
        CHECK(std::memcmp(table[3].payload.data(), "abc", 4) == 0);

        int inner = 0;
        CHECK(for_each_attr(table[2].payload, [&](const Attr& a) {
            ++inner;
            if (a.type == 1) CHECK(attr_u32_be(a) == 0xCAFEBABE);
            if (a.type == 2) CHECK(attr_u64_be(a) == 0x0102030405060708ULL);
            return true;
        }));
        CHECK(inner == 2);
        return true;
    });
    CHECK(ok);
    CHECK(msgs == 1);
}

TEST_CASE("nfnetlink: malformed input is rejected") {
    SUBCASE("attribute len < 4") {
        const std::vector<uint8_t> r = {0x02, 0x00, 0x01, 0x00, 0, 0, 0, 0};
        CHECK_FALSE(for_each_attr(view(r), [](const Attr&) { return true; }));
    }
    SUBCASE("attribute beyond region") {
        const std::vector<uint8_t> r = {0x10, 0x00, 0x01, 0x00, 0, 0, 0, 0};
        Attr table[2];
        CHECK_FALSE(parse_attrs(view(r), table, 2));
    }
    SUBCASE("truncated attribute header") {
        const std::vector<uint8_t> r = {0x08, 0x00, 0x01};
        CHECK_FALSE(for_each_attr(view(r), [](const Attr&) { return true; }));
    }
    SUBCASE("nlmsg_len beyond datagram") {
        const std::vector<uint8_t> d = {
            0x40, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00};
        CHECK_FALSE(for_each_msg(view(d), [](const MsgView&) { return true; }));
    }
    SUBCASE("nlmsg_len below header") {
        const std::vector<uint8_t> d = {
            0x08, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00};
        CHECK_FALSE(for_each_msg(view(d), [](const MsgView&) { return true; }));
    }
    SUBCASE("subsystem message without nfgenmsg") {
        const std::vector<uint8_t> d = {
            0x10, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        CHECK_FALSE(for_each_msg(view(d), [](const MsgView&) { return true; }));
    }
    SUBCASE("datagram shorter than nlmsghdr") {
        const std::vector<uint8_t> d = {0x10, 0x00, 0x00};
        CHECK_FALSE(for_each_msg(view(d), [](const MsgView&) { return true; }));
    }
}

TEST_CASE("nfnetlink: parse_error") {
    auto make = [](int32_t err) {
        std::vector<uint8_t> d(36, 0);
        const uint32_t len = 36;
        std::memcpy(d.data(), &len, 4);
        d[4] = NLMSG_ERROR;  // type
        std::memcpy(d.data() + 16, &err, 4);
        // embedded original nlmsghdr at 20..35, seq at +8
        const uint32_t orig_len = 16;
        std::memcpy(d.data() + 20, &orig_len, 4);
        const uint32_t orig_seq = 42;
        std::memcpy(d.data() + 28, &orig_seq, 4);
        return d;
    };

    SUBCASE("EEXIST") {
        const auto d = make(-EEXIST);
        int calls = 0;
        CHECK(for_each_msg(view(d), [&](const MsgView& m) {
            ++calls;
            int e = -1;
            uint32_t seq = 0;
            CHECK(parse_error(m, e, seq));
            CHECK(e == EEXIST);
            CHECK(seq == 42);
            return true;
        }));
        CHECK(calls == 1);
    }
    SUBCASE("ACK") {
        const auto d = make(0);
        CHECK(for_each_msg(view(d), [&](const MsgView& m) {
            int e = -1;
            uint32_t seq = 0;
            CHECK(parse_error(m, e, seq));
            CHECK(e == 0);
            CHECK(seq == 42);
            return true;
        }));
    }
}
