#include "nl_msg.hpp"

#include <arpa/inet.h>
#include <endian.h>
#include <linux/netfilter/nfnetlink.h>
#include <linux/netlink.h>

#include <cstring>
#include <cerrno>

namespace keen_pbr3::nfnl {

namespace {

constexpr std::size_t kNlAttrHdr = 4;
constexpr std::size_t kNlMsgHdr = sizeof(nlmsghdr);

constexpr std::size_t align4(std::size_t n) { return (n + 3u) & ~static_cast<std::size_t>(3u); }

uint16_t rd16(const uint8_t* p) { uint16_t v; std::memcpy(&v, p, 2); return v; }
uint32_t rd32(const uint8_t* p) { uint32_t v; std::memcpy(&v, p, 4); return v; }

} // namespace

MsgBuilder::MsgBuilder(std::size_t reserve) {
    buf_.reserve(reserve);
}

void MsgBuilder::clear() {
    buf_.clear();
    msg_start_ = 0;
    message_end_ = 0;
}

void MsgBuilder::pad_to_align() {
    buf_.resize(align4(buf_.size()), 0);
}

void MsgBuilder::begin(uint16_t type, uint16_t flags, uint32_t seq, uint8_t family, uint16_t res_id) {
    msg_start_ = buf_.size();
    nlmsghdr nlh;
    std::memset(&nlh, 0, sizeof(nlh));
    nlh.nlmsg_len = static_cast<uint32_t>(kNlMsgHdr + sizeof(nfgenmsg));
    nlh.nlmsg_type = type;
    nlh.nlmsg_flags = flags;
    nlh.nlmsg_seq = seq;
    nlh.nlmsg_pid = 0;
    nfgenmsg nfg;
    nfg.nfgen_family = family;
    nfg.version = NFNETLINK_V0;
    nfg.res_id = htons(res_id);
    const auto* a = reinterpret_cast<const uint8_t*>(&nlh);
    const auto* b = reinterpret_cast<const uint8_t*>(&nfg);
    buf_.insert(buf_.end(), a, a + sizeof(nlh));
    buf_.insert(buf_.end(), b, b + sizeof(nfg));
    message_end_ = buf_.size();
}

void MsgBuilder::end() {
    const uint32_t len = static_cast<uint32_t>(message_end_ - msg_start_);
    std::memcpy(buf_.data() + msg_start_, &len, sizeof(len));
    // nlmsg_len excludes trailing alignment bytes.  The datagram still
    // carries them, and the next message starts at NLMSG_ALIGN(len).
    pad_to_align();
}

void MsgBuilder::batch_begin(uint32_t seq) {
    begin(NFNL_MSG_BATCH_BEGIN, NLM_F_REQUEST, seq, 0, NFNL_SUBSYS_NFTABLES);
    end();
}

void MsgBuilder::batch_end(uint32_t seq) {
    begin(NFNL_MSG_BATCH_END, NLM_F_REQUEST, seq, 0, NFNL_SUBSYS_NFTABLES);
    end();
}

void MsgBuilder::put(uint16_t type, const void* data, std::size_t len) {
    const std::size_t start = buf_.size();
    const uint16_t total = static_cast<uint16_t>(kNlAttrHdr + len);
    buf_.resize(start + kNlAttrHdr);
    std::memcpy(buf_.data() + start, &total, 2);
    std::memcpy(buf_.data() + start + 2, &type, 2);
    if (len != 0) {
        const auto* p = static_cast<const uint8_t*>(data);
        buf_.insert(buf_.end(), p, p + len);
    }
    message_end_ = buf_.size();
    pad_to_align();
}

void MsgBuilder::put_u8(uint16_t type, uint8_t v) { put(type, &v, 1); }

void MsgBuilder::put_u16_be(uint16_t type, uint16_t v) {
    const uint16_t be = htons(v);
    put(type, &be, sizeof(be));
}

void MsgBuilder::put_u32_be(uint16_t type, uint32_t v) {
    const uint32_t be = htonl(v);
    put(type, &be, sizeof(be));
}

void MsgBuilder::put_u64_be(uint16_t type, uint64_t v) {
    const uint64_t be = htobe64(v);
    put(type, &be, sizeof(be));
}

void MsgBuilder::put_strz(uint16_t type, std::string_view s) {
    const std::size_t start = buf_.size();
    const uint16_t total = static_cast<uint16_t>(kNlAttrHdr + s.size() + 1);
    buf_.resize(start + kNlAttrHdr);
    std::memcpy(buf_.data() + start, &total, 2);
    std::memcpy(buf_.data() + start + 2, &type, 2);
    buf_.insert(buf_.end(), s.begin(), s.end());
    buf_.push_back(0);
    message_end_ = buf_.size();
    pad_to_align();
}

std::size_t MsgBuilder::nest_begin(uint16_t type) {
    const std::size_t start = buf_.size();
    const uint16_t t = static_cast<uint16_t>(type | NLA_F_NESTED);
    buf_.resize(start + kNlAttrHdr);
    std::memcpy(buf_.data() + start + 2, &t, 2);
    return start;
}

void MsgBuilder::nest_end(std::size_t token) {
    pad_to_align();
    const uint16_t len = static_cast<uint16_t>(buf_.size() - token);
    std::memcpy(buf_.data() + token, &len, 2);
    message_end_ = buf_.size();
}

bool for_each_attr(ByteView region, const std::function<bool(const Attr&)>& cb) {
    std::size_t off = 0;
    const std::size_t total = region.size();
    while (off < total) {
        if (total - off < kNlAttrHdr) return false;
        const uint8_t* p = region.data() + off;
        const std::size_t len = rd16(p);
        const uint16_t type = rd16(p + 2);
        if (len < kNlAttrHdr || len > total - off) return false;
        Attr a;
        a.type = static_cast<uint16_t>(type & ~(NLA_F_NESTED | NLA_F_NET_BYTEORDER));
        a.payload = ByteView(p + kNlAttrHdr, len - kNlAttrHdr);
        if (!cb(a)) return true;
        off += align4(len);
    }
    return true;
}

bool parse_attrs(ByteView region, Attr* table, std::size_t max_type_plus_one) {
    return for_each_attr(region, [&](const Attr& a) {
        if (a.type < max_type_plus_one) table[a.type] = a;
        return true;
    });
}

uint16_t attr_u16_be(const Attr& a) {
    if (a.payload.size() < 2) return 0;
    return ntohs(rd16(a.payload.data()));
}

uint32_t attr_u32_be(const Attr& a) {
    if (a.payload.size() < 4) return 0;
    return ntohl(rd32(a.payload.data()));
}

uint64_t attr_u64_be(const Attr& a) {
    if (a.payload.size() < 8) return 0;
    uint64_t v;
    std::memcpy(&v, a.payload.data(), 8);
    return be64toh(v);
}

uint8_t attr_u8(const Attr& a) {
    return a.payload.size() < 1 ? 0 : a.payload.data()[0];
}

bool for_each_msg(ByteView datagram, const std::function<bool(const MsgView&)>& cb) {
    std::size_t off = 0;
    const std::size_t total = datagram.size();
    while (off < total) {
        if (total - off < kNlMsgHdr) return false;
        const uint8_t* p = datagram.data() + off;
        const std::size_t len = rd32(p);
        if (len < kNlMsgHdr || len > total - off) return false;

        MsgView m;
        m.type = rd16(p + 4);
        m.flags = rd16(p + 6);
        m.seq = rd32(p + 8);
        m.pid = rd32(p + 12);
        m.raw = ByteView(p, len);
        if (m.type < NLMSG_MIN_TYPE) {
            m.attrs = ByteView(p + kNlMsgHdr, len - kNlMsgHdr);
        } else {
            if (len < kNlMsgHdr + sizeof(nfgenmsg)) return false;
            m.family = p[kNlMsgHdr];
            m.res_id = ntohs(rd16(p + kNlMsgHdr + 2));
            const std::size_t hdrs = kNlMsgHdr + sizeof(nfgenmsg);
            m.attrs = ByteView(p + hdrs, len - hdrs);
        }
        if (!cb(m)) return true;
        off += align4(len);
    }
    return true;
}

bool parse_error(const MsgView& m, int& error_out, uint32_t& orig_seq_out) {
    if (m.type != NLMSG_ERROR) return false;
    if (m.attrs.size() < sizeof(int32_t)) return false;
    int32_t e;
    std::memcpy(&e, m.attrs.data(), sizeof(e));
    error_out = e < 0 ? -e : e;
    orig_seq_out = 0;
    if (m.attrs.size() >= sizeof(int32_t) + kNlMsgHdr) {
        orig_seq_out = rd32(m.attrs.data() + sizeof(int32_t) + 8);
    }
    return true;
}

bool parse_done_error(const MsgView& m, int& error_out) {
    if (m.type != NLMSG_DONE) return false;
    error_out = (m.flags & NLM_F_DUMP_INTR) != 0 ? EINTR : 0;
    if (m.attrs.size() >= sizeof(int32_t)) {
        int32_t value = 0;
        std::memcpy(&value, m.attrs.data(), sizeof(value));
        if (value != 0) error_out = value < 0 ? -value : value;
    }
    return true;
}

} // namespace keen_pbr3::nfnl
