#pragma once

#include "../dns/dns_wire.hpp"
#include "../l7/flow_buffer.hpp"
#include "../l7/quic_initial.hpp"
#include "../lists/domain_index.hpp"
#include "../netfilter/set_writer.hpp"
#include "../util/byte_view.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
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
    std::string domain;
    std::vector<std::string> lists;
    std::vector<std::string> ips;
    uint32_t added{0};
    uint32_t refreshed{0};
    uint32_t errors{0};
    uint32_t hold_us{0};
    bool timed_out{false};
};

struct InterceptCounters {
    std::atomic<uint64_t> dns_packets{0};
    std::atomic<uint64_t> dns_parse_errors{0};
    std::atomic<uint64_t> dns_matched{0};
    std::atomic<uint64_t> dns_hold_timeouts{0};
    std::atomic<uint64_t> dns_tcp_partial{0};
    std::atomic<uint64_t> marker_hits{0};
    std::atomic<uint64_t> l7_packets{0};
    std::atomic<uint64_t> l7_matched{0};
    std::atomic<uint64_t> set_added{0};
    std::atomic<uint64_t> set_refreshed{0};
    std::atomic<uint64_t> set_errors{0};
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
    InterceptProcessor(nfnl::DynamicSetWriter& writer, ConntrackCleanupSink& cleanup,
                       InterceptCounters& counters);

    void set_snapshot(std::shared_ptr<const InterceptSnapshot> snapshot);

    struct DnsDecision {
        bool replace{false};
        const std::vector<uint8_t>* replacement{nullptr};  // valid until the next on_dns_packet()
    };

    // l3 = full IP packet from NFQUEUE.  Never throws.
    DnsDecision on_dns_packet(ByteView l3, std::chrono::steady_clock::time_point deadline,
                              bool replacement_allowed);
    void on_l7_packet(ByteView l3, std::chrono::steady_clock::time_point now);

    std::vector<InterceptEvent> events_since(uint64_t after_seq, std::size_t max) const;
    // Sequence number of the newest event (0 if none yet).
    uint64_t last_event_seq() const;

    static constexpr std::size_t kEventCapacity = 256;

private:
    DnsDecision handle_dns(ByteView l3, std::chrono::steady_clock::time_point deadline,
                           bool replacement_allowed);
    void handle_l7(ByteView l3, std::chrono::steady_clock::time_point now);
    std::shared_ptr<const InterceptSnapshot> snapshot() const;
    void push_event(InterceptEvent&& event);
    void append_add(const InterceptSnapshot& snap, DomainIndex::ListId id, uint8_t family,
                    const std::array<uint8_t, 16>& addr, uint32_t record_ttl_s, bool use_record_ttl);
    void collect_list_names(const InterceptSnapshot& snap, std::vector<std::string>& out) const;

    nfnl::DynamicSetWriter& writer_;
    ConntrackCleanupSink& cleanup_;
    InterceptCounters& counters_;

    mutable std::mutex snapshot_mutex_;
    std::shared_ptr<const InterceptSnapshot> snapshot_;

    mutable std::mutex events_mutex_;
    std::deque<InterceptEvent> events_;
    uint64_t next_seq_{1};

    // Hot-thread-only scratch state.
    dns_wire::ParsedResponse response_;
    std::vector<uint8_t> replacement_;
    std::vector<DomainIndex::ListId> ids_;
    std::vector<DomainIndex::ListId> ids_tmp_;
    std::vector<nfnl::SetAdd> adds_;
    std::vector<nfnl::SetAddResult> results_;
    l7::FlowBuffers flows_;
    l7::QuicCryptoAssembler quic_;
    std::vector<uint8_t> tls_scratch_;
    std::string sni_;
};

} // namespace keen_pbr3
