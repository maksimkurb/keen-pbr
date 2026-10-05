#pragma once

#include "nl_msg.hpp"
#include "nl_socket.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace keen_pbr3::nfnl {

enum class ConntrackFamily : uint8_t { ipv4 = 4, ipv6 = 6 };

// Conntrack exposes a non-default zone either for both directions in the
// top-level CTA_ZONE attribute or for one direction in the corresponding
// tuple.  Keep this provenance: flattening it to a single number can make a
// later CT_DELETE target the wrong zone.
enum class ConntrackZoneScope : uint8_t { none, top, original, reply };

struct ConntrackTuple {
    ConntrackFamily family{ConntrackFamily::ipv4};
    uint8_t protocol{0};
    std::array<uint8_t, 16> src{};
    std::array<uint8_t, 16> dst{};
    uint16_t src_port{0};
    uint16_t dst_port{0};
    bool has_ports{true};
    uint16_t icmp_id{0};
    uint8_t icmp_type{0};
    uint8_t icmp_code{0};
    bool has_icmp_key{false};
    bool has_protocol_key{true};
    uint16_t zone{0};
    bool has_zone{false};
    ConntrackZoneScope zone_scope{ConntrackZoneScope::none};

    bool operator==(const ConntrackTuple& other) const;
};

struct ConntrackEntry {
    ConntrackTuple original;
    // Only populated for a reply-direction zone.  CT_DELETE must then carry
    // the reply tuple alone; retaining it also avoids flattening the zone
    // onto the original tuple.
    std::optional<ConntrackTuple> reply;
    uint32_t mark{0};
    bool has_id{false};
    uint32_t id{0};
    // Duplicate zone locations or incomplete reply tuples remain visible in
    // dumps but are not safe deletion candidates.
    bool deletion_safe{true};

    bool deletion_candidate() const {
        if (!deletion_safe) return false;
        if (original.zone_scope == ConntrackZoneScope::reply) {
            return reply.has_value() && reply->has_protocol_key;
        }
        return original.has_protocol_key;
    }
};

// Kernel-side dump selector: only entries whose ORIGINAL tuple has this source
// address are sent (CTA_FILTER + CTA_TUPLE_ORIG, Linux >= 5.x).  Kernels
// without it ignore the attributes and send everything (or reject them with
// EINVAL), so it is only an optimisation: the userspace `filter` still decides.
struct ConntrackKernelFilter {
    std::array<uint8_t, 16> src{};
};

struct ConntrackOptions {
    int rcvbuf_bytes{2 << 20};
    std::size_t max_entries{4096};
    std::size_t max_delete_batch{128};
    // Optional dump-time selector; rejected entries are not retained, keeping
    // destination-scoped operations independent of unrelated table size.
    std::function<bool(const ConntrackEntry&)> filter;
    // Optional kernel-side pre-filter; see ConntrackKernelFilter.
    std::optional<ConntrackKernelFilter> kernel_filter;
};

// Parses CTA_TUPLE_ORIG plus mark/id and preserves canonical zone provenance.
// Reply-only zones retain their complete reply tuple for the corresponding
// CT_DELETE form; incomplete reply tuples remain non-deletable.
bool parse_conntrack_entry(const MsgView& message, ConntrackEntry& entry);

void build_conntrack_get(MsgBuilder& builder, uint32_t seq, ConntrackFamily family,
                         const ConntrackKernelFilter* kernel_filter = nullptr);
void build_conntrack_delete(MsgBuilder& builder, uint32_t seq,
                            const ConntrackEntry& entry);

// A dump is started by the constructor and advanced by receive().  receive()
// is bounded by timeout_ms and returns 1 when NLMSG_DONE was received, 0 when
// more data is pending, and -1 on a malformed/error/truncated dump.
class ConntrackDump {
public:
    ConntrackDump(ConntrackFamily family, const ConntrackOptions& options = {});

    int receive(int timeout_ms);
    int fd() const { return socket_.fd(); }
    bool complete() const { return complete_; }
    int last_errno() const { return last_errno_; }
    const std::vector<ConntrackEntry>& entries() const { return entries_; }
    // Entries received although their source differs from the kernel
    // pre-filter's.  Non-zero means the kernel ignored the pre-filter.
    std::size_t kernel_filter_mismatches() const { return kernel_mismatches_; }

#ifdef KEEN_PBR3_TESTING
    // Feeds one already-received datagram through the same state machine used
    // by receive().  This is a deterministic seam for malformed ACK/DONE and
    // dump-interruption tests; it does not bypass the production parser.
    int consume_datagram_for_test(ByteView datagram);
    uint32_t sequence_for_test() const { return seq_; }
#endif

private:
    int process_datagram(ByteView datagram);

    NlSocket socket_;
    std::vector<uint8_t> rx_;
    std::vector<ConntrackEntry> entries_;
    uint32_t seq_{0};
    std::size_t max_entries_{0};
    std::size_t kernel_mismatches_{0};
    std::optional<ConntrackKernelFilter> kernel_filter_;
    std::function<bool(const ConntrackEntry&)> filter_;
    bool complete_{false};
    int last_errno_{0};
};

// The kernel refused the dump pre-filter (an older kernel validating the
// request strictly); the caller retries without it.
bool conntrack_kernel_filter_refused(int error);

// Whether the kernel's ctnetlink dump pre-filter (source address) is unusable:
// refused, or ignored (entries of other clients came back).  A property of the
// kernel, learned once per process and shared by every cleanup: at service
// start by probe_conntrack_kernel_filter(), otherwise by the first cleanup that
// notices.  Only a definite answer is remembered.
bool conntrack_kernel_filter_unsupported();
void note_conntrack_kernel_filter_unsupported();
// Dumps IPv4 conntrack with a pre-filter on a documentation address; records
// "unsupported" when the kernel refuses it or returns other clients' entries
// (the latter needs a non-empty table; otherwise the first cleanup learns it).
// Never throws.
void probe_conntrack_kernel_filter();
#ifdef KEEN_PBR3_TESTING
void reset_conntrack_kernel_filter_state_for_tests();
#endif

// Userspace scope of a cleanup: original tuple client -> one of `dsts`
// (addresses in the first 4 or 16 bytes).  This is the single predicate that
// decides what may be deleted, regardless of the kernel pre-filter.
std::function<bool(const ConntrackEntry&)> make_client_destination_filter(
    ConntrackFamily family, const std::array<uint8_t, 16>& client,
    std::vector<std::array<uint8_t, 16>> dsts);

// Sends a bounded batch of tuple-specific deletes and waits for one ACK per
// request.  Kernel errors are counted individually; callers can continue
// draining after the first failure and report a partial cleanup.
class ConntrackDeleteBatch {
public:
    ConntrackDeleteBatch(const std::vector<ConntrackEntry>& entries,
                         const ConntrackOptions& options = {});

    int receive(int timeout_ms);
    bool complete() const { return complete_; }
    int last_errno() const { return last_errno_; }
    std::size_t failures() const { return failures_; }
    std::size_t succeeded() const { return succeeded_; }
    std::size_t skipped() const { return skipped_; }
    std::size_t pending() const { return pending_; }
    int fd() const { return socket_.fd(); }

private:
    NlSocket socket_;
    MsgBuilder tx_;
    std::vector<uint8_t> rx_;
    uint32_t first_seq_{0};
    std::vector<bool> acked_;
    std::size_t pending_{0};
    std::size_t failures_{0};
    std::size_t succeeded_{0};
    std::size_t skipped_{0};
    bool complete_{false};
    int last_errno_{0};
};

} // namespace keen_pbr3::nfnl
