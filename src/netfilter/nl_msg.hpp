#pragma once

#include "../util/byte_view.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string_view>
#include <vector>

namespace keen_pbr3::nfnl {

// Builds one or more netlink messages back-to-back into a single buffer.
class MsgBuilder {
public:
    explicit MsgBuilder(std::size_t reserve = 4096);

    void clear();

    // Starts nlmsghdr + nfgenmsg. type = (subsys << 8) | msg.
    // res_id is host order and is written big-endian.
    void begin(uint16_t type, uint16_t flags, uint32_t seq, uint8_t family, uint16_t res_id);
    // Finalizes nlmsg_len of the current message.
    void end();

    // NFNL_MSG_BATCH_BEGIN / NFNL_MSG_BATCH_END with res_id = NFNL_SUBSYS_NFTABLES.
    void batch_begin(uint32_t seq);
    void batch_end(uint32_t seq);

    void put(uint16_t type, const void* data, std::size_t len);
    void put_u8(uint16_t type, uint8_t v);
    void put_u16_be(uint16_t type, uint16_t v);
    void put_u32_be(uint16_t type, uint32_t v);
    void put_u64_be(uint16_t type, uint64_t v);
    void put_strz(uint16_t type, std::string_view s);

    // Returns a token for nest_end(). Sets NLA_F_NESTED on the attribute.
    std::size_t nest_begin(uint16_t type);
    void nest_end(std::size_t token);

    const uint8_t* data() const { return buf_.data(); }
    std::size_t size() const { return buf_.size(); }

private:
    void pad_to_align();

    std::vector<uint8_t> buf_;
    std::size_t msg_start_{0};
    std::size_t message_end_{0};
};

struct Attr {
    uint16_t type{0};
    ByteView payload;
};

// Iterates attributes inside `region`; returns false if malformed.
// Callback returns false to stop early.
bool for_each_attr(ByteView region, const std::function<bool(const Attr&)>& cb);

// Fills `table[type]` for every attribute with type < max_type_plus_one.
// Entries are NOT cleared first. Returns false if malformed.
bool parse_attrs(ByteView region, Attr* table, std::size_t max_type_plus_one);

// Accessors return 0 if the payload is too short.
uint16_t attr_u16_be(const Attr&);
uint32_t attr_u32_be(const Attr&);
uint64_t attr_u64_be(const Attr&);
uint8_t attr_u8(const Attr&);

struct MsgView {
    uint16_t type{0};
    uint16_t flags{0};
    uint32_t seq{0};
    uint32_t pid{0};
    uint8_t family{0};
    uint16_t res_id{0};   // host order
    ByteView attrs;       // after nfgenmsg (raw payload for control messages)
    ByteView raw;         // whole message
};

// Iterates netlink messages in a datagram. Returns false if malformed.
// For NLMSG_ERROR/DONE/NOOP (type < NLMSG_MIN_TYPE) `attrs` is the raw
// payload after nlmsghdr and family/res_id are 0.
bool for_each_msg(ByteView datagram, const std::function<bool(const MsgView&)>& cb);

// For NLMSG_ERROR: error_out is positive errno (0 == ACK); orig_seq_out is the
// seq of the original request (0 if the kernel did not echo the header).
bool parse_error(const MsgView& m, int& error_out, uint32_t& orig_seq_out);

// For NLMSG_DONE, reports a non-zero dump error payload or EINTR when the
// kernel marks the dump interrupted.  Returns false for other message types.
bool parse_done_error(const MsgView& m, int& error_out);

} // namespace keen_pbr3::nfnl
