#pragma once

#include "../dns/dns_wire.hpp"
#include "dns_tcp_reassembly.hpp"
#include "../l7/flow_buffer.hpp"
#include "../l7/quic_initial.hpp"
#include "../lists/domain_index.hpp"
#include "../netfilter/set_writer.hpp"
#include "seqlock_ring.hpp"
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
#include <optional>
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
    // What each indexed list was built from (its config signature), indexed by
    // DomainIndex::ListId.  Lets a rebind decide whether the index content of
    // a list still describes the list of the new configuration.
    std::vector<std::string> list_signatures;
    uint32_t max_ttl_s{86400};
    std::string marker_domain{"check.keen.pbr"};
    std::array<uint8_t, 4> marker_ipv4{127, 0, 0, 88};
    bool ipv6_enabled{true};  // whether IPv6 sets are available; defaults to true (fail-open)
    bool tls{true};
    bool http{true};
    bool quic{true};
};

enum class InterceptSource : uint8_t { dns, sni, http, quic, marker };

// Why a DNS hold ended in `timed_out` (diagnostics only; never drives behaviour).
enum class TimeoutCause : uint8_t {
    none,
    budget_spent_by_batch,  // deadline had already passed when this packet started
    admission_blocked,      // deadline passed while waiting for the firewall-apply write gate
    own_write_slow,         // this packet's own write ran past the deadline (ETIMEDOUT)
    late_batch_full,        // deferred adds dropped: the pending-late batch was full
    other,
};

const char* timeout_cause_name(TimeoutCause cause);

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
    uint32_t refresh_skipped{0};    // cached Fresh elements not needing refresh yet
    uint32_t not_learned{0};        // non-learnable answer addresses (0.0.0.0, ::, loopback)
    // DNS hold timing breakdown (batch_pos < 0: not a timed DNS event).
    int32_t batch_pos{-1};          // 0-based position of the packet in its wakeup round
    uint32_t batch_size{0};         // packets in the round; 0 until the round ended
    int64_t queue_wait_us{0};       // wakeup -> start of processing (earlier packets' time)
    int64_t budget_left_us{0};      // deadline - processing start; negative: already over
    uint32_t admission_wait_us{0};  // time inside dns_admission() (firewall-apply write pause)
    uint32_t write_elements{0};     // adds in this packet's synchronous write attempt
    uint32_t late_batch_elements{0};  // adds in the combined late write that covered this event
    int32_t write_errno{0};         // errno of the last failed write for this event
    TimeoutCause timeout_cause{TimeoutCause::none};  // set only when timed_out
    uint16_t qtype{0};  // DNS query type (0 for non-DNS)
    uint8_t rcode{0};   // DNS response code (0 for non-DNS)
};

// What the hot path records per observation: fixed size, trivially copyable,
// no heap.  Addresses are raw bytes and lists are ids; events_since() turns a
// record into an InterceptEvent (inet_ntop, list names) on the reading side.
// `seq` must stay the first member (SeqlockRing).  Anything that does not fit
// is counted (`*_overflow`) rather than stored.
struct EventRecord {
    static constexpr std::size_t kMaxIps = 8;
    static constexpr std::size_t kMaxLists = 32;
    static constexpr std::size_t kMaxDomain = 254;

    uint64_t seq{0};
    uint64_t generation{0};   // snapshot generation `list_ids` belong to
    int64_t ts_steady_us{0};  // steady-clock time of the observation
    int64_t queue_wait_us{0};
    int64_t budget_left_us{0};
    uint32_t added{0};
    uint32_t refreshed{0};
    uint32_t errors{0};
    uint32_t hold_us{0};
    uint32_t parse_us{0};
    uint32_t set_write_us{0};
    uint32_t cache_hits{0};
    uint32_t deferred_refresh{0};
    uint32_t refresh_skipped{0};
    uint32_t batch_size{0};
    uint32_t admission_wait_us{0};
    uint32_t write_elements{0};
    uint32_t late_batch_elements{0};
    int32_t write_errno{0};
    int32_t batch_pos{-1};  // < 0: not a timed DNS event
    InterceptSource source{InterceptSource::dns};
    TimeoutCause timeout_cause{TimeoutCause::none};
    bool timed_out{false};
    bool late_write{false};
    uint8_t client_family{0};  // 4 / 6; 0: no client address
    uint8_t domain_len{0};
    uint8_t ip_count{0};
    uint8_t ips_overflow{0};   // answer addresses beyond kMaxIps (saturating)
    uint8_t list_count{0};
    uint8_t lists_overflow{0};  // matched lists beyond kMaxLists (saturating)
    uint8_t rcode{0};  // DNS response code (0 for non-DNS)
    uint8_t not_learned{0};  // non-learnable answer addresses (0.0.0.0, ::, loopback, unspecified)
    uint8_t client[16]{};
    uint8_t ip_family[kMaxIps]{};
    uint8_t ips[kMaxIps][16]{};
    uint16_t qtype{0};  // DNS query type (0 for non-DNS)
    uint16_t list_ids[kMaxLists]{};
    char domain[kMaxDomain]{};
};

using EventRing = SeqlockRing<EventRecord, 512>;

// Per-round timing handed to on_dns_packet(): the wakeup time, the shared hold
// deadline and the packet's position in the round.  Implicitly built from a
// bare deadline (woke = now) for callers without batching.
struct DnsRound {
    std::chrono::steady_clock::time_point woke;
    std::chrono::steady_clock::time_point deadline;
    uint32_t batch_pos{0};

    DnsRound(std::chrono::steady_clock::time_point woke_at,
             std::chrono::steady_clock::time_point deadline_at, uint32_t pos)
        : woke(woke_at), deadline(deadline_at), batch_pos(pos) {}
    DnsRound(std::chrono::steady_clock::time_point deadline_at)  // NOLINT(google-explicit-constructor)
        : woke(std::chrono::steady_clock::now()), deadline(deadline_at) {}
};

// A matched L7 packet is handed to the service's bounded worker when DNS
// interception is enabled.  Keep the snapshot alive because SetAdd stores
// string_views into its set names.
struct InterceptL7Work {
    std::shared_ptr<const InterceptSnapshot> snapshot;
    std::vector<nfnl::SetAdd> adds;
    std::vector<uint16_t> slots;  // set-cache slot per add (parallel to `adds`)
    // Parallel to `adds`: 1 = the cache believes the element is present (Stale),
    // so it is refreshed instead of upserted.  Empty: every add is upserted.
    std::vector<uint8_t> stale;
    uint64_t cache_epoch{0};      // SetElementCache epoch captured before the lookups
    int64_t cache_now_ms{0};      // steady ms used for the lookups / expiry estimate
    EventRecord event;
    uint8_t family{0};
    std::array<uint8_t, 16> destination{};
    std::array<uint8_t, 16> client{};  // source of the captured request
};

// Set-write latency histogram (buckets <1, <5, <10, <30, <100, >=100 ms), the
// slowest write and the element count of that write.
struct WriteLatencyCounters {
    static constexpr std::size_t kBuckets = 6;
    static constexpr std::size_t kPrometheusBuckets = 11;
    // Bucket edges of the exported histograms.  25000 us (le="0.025") is the
    // "slow write" threshold the dashboards derive the slow share from.
    inline static constexpr std::array<uint64_t, kPrometheusBuckets - 1> kPrometheusBoundsUs{
        100, 250, 500, 1000, 2500, 5000, 10000, 25000, 50000, 100000};
    std::array<std::atomic<uint64_t>, kBuckets> buckets{};
    std::array<std::atomic<uint64_t>, kPrometheusBuckets> prometheus_buckets{};
    std::atomic<uint64_t> count{0};
    std::atomic<uint64_t> sum_us{0};
    std::atomic<uint64_t> max_us{0};
    std::atomic<uint64_t> max_elements{0};

    static std::size_t bucket_for(uint64_t us) {
        if (us < 1000) return 0;
        if (us < 5000) return 1;
        if (us < 10000) return 2;
        if (us < 30000) return 3;
        if (us < 100000) return 4;
        return 5;
    }
    void record(uint64_t us, uint64_t elements) {
        buckets[bucket_for(us)].fetch_add(1, std::memory_order_relaxed);
        std::size_t prometheus_bucket = 0;
        while (prometheus_bucket < kPrometheusBoundsUs.size() &&
               us > kPrometheusBoundsUs[prometheus_bucket]) {
            ++prometheus_bucket;
        }
        prometheus_buckets[prometheus_bucket].fetch_add(1, std::memory_order_relaxed);
        count.fetch_add(1, std::memory_order_relaxed);
        sum_us.fetch_add(us, std::memory_order_relaxed);
        // >= so the first write counts even when it took 0 us (fast CPUs).
        uint64_t seen = max_us.load(std::memory_order_relaxed);
        while (us >= seen) {
            if (max_us.compare_exchange_weak(seen, us, std::memory_order_relaxed)) {
                max_elements.store(elements, std::memory_order_relaxed);
                break;
            }
        }
    }
};

struct InterceptCounters {
    std::atomic<uint64_t> dns_packets{0};
    std::atomic<uint64_t> dns_parse_errors{0};
    std::atomic<uint64_t> dns_matched{0};
    std::atomic<uint64_t> dns_aaaa_ignored{0};  // AAAA replies ignored when IPv6 is disabled
    std::atomic<uint64_t> dns_hold_timeouts{0};
    std::atomic<uint64_t> dns_late_writes{0};
    std::atomic<uint64_t> dns_late_write_errors{0};
    std::atomic<uint64_t> set_write_slow{0};
    // dns_hold_timeouts split by TimeoutCause.
    std::atomic<uint64_t> dns_timeout_budget_spent_by_batch{0};
    std::atomic<uint64_t> dns_timeout_admission_blocked{0};
    std::atomic<uint64_t> dns_timeout_own_write_slow{0};
    std::atomic<uint64_t> dns_timeout_late_batch_full{0};
    std::atomic<uint64_t> dns_timeout_other{0};
    WriteLatencyCounters dns_write_latency;   // synchronous (pre-verdict) DNS writes
    WriteLatencyCounters late_write_latency;  // combined post-verdict late writes
    WriteLatencyCounters l7_write_latency;    // L7 worker writes
    WriteLatencyCounters dns_hold_latency;    // total time a DNS packet is held
    WriteLatencyCounters dns_queue_wait_latency;
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
    std::atomic<uint64_t> refresh_skipped{0};
    std::atomic<uint64_t> refresh_dropped{0};
    std::atomic<uint64_t> conntrack_requests{0};
    std::atomic<uint64_t> conntrack_deleted{0};
    std::atomic<uint64_t> conntrack_errors{0};
    std::atomic<uint64_t> queue_overruns{0};
    std::atomic<uint64_t> log_overruns{0};
};

// Adds a source's cumulative snapshot delta while tolerating listener reset.
inline void accumulate_resetting_counter(std::atomic<uint64_t>& total,
                                         uint64_t& previous,
                                         uint64_t current) {
    if (current >= previous) {
        total.fetch_add(current - previous, std::memory_order_relaxed);
    } else {
        total.fetch_add(current, std::memory_order_relaxed);
    }
    previous = current;
}

class ConntrackCleanupSink {
public:
    virtual ~ConntrackCleanupSink() = default;
    // Non-blocking.  Asks for the deletion of conntrack entries whose original
    // tuple is `client` -> `dst` (the client whose traffic caused the learning);
    // other clients' flows to `dst` must be left alone.  Both addresses hold the
    // address in their first 4 (IPv4) or 16 bytes.
    virtual void request(uint8_t family, const std::array<uint8_t, 16>& client,
                         const std::array<uint8_t, 16>& dst) = 0;
};

// Pure packet-processing core.  on_dns_packet()/on_l7_packet() must be called
// from a single thread (they reuse member buffers); set_snapshot(),
// events_since() and counters are thread-safe.
//
// Event store: a preallocated lock-free ring of EventRecords.  Producers are
// the hot thread (DNS, marker, late-flush and, without a submitter, L7 events)
// and the L7 worker (record_l7_result / reject_l7_work); the ring tolerates
// both without a mutex.  The only consumer is the control thread's 100 ms pump
// (events_since(), log_hold_timeouts()).
class InterceptProcessor {
public:
    using L7Submitter = std::function<void(InterceptL7Work)>;
    using WriterAdmission = std::function<bool()>;
    using WriterRelease = std::function<void()>;

    InterceptProcessor(nfnl::DynamicSetWriter& writer, ConntrackCleanupSink& cleanup,
                       InterceptCounters& counters);

    void set_snapshot(std::shared_ptr<const InterceptSnapshot> snapshot);
    // The currently published snapshot (null when none).  Thread-safe.
    std::shared_ptr<const InterceptSnapshot> current_snapshot() const { return snapshot(); }
    // Looks up daemon write evidence for a set in one specific snapshot. A
    // null result means that the set is not a tracked dynamic target in that
    // snapshot; the cache result itself remains advisory.
    std::optional<SetElementCache::Lookup> lookup_set_write_evidence(
        const std::shared_ptr<const InterceptSnapshot>& snapshot,
        const std::string& set_name, uint8_t family,
        const std::array<uint8_t, 16>& addr) const;
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

    // l3 = full IP packet from NFQUEUE.  Never throws.  The observation is only
    // staged: the caller sends the verdict, then calls commit_dns_event(), so
    // recording never adds to the hold latency.
    DnsDecision process_dns_packet(ByteView l3, const DnsRound& round, bool replacement_allowed);
    // Publishes the event staged by the last process_dns_packet() (if any).
    void commit_dns_event();
    // process_dns_packet() + commit_dns_event() for callers without a verdict.
    DnsDecision on_dns_packet(ByteView l3, const DnsRound& round, bool replacement_allowed);
    // Records the size of the wakeup round on its events (seq >= first_seq)
    // once the round is over.  Metadata only.
    void set_round_batch_size(uint64_t first_seq, uint32_t batch_size);
    void on_l7_packet(ByteView l3, std::chrono::steady_clock::time_point now);

    // Formats up to `max` events newer than `after_seq` (oldest first).  Events
    // overwritten in the meantime are missing at the front (the caller detects
    // the gap from the sequence numbers).
    std::vector<InterceptEvent> events_since(uint64_t after_seq, std::size_t max) const;
    // Sequence number of the newest event (0 if none yet).
    uint64_t last_event_seq() const { return ring_.last_seq(); }
    // Drain side: logs one warning per not yet logged DNS hold timeout.  Called
    // by the same single thread that drains events (never the hot thread).
    void log_hold_timeouts();

    static constexpr std::size_t kEventCapacity = 512;
    // Lists remembered by generation for formatting old events: the current and
    // the previous snapshot.
    static constexpr std::size_t kNameGenerations = 2;

private:
    DnsDecision handle_dns(ByteView l3, const DnsRound& round, bool replacement_allowed);
    struct SlotTable;
    DnsDecision handle_dns_message(ByteView l3, const dns_wire::PacketLayout& layout,
                                   ByteView message, bool udp, const DnsRound& round,
                                   std::chrono::steady_clock::time_point started,
                                   uint64_t cache_epoch,
                                   const std::shared_ptr<const InterceptSnapshot>& snap,
                                   const std::shared_ptr<const SlotTable>& slots,
                                   bool replacement_allowed);
    bool defer_late_write(const std::shared_ptr<const InterceptSnapshot>& snap,
                          std::chrono::steady_clock::time_point started, bool after_timeout,
                          TimeoutCause cause);
    void handle_l7(ByteView l3, std::chrono::steady_clock::time_point now);
    std::shared_ptr<const InterceptSnapshot> snapshot() const;
    struct SlotTable {
        std::vector<std::string> names;  // sorted dynamic set names, indexed by slot
        // [0] = v4 set slot, [1] = v6 set slot, per InterceptListTarget.
        std::vector<std::array<uint16_t, 2>> by_target;
        uint64_t generation{0};  // identifies the snapshot in EventRecord::generation
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
    // the cached part (dropped, or queued for a post-verdict refresh).  Without
    // queue_refreshes (L7) a Stale entry is kept too and flagged in add_stale_.
    void classify_adds(const std::shared_ptr<const InterceptSnapshot>& snap, int64_t now_ms,
                       bool queue_refreshes, EventRecord& event);
    void flush_refreshes();
    void bump_timeout_cause(TimeoutCause cause);
    void push_event(EventRecord& event);
    // Starts the DNS event staged for the verdict (see commit_dns_event()).
    EventRecord& stage_event(InterceptSource source);
    InterceptEvent format_event(const EventRecord& record) const;
    void append_add(const InterceptSnapshot& snap, const SlotTable& slots, DomainIndex::ListId id,
                    uint8_t family,
                    const std::array<uint8_t, 16>& addr, uint32_t record_ttl_s, bool use_record_ttl);
    void record_list_ids(const SlotTable& slots, EventRecord& event) const;
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

    EventRing ring_;
    uint64_t logged_seq_{0};  // drain side: newest event already checked for timeouts
    uint64_t next_generation_{1};  // guarded by snapshot_mutex_
    // List names of the last kNameGenerations snapshots, oldest first.
    mutable std::mutex names_mutex_;
    struct NameTable {
        uint64_t generation{0};
        std::vector<std::string> names;
    };
    std::deque<NameTable> name_tables_;
    // Staged DNS observation (hot thread only): written by process_dns_packet(),
    // published by commit_dns_event() after the verdict.
    EventRecord pending_;
    bool pending_valid_{false};

    // Hot-thread-only scratch state.
    dns_wire::ParsedResponse response_;
    std::vector<uint8_t> replacement_;
    std::vector<DomainIndex::ListId> ids_;
    std::vector<DomainIndex::ListId> ids_tmp_;
    std::vector<nfnl::SetAdd> adds_;
    std::vector<uint16_t> add_slots_;  // parallel to adds_
    std::vector<uint8_t> add_stale_;   // parallel to adds_ after an L7 classify_adds()
    std::vector<nfnl::SetAddResult> results_;
    // Pending-late batch (hot thread only).  Capacity is reserved once in the
    // constructor, so queuing never reallocates.
    struct LateEvent {
        EventRecord event;
        std::shared_ptr<const InterceptSnapshot> snapshot;  // keeps SetAdd string_views alive
        std::vector<dns_wire::AddressRecord> addresses;     // for conntrack cleanup
        std::array<uint8_t, 16> client{};                   // asker, scopes the cleanup
        bool client_valid{false};
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
    // Client (reply destination) of the DNS packet being handled; the conntrack
    // cleanup of its answers is scoped to it.  Valid only inside handle_dns().
    std::array<uint8_t, 16> dns_client_{};
    bool dns_client_valid_{false};
    std::vector<std::array<uint8_t, 16>> refresh_clients_;  // parallel to refresh_adds_
    std::vector<std::size_t> flush_refresh_idx_;            // refresh_* index per flush_adds_ entry
    std::vector<uint8_t> refresh_client_valid_;             // parallel to refresh_adds_
    SetElementCache cache_;
    std::function<std::chrono::steady_clock::time_point()> clock_;
    std::chrono::steady_clock::time_point late_backoff_until_{};
    l7::FlowBuffers flows_;
    DnsTcpReassembler tcp_reassembly_;  // DNS-over-TCP replies (hot thread only)
    DnsTcpReassembler::Messages tcp_messages_;
    l7::QuicCryptoAssembler quic_;
    std::vector<uint8_t> tls_scratch_;
    std::string sni_;
};

} // namespace keen_pbr3
