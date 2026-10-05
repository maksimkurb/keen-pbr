#include "nfqueue.hpp"
#include "uapi_compat.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <limits>
#include <string>

namespace keen_pbr3::nfnl {

namespace {

constexpr uint16_t kNfqaCapLen = uapi::kNfqaCapLen;

constexpr uint16_t kMsgConfig = (NFNL_SUBSYS_QUEUE << 8) | NFQNL_MSG_CONFIG;
constexpr uint16_t kMsgVerdict = (NFNL_SUBSYS_QUEUE << 8) | NFQNL_MSG_VERDICT;
constexpr uint16_t kMsgVerdictBatch = (NFNL_SUBSYS_QUEUE << 8) | NFQNL_MSG_VERDICT_BATCH;
constexpr uint16_t kMsgPacket = (NFNL_SUBSYS_QUEUE << 8) | NFQNL_MSG_PACKET;

constexpr uint16_t kReqAck = NLM_F_REQUEST | NLM_F_ACK;
constexpr std::size_t kRxBufSize = std::size_t{128} * 1024U;  // > max netlink skb for a 64 KiB copy range
constexpr int kMaxDatagramsPerReceive = 256;
constexpr int kConfigTimeoutMs = 2000;
constexpr ino_t kInitialUserNamespaceInode = static_cast<ino_t>(0xEFFFFFFDU);
constexpr ino_t kInitialNetworkNamespaceInode = static_cast<ino_t>(0xEFFFFFF9U);

void put_cfg_cmd(MsgBuilder& b, uint8_t command, uint16_t pf) {
    nfqnl_msg_config_cmd cmd;
    std::memset(&cmd, 0, sizeof(cmd));
    cmd.command = command;
    cmd.pf = htons(pf);
    b.put(NFQA_CFG_CMD, &cmd, sizeof(cmd));
}

// Expected length of the L3 packet according to its own header, 0 if unknown.
std::size_t ip_declared_len(ByteView p) {
    if (p.size() < 1) return 0;
    const uint8_t ver = p.data()[0] >> 4;
    if (ver == 4 && p.size() >= 4) {
        return (static_cast<std::size_t>(p.data()[2]) << 8) | p.data()[3];
    }
    if (ver == 6 && p.size() >= 6) {
        return 40 + ((static_cast<std::size_t>(p.data()[4]) << 8) | p.data()[5]);
    }
    return 0;
}

void build_verdict_common(MsgBuilder& b, uint16_t type, uint32_t flags, uint32_t seq,
                          uint16_t queue_num, uint32_t id, uint32_t verdict,
                          const uint8_t* payload, std::size_t len) {
    b.begin(type, flags, seq, AF_UNSPEC, queue_num);
    nfqnl_msg_verdict_hdr vh;
    vh.verdict = htonl(verdict);
    vh.id = htonl(id);
    b.put(NFQA_VERDICT_HDR, &vh, sizeof(vh));
    if (payload != nullptr && len != 0) b.put(NFQA_PAYLOAD, payload, len);
    b.end();
}

bool packet_id_from_attrs(ByteView attrs, uint32_t& id) {
    bool found = false;
    const bool valid = for_each_attr(attrs, [&](const Attr& attr) {
        if (attr.type == NFQA_PACKET_HDR && attr.payload.size() >= sizeof(nfqnl_msg_packet_hdr)) {
            uint32_t id_be = 0;
            std::memcpy(&id_be, attr.payload.data(), sizeof(id_be));
            id = ntohl(id_be);
            found = true;
            return false;
        }
        return true;
    });
    // A malformed attribute after a valid packet header must not hide the
    // recoverable ID; the caller can still issue a fail-open verdict.  If the
    // header itself is absent or truncated, no safe verdict target exists.
    (void)valid;
    return found;
}

ReplacementCapability initial_owner_decision(int owner_fd, ino_t owner_inode, ino_t netns_inode) {
    if (owner_fd >= 0) {
        return owner_inode == kInitialUserNamespaceInode ? ReplacementCapability::supported
                                                         : ReplacementCapability::unsupported;
    }
    // No NS_GET_USERNS (kernel < 4.9): only the initial network namespace is
    // known to be owned by the initial user namespace.
    return netns_inode == kInitialNetworkNamespaceInode ? ReplacementCapability::supported
                                                        : ReplacementCapability::unknown;
}

ReplacementCapability detect_replacement_capability() {
    const int netns = ::open("/proc/self/ns/net", O_RDONLY | O_CLOEXEC);
    if (netns < 0) return ReplacementCapability::unknown;

    struct stat netns_stat{};
    const bool have_netns_stat = ::fstat(netns, &netns_stat) == 0;
    const int owner = ::ioctl(netns, NS_GET_USERNS);
    if (owner >= 0) {
        struct stat owner_stat{};
        const bool have_owner_stat = ::fstat(owner, &owner_stat) == 0;
        ::close(owner);
        ::close(netns);
        return initial_owner_decision(0, have_owner_stat ? owner_stat.st_ino : 0, 0);
    }
    ::close(netns);
    return initial_owner_decision(-1, 0, have_netns_stat ? netns_stat.st_ino : 0);
}

} // namespace

#ifdef KEEN_PBR3_TESTING
bool nfqueue_initial_owner_decision_for_test(int owner_fd, uint64_t owner_inode,
                                             uint64_t netns_inode) {
    return initial_owner_decision(owner_fd, static_cast<ino_t>(owner_inode),
                                  static_cast<ino_t>(netns_inode)) ==
           ReplacementCapability::supported;
}

ReplacementCapability nfqueue_replacement_decision_for_test(int owner_fd, uint64_t owner_inode,
                                                            uint64_t netns_inode) {
    return initial_owner_decision(owner_fd, static_cast<ino_t>(owner_inode),
                                  static_cast<ino_t>(netns_inode));
}
#endif

void build_nfqueue_pf_cmd(MsgBuilder& b, uint32_t seq, uint8_t pf_cmd, uint8_t pf) {
    b.begin(kMsgConfig, kReqAck, seq, pf, 0);
    put_cfg_cmd(b, pf_cmd, pf);
    b.end();
}

void build_nfqueue_bind(MsgBuilder& b, uint32_t seq, uint16_t queue_num) {
    b.begin(kMsgConfig, kReqAck, seq, AF_UNSPEC, queue_num);
    put_cfg_cmd(b, NFQNL_CFG_CMD_BIND, 0);
    b.end();
}

void build_nfqueue_unbind(MsgBuilder& b, uint32_t seq, uint16_t queue_num) {
    b.begin(kMsgConfig, kReqAck, seq, AF_UNSPEC, queue_num);
    put_cfg_cmd(b, NFQNL_CFG_CMD_UNBIND, 0);
    b.end();
}

void build_nfqueue_params(MsgBuilder& b, uint32_t seq, const NfQueueOptions& o,
                          bool include_flags) {
    b.begin(kMsgConfig, kReqAck, seq, AF_UNSPEC, o.queue_num);
    nfqnl_msg_config_params params;
    std::memset(&params, 0, sizeof(params));
    params.copy_range = htonl(o.copy_range);
    params.copy_mode = NFQNL_COPY_PACKET;
    b.put(NFQA_CFG_PARAMS, &params, sizeof(params));
    b.put_u32_be(NFQA_CFG_QUEUE_MAXLEN, o.queue_maxlen);
    if (include_flags) {
        b.put_u32_be(uapi::kNfqaCfgMask, uapi::kNfqaCfgFFailOpen);
        b.put_u32_be(uapi::kNfqaCfgFlags, o.fail_open ? uapi::kNfqaCfgFFailOpen : 0u);
    }
    b.end();
}

void build_nfqueue_verdict(MsgBuilder& b, uint16_t queue_num, uint32_t id, uint32_t verdict,
                           const uint8_t* payload, std::size_t len) {
    build_verdict_common(b, kMsgVerdict, NLM_F_REQUEST, 0, queue_num, id, verdict, payload,
                         len);
}

void build_nfqueue_verdict_batch(MsgBuilder& b, uint16_t queue_num, uint32_t max_id,
                                 uint32_t verdict) {
    build_verdict_common(b, kMsgVerdictBatch, NLM_F_REQUEST, 0, queue_num, max_id, verdict,
                         nullptr, 0);
}

bool parse_nfqueue_packet(const MsgView& m, QueuedPacket& out) {
    if (m.type != kMsgPacket) return false;
    Attr table[uapi::kNfqaAttrCount];
    if (!parse_attrs(m.attrs, table, uapi::kNfqaAttrCount)) return false;

    const Attr& hdr = table[NFQA_PACKET_HDR];
    if (hdr.payload.size() < sizeof(nfqnl_msg_packet_hdr)) return false;
    const Attr& pl = table[NFQA_PAYLOAD];
    if (pl.payload.data() == nullptr) return false;

    const uint8_t* h = hdr.payload.data();
    uint32_t id_be = 0;
    uint16_t proto_be = 0;
    std::memcpy(&id_be, h, sizeof(id_be));
    std::memcpy(&proto_be, h + 4, sizeof(proto_be));
    out.packet_id = ntohl(id_be);
    out.hw_protocol = ntohs(proto_be);
    out.hook = h[6];
    out.mark = attr_u32_be(table[NFQA_MARK]);
    out.payload = pl.payload;

    bool truncated = false;
    const Attr& cap = table[kNfqaCapLen];
    if (cap.payload.size() >= 4 && attr_u32_be(cap) > pl.payload.size()) truncated = true;
    if (ip_declared_len(pl.payload) > pl.payload.size()) truncated = true;
    out.truncated = truncated;
    const uint32_t info = attr_u32_be(table[uapi::kNfqaSkbInfo]);
    out.gso = (info & uapi::kNfqaSkbGso) != 0;
    out.csum_not_ready = (info & uapi::kNfqaSkbCsumNotReady) != 0;
    return true;
}

NfQueue::NfQueue(const NfQueueOptions& opt)
    : opt_(opt), sock_(opt.rcvbuf_bytes, true), tx_(kRxBufSize + 512) {
    rx_.resize(kRxBufSize);

    // Do not issue PF_UNBIND/PF_BIND here: those commands are global to the
    // protocol family and can steal another daemon's handler on old kernels.
    // Queue-local bind is sufficient on supported nfnetlink_queue versions.
    const uint32_t bind_seq = sock_.next_seq();
    build_nfqueue_bind(tx_, bind_seq, opt_.queue_num);
    const uint32_t params_seq = sock_.next_seq();
    build_nfqueue_params(tx_, params_seq, opt_, /*include_flags=*/false);

    int bind_err = std::numeric_limits<int>::min();
    int params_err = std::numeric_limits<int>::min();
    bool startup_malformed = false;
    int startup_verdict_err = 0;
    const int rc = sock_.transact(
        tx_.data(), tx_.size(), bind_seq, params_seq, kConfigTimeoutMs,
        [&](const MsgView& msg) {
            if (msg.type != kMsgPacket) return;
            uint32_t id = 0;
            if (!packet_id_from_attrs(msg.attrs, id)) {
                startup_malformed = true;
                return;
            }
            // The queue has no runtime callback yet.  Release each packet as
            // soon as it is observed, rather than waiting for the bind/params
            // transaction (which has its own deadline).  Use a separate
            // builder: tx_ is the request buffer still owned by transact().
            MsgBuilder verdict_msg;
            build_nfqueue_verdict(verdict_msg, opt_.queue_num, id, NF_ACCEPT,
                                  nullptr, 0);
            int err = 0;
            if (!sock_.send(verdict_msg.data(), verdict_msg.size(), err) &&
                startup_verdict_err == 0) {
                startup_verdict_err = err;
            }
        },
        [&](uint32_t seq, int err) {
            if (seq == bind_seq) bind_err = err;
            else if (seq == params_seq) params_err = err;
        });

    const std::string q = "nfqueue " + std::to_string(opt_.queue_num) + ": ";
    if (rc != 0) {
        throw NlSocketError(q + "configuration failed: " + std::strerror(rc), rc);
    }
    if (startup_verdict_err != 0) {
        throw NlSocketError(q + "failed to fail-open a packet received during startup: " +
                            std::strerror(startup_verdict_err));
    }
    if (bind_err != 0) {
        if (bind_err == std::numeric_limits<int>::min())
            throw NlSocketError(q + "timed out waiting for bind acknowledgement", ETIMEDOUT);
        std::string msg = q + "bind failed: " + std::strerror(bind_err);
        if (bind_err == EBUSY) msg += " (queue already bound by another process)";
        else if (bind_err == EPERM) msg += " (CAP_NET_ADMIN required)";
        else if (bind_err == EINVAL || bind_err == EOPNOTSUPP)
            msg += " (is nfnetlink_queue available?)";
        throw NlSocketError(msg, bind_err);
    }
    if (params_err != 0) {
        if (params_err == std::numeric_limits<int>::min())
            throw NlSocketError(q + "timed out waiting for parameter acknowledgement", ETIMEDOUT);
        throw NlSocketError(q + "set parameters failed: " + std::strerror(params_err), params_err);
    }
    if (startup_malformed) {
        throw NlSocketError(q + "received a malformed queued packet before startup completed");
    }
    bound_ = true;
    replacement_ = detect_replacement_capability();
    tx_.clear();
    if (opt_.fail_open) probe_fail_open();
    if (opt_.gso) enable_gso();
}

void NfQueue::enable_gso() {
    // A separate request: an unknown flag bit rejects the whole attribute
    // (EOPNOTSUPP on 3.6..3.9), which must not take fail-open down with it.
    // Kernels before 3.6 ignore the attribute and ACK; that is harmless too.
    // Whatever the answer, the bind stays: GSO only changes how big the queued
    // packets are.  The receive buffer and copy range already cover 64 KiB.
    tx_.clear();
    const uint32_t seq = sock_.next_seq();
    build_nfqueue_flags(tx_, seq, opt_.queue_num, uapi::kNfqaCfgFGso, uapi::kNfqaCfgFGso);
    int err = ETIMEDOUT;
    (void)sock_.transact(
        tx_.data(), tx_.size(), seq, seq, kConfigTimeoutMs,
        [&](const MsgView& msg) {
            if (msg.type != kMsgPacket) return;
            uint32_t id = 0;
            if (!packet_id_from_attrs(msg.attrs, id)) return;
            MsgBuilder verdict_msg;
            build_nfqueue_verdict(verdict_msg, opt_.queue_num, id, NF_ACCEPT, nullptr, 0);
            int send_err = 0;
            (void)sock_.send(verdict_msg.data(), verdict_msg.size(), send_err);
        },
        [&](uint32_t ack_seq, int ack_err) {
            if (ack_seq == seq) err = ack_err;
        });
    gso_ = classify_errno(err, "NFQA_CFG_F_GSO");
    tx_.clear();
}

void NfQueue::probe_fail_open() {
    // Two requests in one datagram: the real flag, and a control carrying an
    // unknown flag bit.  Kernels >= 3.6 reject the control (EOPNOTSUPP); older
    // ones ignore the whole attribute and ACK both.  Neither request can
    // disturb the bind that already succeeded.
    tx_.clear();
    const uint32_t enable_seq = sock_.next_seq();
    build_nfqueue_flags(tx_, enable_seq, opt_.queue_num, uapi::kNfqaCfgFFailOpen,
                        uapi::kNfqaCfgFFailOpen);
    const uint32_t control_seq = sock_.next_seq();
    build_nfqueue_flags(tx_, control_seq, opt_.queue_num, 0x80000000u, 0u);
    int enable_err = ETIMEDOUT;
    int control_err = ETIMEDOUT;
    const int rc = sock_.transact(
        tx_.data(), tx_.size(), enable_seq, control_seq, kConfigTimeoutMs,
        [&](const MsgView& msg) {
            // A packet can be delivered while the probe waits; release it.
            if (msg.type != kMsgPacket) return;
            uint32_t id = 0;
            if (!packet_id_from_attrs(msg.attrs, id)) return;
            MsgBuilder verdict_msg;
            build_nfqueue_verdict(verdict_msg, opt_.queue_num, id, NF_ACCEPT, nullptr, 0);
            int err = 0;
            (void)sock_.send(verdict_msg.data(), verdict_msg.size(), err);
        },
        [&](uint32_t seq, int err) {
            if (seq == enable_seq) enable_err = err;
            else if (seq == control_seq) control_err = err;
        });
    // rc is the first non-zero ACK (expected for the control request); the
    // per-sequence results above are what classify the outcome, and an ACK that
    // never arrived keeps its ETIMEDOUT default.
    (void)rc;
    fail_open_ = classify_fail_open(enable_err, control_err);
    tx_.clear();
}

NfQueue::~NfQueue() {
    if (bound_ && sock_.fd() >= 0) {
        tx_.clear();
        build_nfqueue_unbind(tx_, sock_.next_seq(), opt_.queue_num);
        int err = 0;
        (void)sock_.send(tx_.data(), tx_.size(), err);
    }
}

int NfQueue::receive(const std::function<void(const QueuedPacket&)>& on_packet) {
    int delivered = 0;
    for (int i = 0; i < kMaxDatagramsPerReceive; ++i) {
        int err = 0;
        const ssize_t n = sock_.recv(rx_, err);
        if (n < 0) {
            if (err == EAGAIN || err == EWOULDBLOCK) break;
            // ENOBUFS: the kernel dropped queued messages (NETLINK_NO_ENOBUFS
            // should already suppress it); count it and keep the listener.
            if (err == ENOBUFS) {
                ++overruns_;
                last_errno_ = err;
                continue;
            }
            // EMSGSIZE: MSG_TRUNC consumed the datagram, so its packet ID
            // cannot be recovered safely.  Stop the listener and expose the loss.
            ++overruns_;
            last_errno_ = err;
            return -1;
        }
        bool malformed_datagram = false;
        const bool valid = for_each_msg(
            ByteView(rx_.data(), static_cast<std::size_t>(n)), [&](const MsgView& msg) {
                if (msg.type != kMsgPacket) return true;
                QueuedPacket packet;
                if (parse_nfqueue_packet(msg, packet)) {
                    on_packet(packet);
                    ++delivered;
                    return true;
                }

                uint32_t id = 0;
                if (!packet_id_from_attrs(msg.attrs, id) || !verdict(id, NF_ACCEPT)) {
                    malformed_datagram = true;
                    last_errno_ = last_errno_ == 0 ? EPROTO : last_errno_;
                    return false;
                }
                return true;
            });
        if (!valid || malformed_datagram) {
            if (last_errno_ == 0) last_errno_ = EPROTO;
            return -1;
        }
    }
    return delivered;
}

bool NfQueue::verdict(uint32_t packet_id, uint32_t verdict, const uint8_t* replacement,
                      std::size_t replacement_len) {
    // nfqnl_mangle() is restricted to the initial user namespace.  Sending a
    // payload from an unprivileged user namespace receives ACK 0 but the
    // kernel changes the effective verdict to DROP; preserve the packet by
    // falling back to an ordinary verdict instead.
    if (replacement != nullptr && !payload_replacement_supported()) {
        replacement = nullptr;
        replacement_len = 0;
    }
    tx_.clear();
    build_nfqueue_verdict(tx_, opt_.queue_num, packet_id, verdict, replacement, replacement_len);
    int err = 0;
    if (!sock_.send(tx_.data(), tx_.size(), err)) {
        last_errno_ = err;
        return false;
    }
    return true;
}

bool NfQueue::verdict_batch_accept(uint32_t max_packet_id) {
    tx_.clear();
    build_nfqueue_verdict_batch(tx_, opt_.queue_num, max_packet_id, NF_ACCEPT);
    int err = 0;
    if (!sock_.send(tx_.data(), tx_.size(), err)) {
        last_errno_ = err;
        return false;
    }
    return true;
}

#ifdef KEEN_PBR3_TESTING
bool NfQueue::verdict_checked_for_test(uint32_t packet_id, uint32_t verdict,
                                       const uint8_t* replacement,
                                       std::size_t replacement_len, int timeout_ms,
                                       int& kernel_errno) {
    const uint32_t seq = sock_.next_seq();
    tx_.clear();
    build_verdict_common(tx_, kMsgVerdict, NLM_F_REQUEST | NLM_F_ACK, seq, opt_.queue_num,
                         packet_id, verdict, replacement, replacement_len);
    int ack_errno = std::numeric_limits<int>::min();
    const int rc = sock_.transact(
        tx_.data(), tx_.size(), seq, seq, timeout_ms, [](const MsgView&) {},
        [&](uint32_t ack_seq, int error) {
            if (ack_seq == seq) ack_errno = error;
        });
    if (rc != 0) {
        kernel_errno = rc;
        last_errno_ = rc;
        return false;
    }
    if (ack_errno == std::numeric_limits<int>::min()) {
        kernel_errno = ETIMEDOUT;
        last_errno_ = ETIMEDOUT;
        return false;
    }
    kernel_errno = ack_errno;
    if (ack_errno != 0) last_errno_ = ack_errno;
    return ack_errno == 0;
}

NfQueue::NfQueue(ForTest, int rcvbuf_bytes)
    : opt_(), sock_(rcvbuf_bytes, true), tx_(kRxBufSize + 512) {
    rx_.resize(kRxBufSize);
    bound_ = false;
}

NfQueue nfqueue_for_test(int rcvbuf_bytes) {
    return NfQueue(NfQueue::ForTest::tag, rcvbuf_bytes);
}
#endif

} // namespace keen_pbr3::nfnl
