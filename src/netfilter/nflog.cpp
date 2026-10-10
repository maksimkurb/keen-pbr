#include "nflog.hpp"
#include "uapi_compat.hpp"

#include <arpa/inet.h>
#include <poll.h>

#include <cerrno>
#include <cstring>
#include <limits>
#include <string>

namespace keen_pbr3::nfnl {
namespace {

constexpr uint16_t kConfig =
    static_cast<uint16_t>((NFNL_SUBSYS_ULOG << 8) | NFULNL_MSG_CONFIG);
constexpr uint16_t kPacket =
    static_cast<uint16_t>((NFNL_SUBSYS_ULOG << 8) | NFULNL_MSG_PACKET);
constexpr uint16_t kAckFlags = NLM_F_REQUEST | NLM_F_ACK;
constexpr std::size_t kRxBufSize = std::size_t{128} * 1024U;
constexpr int kConfigTimeoutMs = 2000;
constexpr int kMaxDatagramsPerReceive = 256;

void put_command(MsgBuilder& builder, uint8_t command) {
    nfulnl_msg_config_cmd cmd{};
    cmd.command = command;
    builder.put(NFULA_CFG_CMD, &cmd, sizeof(cmd));
}

} // namespace

void build_nflog_bind(MsgBuilder& builder, uint32_t seq, uint16_t group) {
    builder.begin(kConfig, kAckFlags, seq, AF_UNSPEC, group);
    put_command(builder, NFULNL_CFG_CMD_BIND);
    builder.end();
}

void build_nflog_unbind(MsgBuilder& builder, uint32_t seq, uint16_t group) {
    builder.begin(kConfig, kAckFlags, seq, AF_UNSPEC, group);
    put_command(builder, NFULNL_CFG_CMD_UNBIND);
    builder.end();
}

void build_nflog_config(MsgBuilder& builder, uint32_t seq,
                        const NfLogOptions& options) {
    builder.begin(kConfig, kAckFlags, seq, AF_UNSPEC, options.group);
    nfulnl_msg_config_mode mode{};
    mode.copy_range = htonl(options.copy_range);
    mode.copy_mode = NFULNL_COPY_PACKET;
    builder.put(NFULA_CFG_MODE, &mode, sizeof(mode));
    builder.put_u32_be(NFULA_CFG_QTHRESH, options.queue_threshold);
    builder.put_u32_be(NFULA_CFG_TIMEOUT, options.timeout_hundredths);
    builder.end();
}

bool parse_nflog_packet(const MsgView& message, LoggedPacket& packet) {
    if (message.type != kPacket) return false;
    Attr attrs[NFULA_MAX + 1]{};
    if (!parse_attrs(message.attrs, attrs, NFULA_MAX + 1)) return false;
    const Attr& header = attrs[NFULA_PACKET_HDR];
    if (header.payload.size() < sizeof(nfulnl_msg_packet_hdr)) return false;
    const Attr& payload = attrs[NFULA_PAYLOAD];
    if (payload.payload.data() == nullptr) return false;

    uint16_t protocol = 0;
    std::memcpy(&protocol, header.payload.data(), sizeof(protocol));
    packet.hw_protocol = ntohs(protocol);
    packet.hook = header.payload.data()[2];
    packet.mark = attr_u32_be(attrs[NFULA_MARK]);
    packet.indev = attr_u32_be(attrs[NFULA_IFINDEX_INDEV]);
    packet.outdev = attr_u32_be(attrs[NFULA_IFINDEX_OUTDEV]);
    packet.payload = payload.payload;
    packet.conntrack_attrs = attrs[uapi::kNfulaCt].payload;
    return true;
}

NfLog::NfLog(const NfLogOptions& options)
    : options_(options), socket_(options.rcvbuf_bytes, true), tx_(4096) {
    rx_.resize(kRxBufSize);
    const uint32_t bind_seq = socket_.next_seq();
    build_nflog_bind(tx_, bind_seq, options_.group);
    const uint32_t config_seq = socket_.next_seq();
    build_nflog_config(tx_, config_seq, options_);
    int bind_error = std::numeric_limits<int>::min();
    int config_error = std::numeric_limits<int>::min();
    const int rc = socket_.transact(
        tx_.data(), tx_.size(), bind_seq, config_seq, kConfigTimeoutMs,
        [](const MsgView&) {},
        [&](uint32_t seq, int error) {
            if (seq == bind_seq) bind_error = error;
            if (seq == config_seq) config_error = error;
        });
    const std::string prefix = "nflog group " + std::to_string(options_.group) + ": ";
    if (rc != 0) {
        throw NlSocketError(prefix + "configuration failed: " + std::strerror(rc), rc);
    }
    if (bind_error != 0) {
        if (bind_error == std::numeric_limits<int>::min()) {
            throw NlSocketError(prefix + "bind acknowledgement timed out", ETIMEDOUT);
        }
        throw NlSocketError(prefix + "bind failed: " + std::strerror(bind_error), bind_error);
    }
    if (config_error != 0) {
        if (config_error == std::numeric_limits<int>::min()) {
            throw NlSocketError(prefix + "configuration acknowledgement timed out", ETIMEDOUT);
        }
        throw NlSocketError(prefix + "set mode failed: " + std::strerror(config_error), config_error);
    }
    bound_ = true;
    tx_.clear();
}

NfLog::~NfLog() {
    if (!bound_ || socket_.fd() < 0) return;
    tx_.clear();
    build_nflog_unbind(tx_, socket_.next_seq(), options_.group);
    int error = 0;
    (void)socket_.send(tx_.data(), tx_.size(), error);
}

int NfLog::receive(const std::function<void(const LoggedPacket&)>& on_packet) {
    int delivered = 0;
    for (int datagram = 0; datagram < kMaxDatagramsPerReceive; ++datagram) {
        int error = 0;
        const ssize_t length = socket_.recv(rx_, error);
        if (length < 0) {
            if (error == EAGAIN || error == EWOULDBLOCK) break;
            // NFLOG is passive, so lost records only cost learning: ENOBUFS
            // (kernel dropped records; NETLINK_NO_ENOBUFS should already
            // suppress it) and EMSGSIZE (recv() grew the buffer) are counted
            // as overruns and the listener keeps going.
            if (error == ENOBUFS || error == EMSGSIZE) {
                ++overruns_;
                last_errno_ = error;
                continue;
            }
            ++overruns_;
            last_errno_ = error;
            return -1;
        }
        const bool valid = for_each_msg(
            ByteView(rx_.data(), static_cast<std::size_t>(length)),
            [&](const MsgView& message) {
                if (message.type != kPacket) return true;
                LoggedPacket packet;
                if (!parse_nflog_packet(message, packet)) {
                    // NFLOG is passive: one corrupt record must not tear down
                    // the listener or make later L7 records disappear.
                    ++malformed_packets_;
                    last_errno_ = EPROTO;
                    return true;
                }
                if (on_packet) on_packet(packet);
                ++delivered;
                return true;
            });
        if (!valid) {
            if (last_errno_ == 0) last_errno_ = EPROTO;
            return -1;
        }
    }
    return delivered;
}

#ifdef KEEN_PBR3_TESTING
NfLog::NfLog(ForTest, int rcvbuf_bytes)
    : options_(), socket_(rcvbuf_bytes, true), tx_(4096) {
    rx_.resize(std::size_t{128} * 1024U);
    bound_ = false;
}

NfLog nflog_for_test(int rcvbuf_bytes) {
    return NfLog(NfLog::ForTest::tag, rcvbuf_bytes);
}
#endif

} // namespace keen_pbr3::nfnl
