#pragma once

#include "../dns/dns_wire.hpp"
#include "../l7/flow_buffer.hpp"
#include "../l7/quic_initial.hpp"
#include "../lists/domain_index.hpp"
#include "../netfilter/set_writer.hpp"
#include "set_element_cache.hpp"
#include "../util/byte_view.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace keen_pbr3 {

struct InterceptListTarget {
    std::string set_v4;     // "" if the list has no v4 dynamic set
    std::string set_v6;     // "" if none / IPv6 disabled
    uint32_t min_ttl_s{0};  // floor for element timeout; 0 = permanent elements
};

struct InterceptSnapshot {
    std::shared_ptr<const DomainIndex> index;
    std::vector<InterceptListTarget> targets;  // indexed by DomainIndex::ListId
    uint32_t max_ttl_s{86400};
    std::string marker_domain{"check.keen.pbr"};
    std::array<uint8_t, 4> marker_ipv4{127, 0, 0, 88};
    bool tls{true};
    bool http{true};
    bool quic{true};
};

enum class InterceptSource : uint8_t { dns, sni, http, quic, marker };

struct InterceptEvent {
    uint64_t seq{0};
    int64_t ts_ms{0};
    InterceptSource source{InterceptSource::dns};
    // Client address observed in the captured packet. DNS replies use the
    // reply destination; L7 requests use the request source.
    std::string client_ip;
    std::string domain;
    std::vector<std::string> lists;
    std::vector<std::string> ips;
    uint32_t added{0};
    uint32_t refreshed{0};
    uint32_t errors{0};
    uint32_t hold_us{0};
    uint32_t parse_us{0};      // captured request/response parse time
    uint32_t set_write_us{0};  // time inside the set writer for this observation
    bool timed_out{false};
    bool late_write{false};    // set write finished after the verdict was released
    uint32_t cache_hits{0};         // adds skipped before the verdict: element already cached
    uint32_t deferred_refresh{0};   // timeout refreshes queued for after the verdict
};

// A matched L7 packet is handed to the service's bounded worker when DNS
// interception is enabled.  Keep the snapshot alive because SetAdd stores
// string_views into its set names.
struct InterceptL7Work {
    std::shared_ptr<const InterceptSnapshot> snapshot;
    std::vector<nfnl::SetAdd> adds;
    std::vector<uint16_t> slots;  // set-cache slot per add (parallel to `adds`)
    uint64_t cache_epoch{0};      // SetElementCache epoch captured before the lookups
    int64_t cache_now_ms{0};      // steady ms used for the lookups / expiry estimate
    InterceptEvent event;
    uint8_t family{0};
    std::array<uint8_t, 16> destination{};
};

struct InterceptCounters {
    std::atomic<uint64_t> dns_packets{0};
    std::atomic<uint64_t> dns_parse_errors{0};
    std::atomic<uint64_t> dns_matched{0};
    std::atomic<uint64_t> dns_hold_timeouts{0};
    std::atomic<uint64_t> dns_late_writes{0};
    std::atomic<uint64_t> dns_late_write_errors{0};
    std::atomic<uint64_t> set_write_slow{0};
    std::atomic<uint64_t> dns_tcp_partial{0};
    std::atomic<uint64_t> marker_hits{0};
    std::atomic<uint64_t> l7_packets{0};
    std::atomic<uint64_t> l7_matched{0};
    std::atomic<uint64_t> set_added{0};
    std::atomic<uint64_t> set_refreshed{0};
    std::atomic<uint64_t> set_errors{0};
    std::atomic<uint64_t> set_cache_hits{0};
    std::atomic<uint64_t> set_cache_misses{0};
    std::atomic<uint64_t> set_cache_entries{0};  // gauge
    std::atomic<uint64_t> dns_refresh_deferred{0};
    std::atomic<uint64_t> refresh_dropped{0};
    std::atomic<uint64_t> conntrack_requests{0};
    std::atomic<uint64_t> conntrack_deleted{0};
    std::atomic<uint64_t> conntrack_errors{0};
    std::atomic<uint64_t> queue_overruns{0};
    std::atomic<uint64_t> log_overruns{0};
};

class ConntrackCleanupSink {
public:
    virtual ~ConntrackCleanupSink() = default;
    // Non-blocking.  `dst` holds the address in its first 4 (IPv4) or 16 bytes.
    virtual void request(uint8_t family, const std::array<uint8_t, 16>& dst) = 0;
};

// Pure packet-processing core.  on_dns_packet()/on_l7_packet() must be called
// from a single thread (they reuse member buffers); set_snapshot(),
// events_since() and counters are thread-safe.
class InterceptProcessor {
public:
    using L7Submitter = std::function<void(InterceptL7Work)>;
    using WriterAdmission = std::function<bool()>;
    using WriterRelease = std::function<void()>;

    InterceptProcessor(nfnl::DynamicSetWriter& writer, ConntrackCleanupSink& cleanup,
                       InterceptCounters& counters);

    void set_snapshot(std::shared_ptr<const InterceptSnapshot> snapshot);
    void set_l7_submitter(L7Submitter submitter);
    void set_writer_callbacks(WriterAdmission dns_admission, WriterRelease dns_release,
                              WriterAdmission l7_admission, WriterRelease l7_release);
    void process_l7_work(InterceptL7Work work, nfnl::DynamicSetWriter& writer);
    void reject_l7_work(InterceptL7Work work);

    struct DnsDecision {
        bool replace{false};
        const std::vector<uint8_t>* replacement{nullptr};  // valid until the next on_dns_packet()
        // The hold deadline passed (or the on-time write timed out); the
        // unwritten adds were queued in the pending-late batch.  The verdict
        // must be sent now; the service calls flush_late_writes() once per
        // loop iteration after the queue was drained.
        bool late_write{false};
    };

    // Budget for one whole post-verdict flush, and the reduced budget used for
    // 1 s after a flush ended in ETIMEDOUT (stuck-kernel backoff).
    static constexpr int kLateWriteBudgetMs = 500;
    static constexpr int kLateBackoffBudgetMs = 50;
    static constexpr int kLateBackoffWindowMs = 1000;
    // Fixed capacity of the pending-late batch (elements and deferred events).
    static constexpr std::size_t kLateBatchCapacity = 512;
    // A cached element is only refreshed after the verdict when the new expiry
    // would exceed the cached one by more than this.
    static constexpr int64_t kRefreshSlackMs = 60000;

    // Writes ALL pending-late adds with one writer call, requests conntrack
    // cleanup for Added elements and publishes the deferred events.  No-op when
    // nothing is pending.  Never throws.  Same thread as on_dns_packet().
    void flush_late_writes();
    std::size_t pending_late_events() const { return late_events_.size(); }
    std::size_t pending_refreshes() const { return refresh_adds_.size(); }

    // Forgets every cached element (sets were recreated / flushed).  Thread-safe.
    void invalidate_set_cache();
    // Test seam: replaces the steady clock used for cache timestamps.
    void set_clock(std::function<std::chrono::steady_clock::time_point()> clock);
    SetElementCache& set_cache() { return cache_; }

    // l3 = full IP packet from NFQUEUE.  Never throws.
    DnsDecision on_dns_packet(ByteView l3, std::chrono::steady_clock::time_point deadline,
                              bool replacement_allowed);
    void on_l7_packet(ByteView l3, std::chrono::steady_clock::time_point now);

    std::vector<InterceptEvent> events_since(uint64_t after_seq, std::size_t max) const;
    // Sequence number of the newest event (0 if none yet).
    uint64_t last_event_seq() const;

    static constexpr std::size_t kEventCapacity = 512;

private:
    DnsDecision handle_dns(ByteView l3, std::chrono::steady_clock::time_point deadline,
                           bool replacement_allowed);
    bool defer_late_write(const std::shared_ptr<const InterceptSnapshot>& snap, InterceptEvent&& event,
                          std::chrono::steady_clock::time_point started, bool after_timeout);
    void handle_l7(ByteView l3, std::chrono::steady_clock::time_point now);
    std::shared_ptr<const InterceptSnapshot> snapshot() const;
    struct SlotTable {
        // [0] = v4 set slot, [1] = v6 set slot, per InterceptListTarget.
        std::vector<std::array<uint16_t, 2>> by_target;
    };
    std::shared_ptr<const InterceptSnapshot> snapshot_and_slots(
        std::shared_ptr<const SlotTable>& slots) const;
    std::chrono::steady_clock::time_point clock_now() const;
    static int64_t to_ms(std::chrono::steady_clock::time_point t);
    void note_written(uint16_t slot, const nfnl::SetAdd& add, int64_t at_ms, uint64_t epoch);
    void sync_cache_gauge();
    bool queue_refresh(const std::shared_ptr<const InterceptSnapshot>& snap,
                       const nfnl::SetAdd& add, uint16_t slot);
    // Splits adds_ (in place) into the pre-verdict part (cache miss, kept) and
    // the cached part (dropped, or queued for a post-verdict refresh).
    void classify_adds(const std::shared_ptr<const InterceptSnapshot>& snap, int64_t now_ms,
                       bool queue_refreshes, InterceptEvent& event);
    void flush_refreshes();
    void push_event(InterceptEvent&& event);
    void append_add(const InterceptSnapshot& snap, const SlotTable& slots, DomainIndex::ListId id,
                    uint8_t family,
                    const std::array<uint8_t, 16>& addr, uint32_t record_ttl_s, bool use_record_ttl);
    void collect_list_names(const InterceptSnapshot& snap, std::vector<std::string>& out) const;
    bool snapshot_is_current(const std::shared_ptr<const InterceptSnapshot>& snapshot) const;
    void record_l7_result(InterceptL7Work work, nfnl::DynamicSetWriter& writer);

    nfnl::DynamicSetWriter& writer_;
    ConntrackCleanupSink& cleanup_;
    InterceptCounters& counters_;

    mutable std::mutex snapshot_mutex_;
    std::shared_ptr<const InterceptSnapshot> snapshot_;
    std::shared_ptr<const SlotTable> slots_;
    L7Submitter l7_submitter_;
    WriterAdmission dns_admission_;
    WriterRelease dns_release_;
    WriterAdmission l7_admission_;
    WriterRelease l7_release_;

    mutable std::mutex events_mutex_;
    std::deque<InterceptEvent> events_;
    uint64_t next_seq_{1};

    // Hot-thread-only scratch state.
    dns_wire::ParsedResponse response_;
    std::vector<uint8_t> replacement_;
    std::vector<DomainIndex::ListId> ids_;
    std::vector<DomainIndex::ListId> ids_tmp_;
    std::vector<nfnl::SetAdd> adds_;
    std::vector<uint16_t> add_slots_;  // parallel to adds_
    std::vector<nfnl::SetAddResult> results_;
    // Pending-late batch (hot thread only).  Capacity is reserved once in the
    // constructor, so queuing never reallocates.
    struct LateEvent {
        InterceptEvent event;
        std::shared_ptr<const InterceptSnapshot> snapshot;  // keeps SetAdd string_views alive
        std::vector<dns_wire::AddressRecord> addresses;     // for conntrack cleanup
        std::size_t first{0};
        std::size_t count{0};
        bool snapshot_ok{false};    // set by the flush
        bool after_timeout{false};  // first attempt timed out: outcome unknown
    };
    std::vector<nfnl::SetAdd> late_adds_;
    std::vector<uint16_t> late_slots_;  // parallel to late_adds_
    std::vector<LateEvent> late_events_;
    std::vector<nfnl::SetAdd> flush_adds_;
    std::vector<uint16_t> flush_slots_;
    std::vector<nfnl::SetAddResult> flush_results_;
    // Post-verdict timeout refreshes of elements believed to exist (hot thread
    // only, fixed capacity kLateBatchCapacity; overflow is dropped, not an error).
    std::vector<nfnl::SetAdd> refresh_adds_;
    std::vector<uint16_t> refresh_slots_;
    std::vector<std::shared_ptr<const InterceptSnapshot>> refresh_snaps_;  // keep set names alive
    SetElementCache cache_;
    std::function<std::chrono::steady_clock::time_point()> clock_;
    std::chrono::steady_clock::time_point late_backoff_until_{};
    l7::FlowBuffers flows_;
    l7::QuicCryptoAssembler quic_;
    std::vector<uint8_t> tls_scratch_;
    std::string sni_;
};

} // namespace keen_pbr3
