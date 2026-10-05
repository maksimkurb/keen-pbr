#pragma once

#include "kernel_probe.hpp"
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
    // NFQA_SKB_INFO.  gso: the packet is a GSO/GRO super-packet (queue GSO flag), possibly
    // larger than the MTU; its payload must never be replaced.  csum_not_ready: checksums
    // are not filled in yet (offload); read-only consumers do not care.
    bool gso{false};
    bool csum_not_ready{false};
};

struct NfQueueOptions {
    uint16_t queue_num{0};
    uint32_t copy_range{0xFFFF};
    uint32_t queue_maxlen{1024};
    bool fail_open{true};     // NFQA_CFG_F_FAIL_OPEN
    // NFQA_CFG_F_GSO: queue GSO/GRO super-packets whole instead of letting the kernel
    // segment them first.  Best effort; a kernel without it keeps segmenting.
    bool gso{true};
    int rcvbuf_bytes{2 << 20};
};

// Whether the kernel accepts NFQA_CFG_F_GSO.  The answer is a property of the
// kernel, learned once per process (by probe_nfqueue_gso() at service start, or
// by the first real bind when that could not run) and shared by every later
// bind: a rejected flag is not sent again.  Only a definite rejection is
// remembered; a timeout or socket error stays "unknown" and is retried.
bool nfqueue_gso_rejected();
// Binds a spare queue (never `avoid_queue`) with GSO requested, then unbinds it.
// Records the kernel's answer; a bind failure leaves it unknown.  Never throws.
void probe_nfqueue_gso(uint16_t avoid_queue);
#ifdef KEEN_PBR3_TESTING
void reset_nfqueue_gso_state_for_tests();
#endif

class NfQueue {
public:
    // Opens the socket, binds the queue and sets params.  Queue binding is
    // deliberately scoped to this queue; it never unbinds another process's
    // global protocol-family state.
    explicit NfQueue(const NfQueueOptions& opt);
    ~NfQueue();  // sends NFQNL_CFG_CMD_UNBIND best-effort
    NfQueue(const NfQueue&) = delete;
    NfQueue& operator=(const NfQueue&) = delete;

#ifdef KEEN_PBR3_TESTING
    // Test-only: for initialization by nfqueue_for_test()
    enum class ForTest { tag };
    explicit NfQueue(ForTest, int rcvbuf_bytes);
#endif

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
    bool payload_replacement_supported() const {
        return replacement_ == ReplacementCapability::supported;
    }
    // supported / unsupported (NS_GET_USERNS answered) / unknown (pre-4.9
    // kernel in a non-initial network namespace).
    ReplacementCapability payload_replacement() const { return replacement_; }

    // Result of enabling NFQA_CFG_F_FAIL_OPEN on the bound queue.  A kernel
    // that rejects or silently ignores the flag does not fail the bind: the
    // queue keeps running without fail-open and this reports why.  not_run when
    // fail-open was not requested.
    const ProbeResult& fail_open_probe() const { return fail_open_; }
    bool fail_open_active() const { return fail_open_.is_ok(); }

    // Result of enabling NFQA_CFG_F_GSO (Linux 3.10).  A kernel that rejects
    // the flag does not fail the bind; the queue then receives segmented packets.
    const ProbeResult& gso_probe() const { return gso_; }
    bool gso_active() const { return gso_.is_ok(); }

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

#ifdef KEEN_PBR3_TESTING
    NlSocket& socket_for_test() { return sock_; }
#endif

private:
    void probe_fail_open();
    void enable_gso();

    NfQueueOptions opt_;
    NlSocket sock_;
    MsgBuilder tx_;
    std::vector<uint8_t> rx_;
    uint64_t overruns_{0};
    int last_errno_{0};
    bool bound_{false};
    ReplacementCapability replacement_{ReplacementCapability::unknown};
    ProbeResult fail_open_;
    ProbeResult gso_;
};

// Exposed for golden tests.
// NFQNL_MSG_CONFIG with NFQA_CFG_CMD = pf_cmd (PF_UNBIND / PF_BIND) for `pf`. Uses queue 0.
void build_nfqueue_pf_cmd(MsgBuilder& b, uint32_t seq, uint8_t pf_cmd, uint8_t pf);
// Includes a NFQNL_CFG_CMD_BIND message (flags REQUEST|ACK).
void build_nfqueue_bind(MsgBuilder& b, uint32_t seq, uint16_t queue_num);
void build_nfqueue_unbind(MsgBuilder& b, uint32_t seq, uint16_t queue_num);
// NFQA_CFG_PARAMS (copy mode PACKET + range), QUEUE_MAXLEN, and with include_flags
// FLAGS/MASK (fail-open).  NfQueue sends the flags separately so a kernel that
// rejects them cannot fail the bind.
void build_nfqueue_params(MsgBuilder& b, uint32_t seq, const NfQueueOptions& o,
                          bool include_flags = true);
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
ReplacementCapability nfqueue_replacement_decision_for_test(int owner_fd, uint64_t owner_inode,
                                                            uint64_t netns_inode);
// Test-only helper for injecting datagrams into NfQueue.receive()
NfQueue nfqueue_for_test(int rcvbuf_bytes = 2 << 20);
#endif

} // namespace keen_pbr3::nfnl
