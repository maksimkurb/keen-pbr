#pragma once

#include "nl_msg.hpp"
#include "nl_socket.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace keen_pbr3::nfnl {

struct QueuedPacket {
    uint32_t packet_id{0};    // from NFQA_PACKET_HDR (big-endian in the attr)
    uint16_t hw_protocol{0};  // ETH_P_IP / ETH_P_IPV6 (host order)
    uint8_t hook{0};
    uint32_t mark{0};         // NFQA_MARK, 0 if absent
    ByteView payload;         // NFQA_PAYLOAD: full L3 packet; valid until next receive call
    bool truncated{false};    // captured payload is shorter than the original packet
};

struct NfQueueOptions {
    uint16_t queue_num{0};
    uint32_t copy_range{0xFFFF};
    uint32_t queue_maxlen{1024};
    bool fail_open{true};     // NFQA_CFG_F_FAIL_OPEN
    int rcvbuf_bytes{2 << 20};
};

class NfQueue {
public:
    // Opens the socket, binds the queue and sets params.  Queue binding is
    // deliberately scoped to this queue; it never unbinds another process's
    // global protocol-family state.
    explicit NfQueue(const NfQueueOptions& opt);
    ~NfQueue();  // sends NFQNL_CFG_CMD_UNBIND best-effort
    NfQueue(const NfQueue&) = delete;
    NfQueue& operator=(const NfQueue&) = delete;

    int fd() const { return sock_.fd(); }

    // Non-blocking: drains available datagrams, calling on_packet for each
    // valid packet message.  Malformed messages are fail-opened when their ID
    // can be recovered; missing IDs, truncation, and verdict-send failures are
    // fatal and exposed through last_errno().
    int receive(const std::function<void(const QueuedPacket&)>& on_packet);

    // NFQNL_MSG_VERDICT (NF_ACCEPT/NF_DROP). With replacement != nullptr also sends NFQA_PAYLOAD
    // (packet rewrite). Fire-and-forget (no ACK); returns false if sendto failed.
    bool verdict(uint32_t packet_id, uint32_t verdict, const uint8_t* replacement = nullptr,
                 std::size_t replacement_len = 0);

    // NFQNL_MSG_VERDICT_BATCH: accepts every queued packet with id <= max_packet_id.
    bool verdict_batch_accept(uint32_t max_packet_id);

    // Linux rejects NFQA_PAYLOAD mangling from a network namespace owned by a
    // non-initial user namespace.  Callers can use this to avoid requesting a
    // replacement that the kernel would turn into NF_DROP.
    bool payload_replacement_supported() const;

#ifdef KEEN_PBR3_TESTING
    // Diagnostic-only ACK path.  Runtime verdicts intentionally remain
    // fire-and-forget so a packet hold is never extended by a netlink round
    // trip; tests use this to distinguish send() success from kernel ACK.
    bool verdict_checked_for_test(uint32_t packet_id, uint32_t verdict,
                                  const uint8_t* replacement, std::size_t replacement_len,
                                  int timeout_ms, int& kernel_errno);
#endif

    uint64_t overruns() const { return overruns_; }
    int last_errno() const { return last_errno_; }

private:
    NfQueueOptions opt_;
    NlSocket sock_;
    MsgBuilder tx_;
    std::vector<uint8_t> rx_;
    uint64_t overruns_{0};
    int last_errno_{0};
    bool bound_{false};
    bool replacement_capability_{false};
};

// Exposed for golden tests.
// NFQNL_MSG_CONFIG with NFQA_CFG_CMD = pf_cmd (PF_UNBIND / PF_BIND) for `pf`. Uses queue 0.
void build_nfqueue_pf_cmd(MsgBuilder& b, uint32_t seq, uint8_t pf_cmd, uint8_t pf);
// Includes a NFQNL_CFG_CMD_BIND message (flags REQUEST|ACK).
void build_nfqueue_bind(MsgBuilder& b, uint32_t seq, uint16_t queue_num);
void build_nfqueue_unbind(MsgBuilder& b, uint32_t seq, uint16_t queue_num);
// NFQA_CFG_PARAMS (copy mode PACKET + range), QUEUE_MAXLEN, FLAGS/MASK (fail-open).
void build_nfqueue_params(MsgBuilder& b, uint32_t seq, const NfQueueOptions& o);
void build_nfqueue_verdict(MsgBuilder& b, uint16_t queue_num, uint32_t id, uint32_t verdict,
                           const uint8_t* payload, std::size_t len);
void build_nfqueue_verdict_batch(MsgBuilder& b, uint16_t queue_num, uint32_t max_id, uint32_t verdict);
bool parse_nfqueue_packet(const MsgView& m, QueuedPacket& out);

#ifdef KEEN_PBR3_TESTING
// Pure decision seam for the namespace-owner capability check.  `owner_fd`
// is non-negative when NS_GET_USERNS succeeded; otherwise the network
// namespace inode is the only permitted legacy fallback.
bool nfqueue_initial_owner_decision_for_test(int owner_fd, uint64_t owner_inode,
                                             uint64_t netns_inode);
#endif

} // namespace keen_pbr3::nfnl
