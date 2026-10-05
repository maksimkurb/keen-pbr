#include "intercept_processor.hpp"

#include "../l7/http_host.hpp"
#include "../l7/tls_client_hello.hpp"
#include "../log/logger.hpp"
#include "../util/byte_view.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>

#include <algorithm>
#include <cerrno>
#include <cstring>

namespace keen_pbr3 {

namespace {

using Clock = std::chrono::steady_clock;

int64_t wall_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string format_ip(uint8_t family, const uint8_t* addr) {
    char buf[INET6_ADDRSTRLEN] = {};
    if (::inet_ntop(family == 6 ? AF_INET6 : AF_INET, addr, buf, sizeof(buf)) == nullptr) {
        return {};
    }
    return buf;
}

int64_t steady_us(std::chrono::steady_clock::time_point t) {
    return std::chrono::duration_cast<std::chrono::microseconds>(t.time_since_epoch()).count();
}

const EventRecord kEmptyRecord{};

void set_domain(EventRecord& event, std::string_view domain) {
    const std::size_t n = std::min(domain.size(), EventRecord::kMaxDomain);
    std::memcpy(event.domain, domain.data(), n);
    event.domain_len = static_cast<uint8_t>(n);
}

void set_client(EventRecord& event, uint8_t family, const uint8_t* addr) {
    event.client_family = family;
    std::memcpy(event.client, addr, family == 6 ? 16 : 4);
}

void add_ip(EventRecord& event, uint8_t family, const uint8_t* addr) {
    if (event.ip_count >= EventRecord::kMaxIps) {
        if (event.ips_overflow < UINT8_MAX) ++event.ips_overflow;
        return;
    }
    event.ip_family[event.ip_count] = family;
    std::memcpy(event.ips[event.ip_count], addr, family == 6 ? 16 : 4);
    ++event.ip_count;
}

using keen_pbr3::load_be16;
using keen_pbr3::load_be32;

uint32_t timeout_for(const InterceptSnapshot& snap, const InterceptListTarget& target,
                     uint32_t record_ttl_s, bool use_record_ttl) {
    if (target.min_ttl_s == 0) return 0;
    const uint32_t wanted = use_record_ttl ? std::max(record_ttl_s, target.min_ttl_s) : target.min_ttl_s;
    return std::clamp<uint32_t>(wanted, 1, std::max<uint32_t>(snap.max_ttl_s, 1));
}

void bump(std::atomic<uint64_t>& counter, uint64_t by = 1) {
    counter.fetch_add(by, std::memory_order_relaxed);
}

uint32_t micros_since(Clock::time_point from, Clock::time_point to) {
    return static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(to - from).count());
}

int64_t signed_micros(Clock::time_point from, Clock::time_point to) {
    return std::chrono::duration_cast<std::chrono::microseconds>(to - from).count();
}

} // namespace

static_assert(InterceptProcessor::kEventCapacity == 512, "EventRing capacity");

const char* timeout_cause_name(TimeoutCause cause) {
    switch (cause) {
    case TimeoutCause::none: return "none";
    case TimeoutCause::budget_spent_by_batch: return "budget_spent_by_batch";
    case TimeoutCause::admission_blocked: return "admission_blocked";
    case TimeoutCause::own_write_slow: return "own_write_slow";
    case TimeoutCause::late_batch_full: return "late_batch_full";
    case TimeoutCause::other: return "other";
    }
    return "other";
}

InterceptProcessor::InterceptProcessor(nfnl::DynamicSetWriter& writer, ConntrackCleanupSink& cleanup,
                                       InterceptCounters& counters)
    : writer_(writer), cleanup_(cleanup), counters_(counters) {
    late_adds_.reserve(kLateBatchCapacity);
    late_events_.reserve(kLateBatchCapacity);
    flush_adds_.reserve(kLateBatchCapacity);
    flush_results_.reserve(kLateBatchCapacity);
    late_slots_.reserve(kLateBatchCapacity);
    flush_slots_.reserve(kLateBatchCapacity);
    refresh_adds_.reserve(kLateBatchCapacity);
    refresh_slots_.reserve(kLateBatchCapacity);
    refresh_snaps_.reserve(kLateBatchCapacity);
    refresh_clients_.reserve(kLateBatchCapacity);
    refresh_client_valid_.reserve(kLateBatchCapacity);
    flush_refresh_idx_.reserve(kLateBatchCapacity);
}

Clock::time_point InterceptProcessor::clock_now() const {
    return clock_ ? clock_() : Clock::now();
}

int64_t InterceptProcessor::to_ms(Clock::time_point t) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(t.time_since_epoch()).count();
}

void InterceptProcessor::set_clock(std::function<Clock::time_point()> clock) {
    clock_ = std::move(clock);
}

void InterceptProcessor::invalidate_set_cache() {
    cache_.clear();
    sync_cache_gauge();
}

void InterceptProcessor::sync_cache_gauge() {
    counters_.set_cache_entries.store(cache_.size(), std::memory_order_relaxed);
}

void InterceptProcessor::note_written(uint16_t slot, const nfnl::SetAdd& add, int64_t at_ms,
                                      uint64_t epoch) {
    cache_.record(slot, add.family, add.addr, add.timeout_s, at_ms, epoch);
}

bool InterceptProcessor::queue_refresh(const std::shared_ptr<const InterceptSnapshot>& snap,
                                       const nfnl::SetAdd& add, uint16_t slot) {
    if (refresh_adds_.size() >= kLateBatchCapacity) {
        // The element is still present; a later query refreshes it.
        bump(counters_.refresh_dropped);
        return false;
    }
    refresh_adds_.push_back(add);  // within reserved capacity
    refresh_clients_.push_back(dns_client_);
    refresh_client_valid_.push_back(dns_client_valid_ ? 1 : 0);
    refresh_slots_.push_back(slot);
    refresh_snaps_.push_back(snap);
    bump(counters_.dns_refresh_deferred);
    return true;
}

void InterceptProcessor::classify_adds(const std::shared_ptr<const InterceptSnapshot>& snap,
                                       int64_t now_ms, bool queue_refreshes,
                                       EventRecord& event) {
    std::size_t kept = 0;
    uint64_t hits = 0;
    for (std::size_t i = 0; i < adds_.size(); ++i) {
        const nfnl::SetAdd& add = adds_[i];
        const SetElementCache::Lookup found = cache_.lookup(add_slots_[i], add.family, add.addr, now_ms);
        // L7 (queue_refreshes == false) only skips trusted entries; a Stale one is rewritten.
        const bool hit = found.state == SetElementCache::State::Fresh ||
                         (queue_refreshes && found.state == SetElementCache::State::Stale);
        if (!hit) {
            if (kept != i) {
                adds_[kept] = adds_[i];
                add_slots_[kept] = add_slots_[i];
            }
            ++kept;
            continue;
        }
        ++hits;
        if (!queue_refreshes) continue;
        bool wants = found.state == SetElementCache::State::Stale;
        if (!wants) {
            // Fresh entry: refresh only if remaining lifetime < half of the desired timeout,
            // or if desired timeout is permanent (0) while cached is finite.
            const int64_t remaining_ms = found.expires_at_ms - now_ms;
            const bool cached_permanent = found.expires_at_ms == SetElementCache::kPermanent;
            const bool desired_permanent = add.timeout_s == 0;
            if (SetElementCache::needs_refresh(remaining_ms, add.timeout_s, cached_permanent, desired_permanent)) {
                wants = true;
            } else {
                ++event.refresh_skipped;
                bump(counters_.refresh_skipped);
            }
        }
        if (wants && queue_refresh(snap, add, add_slots_[i])) ++event.deferred_refresh;
    }
    const uint64_t misses = kept;
    adds_.resize(kept);
    add_slots_.resize(kept);
    event.cache_hits = static_cast<uint32_t>(hits);
    bump(counters_.set_cache_hits, hits);
    bump(counters_.set_cache_misses, misses);
}

void InterceptProcessor::set_snapshot(std::shared_ptr<const InterceptSnapshot> snapshot) {
    // One slot per distinct dynamic-set name; built once per snapshot so the
    // hot path resolves a slot with an index, never a string lookup.
    std::shared_ptr<SlotTable> table;
    if (snapshot) {
        std::vector<std::string_view> names;
        for (const InterceptListTarget& target : snapshot->targets) {
            if (!target.set_v4.empty()) names.emplace_back(target.set_v4);
            if (!target.set_v6.empty()) names.emplace_back(target.set_v6);
        }
        std::sort(names.begin(), names.end());
        names.erase(std::unique(names.begin(), names.end()), names.end());
        const auto slot_of = [&names](const std::string& name) -> uint16_t {
            if (name.empty()) return SetElementCache::kNoSlot;
            const auto it = std::lower_bound(names.begin(), names.end(), std::string_view(name));
            const std::size_t idx = static_cast<std::size_t>(it - names.begin());
            return idx < SetElementCache::kNoSlot ? static_cast<uint16_t>(idx)
                                                  : SetElementCache::kNoSlot;
        };
        table = std::make_shared<SlotTable>();
        table->by_target.reserve(snapshot->targets.size());
        for (const InterceptListTarget& target : snapshot->targets) {
            table->by_target.push_back({slot_of(target.set_v4), slot_of(target.set_v6)});
        }
    }
    {
        std::lock_guard<std::mutex> lock(snapshot_mutex_);
        if (table) {
            // Events carry this generation; events_since() resolves their list
            // ids with the name table registered under it.
            table->generation = next_generation_++;
            std::lock_guard<std::mutex> names_lock(names_mutex_);
            name_tables_.push_back({table->generation, snapshot->index->list_names()});
            while (name_tables_.size() > kNameGenerations) name_tables_.pop_front();
        }
        snapshot_ = std::move(snapshot);
        slots_ = std::move(table);
        // A snapshot change means the sets may have been recreated: nothing
        // cached about the old ones can be trusted.  Cleared under the snapshot
        // lock so a reader that captured the epoch first can never record
        // against the new snapshot.
        cache_.clear();
    }
    sync_cache_gauge();
}

void InterceptProcessor::set_l7_submitter(L7Submitter submitter) {
    std::lock_guard<std::mutex> lock(snapshot_mutex_);
    l7_submitter_ = std::move(submitter);
}

void InterceptProcessor::set_writer_callbacks(WriterAdmission dns_admission,
                                              WriterRelease dns_release,
                                              WriterAdmission l7_admission,
                                              WriterRelease l7_release) {
    std::lock_guard<std::mutex> lock(snapshot_mutex_);
    dns_admission_ = std::move(dns_admission);
    dns_release_ = std::move(dns_release);
    l7_admission_ = std::move(l7_admission);
    l7_release_ = std::move(l7_release);
}

std::shared_ptr<const InterceptSnapshot> InterceptProcessor::snapshot() const {
    std::lock_guard<std::mutex> lock(snapshot_mutex_);
    return snapshot_;
}

std::shared_ptr<const InterceptSnapshot> InterceptProcessor::snapshot_and_slots(
    std::shared_ptr<const SlotTable>& slots) const {
    std::lock_guard<std::mutex> lock(snapshot_mutex_);
    slots = slots_;
    return snapshot_;
}

bool InterceptProcessor::snapshot_is_current(
    const std::shared_ptr<const InterceptSnapshot>& snapshot) const {
    std::lock_guard<std::mutex> lock(snapshot_mutex_);
    return snapshot_ == snapshot;
}

void InterceptProcessor::bump_timeout_cause(TimeoutCause cause) {
    switch (cause) {
    case TimeoutCause::budget_spent_by_batch: bump(counters_.dns_timeout_budget_spent_by_batch); break;
    case TimeoutCause::admission_blocked: bump(counters_.dns_timeout_admission_blocked); break;
    case TimeoutCause::own_write_slow: bump(counters_.dns_timeout_own_write_slow); break;
    case TimeoutCause::late_batch_full: bump(counters_.dns_timeout_late_batch_full); break;
    case TimeoutCause::none:
    case TimeoutCause::other: bump(counters_.dns_timeout_other); break;
    }
}

void InterceptProcessor::set_round_batch_size(uint64_t first_seq, uint32_t batch_size) {
    // Runs on the producer thread: rewrites its own slots in place (seqlock
    // update); events already overwritten or not timed are left alone.
    const uint64_t last = ring_.last_seq();
    for (uint64_t seq = first_seq; seq <= last; ++seq) {
        EventRecord rec;
        if (ring_.peek(seq, rec) != EventRing::Peek::Ok || rec.batch_pos < 0 ||
            rec.batch_size == batch_size) {
            continue;
        }
        ring_.update(seq, [batch_size](EventRecord& r) {
            if (r.batch_pos < 0) return false;
            r.batch_size = batch_size;
            return true;
        });
    }
}

void InterceptProcessor::push_event(EventRecord& event) {
    if (event.ts_steady_us == 0) event.ts_steady_us = steady_us(Clock::now());
    ring_.push(event);
}

EventRecord& InterceptProcessor::stage_event(InterceptSource source) {
    pending_ = kEmptyRecord;
    pending_.source = source;
    pending_valid_ = true;
    return pending_;
}

void InterceptProcessor::commit_dns_event() {
    if (!pending_valid_) return;
    pending_valid_ = false;
    push_event(pending_);
}

InterceptEvent InterceptProcessor::format_event(const EventRecord& record) const {
    InterceptEvent event;
    event.seq = record.seq;
    // The record keeps a steady timestamp; the wall time is derived from the
    // event's age so the hot path never reads the wall clock.
    event.ts_ms = wall_ms() - (steady_us(Clock::now()) - record.ts_steady_us) / 1000;
    event.source = record.source;
    if (record.client_family != 0) event.client_ip = format_ip(record.client_family, record.client);
    event.domain.assign(record.domain, record.domain_len);
    event.ips.reserve(record.ip_count);
    for (std::size_t i = 0; i < record.ip_count; ++i) {
        event.ips.push_back(format_ip(record.ip_family[i], record.ips[i]));
    }
    event.lists.reserve(record.list_count);
    {
        // Names of generations no longer kept are shown as "?".
        std::lock_guard<std::mutex> lock(names_mutex_);
        const NameTable* table = nullptr;
        for (const NameTable& candidate : name_tables_) {
            if (candidate.generation == record.generation) table = &candidate;
        }
        for (std::size_t i = 0; i < record.list_count; ++i) {
            const uint16_t id = record.list_ids[i];
            if (table != nullptr && id < table->names.size()) {
                event.lists.push_back(table->names[id]);
            } else {
                event.lists.emplace_back("?");
            }
        }
    }
    event.added = record.added;
    event.refreshed = record.refreshed;
    event.errors = record.errors;
    event.hold_us = record.hold_us;
    event.parse_us = record.parse_us;
    event.set_write_us = record.set_write_us;
    event.timed_out = record.timed_out;
    event.late_write = record.late_write;
    event.cache_hits = record.cache_hits;
    event.deferred_refresh = record.deferred_refresh;
    event.refresh_skipped = record.refresh_skipped;
    event.batch_pos = record.batch_pos;
    event.batch_size = record.batch_size;
    event.queue_wait_us = record.queue_wait_us;
    event.budget_left_us = record.budget_left_us;
    event.admission_wait_us = record.admission_wait_us;
    event.write_elements = record.write_elements;
    event.late_batch_elements = record.late_batch_elements;
    event.write_errno = record.write_errno;
    event.timeout_cause = record.timeout_cause;
    return event;
}

std::vector<InterceptEvent> InterceptProcessor::events_since(uint64_t after_seq,
                                                             std::size_t max) const {
    std::vector<EventRecord> records;
    ring_.read(after_seq, max, records);
    std::vector<InterceptEvent> out;
    out.reserve(records.size());
    for (const EventRecord& record : records) out.push_back(format_event(record));
    return out;
}

void InterceptProcessor::log_hold_timeouts() {
    // Every hold timeout may have let a client connect before its address was
    // routed, so it is worth a warning (timeouts only, never per packet).
    if (!Logger::instance().is_enabled(LogLevel::warn)) {
        logged_seq_ = std::max(logged_seq_, ring_.last_seq());
        return;
    }
    std::vector<EventRecord> records;
    ring_.read(logged_seq_, kEventCapacity, records);
    for (const EventRecord& event : records) {
        logged_seq_ = event.seq;
        if (!event.timed_out) continue;
        Logger::instance().warn(
            "intercept: dns hold timeout cause={} domain={} batch_pos={} queue_wait={}us "
            "budget_left={}us parse={}us admission_wait={}us write={}us elements={} "
            "late_elements={} errno={}",
            timeout_cause_name(event.timeout_cause),
            log_escape(std::string_view(event.domain, event.domain_len)), event.batch_pos,
            event.queue_wait_us, event.budget_left_us, event.parse_us, event.admission_wait_us,
            event.set_write_us, event.write_elements, event.late_batch_elements,
            event.write_errno);
    }
}

void InterceptProcessor::record_list_ids(const SlotTable& slots, EventRecord& event) const {
    event.generation = slots.generation;
    for (DomainIndex::ListId id : ids_) {
        if (event.list_count < EventRecord::kMaxLists) {
            event.list_ids[event.list_count++] = id;
        } else if (event.lists_overflow < UINT8_MAX) {
            ++event.lists_overflow;
        }
    }
}

void InterceptProcessor::append_add(const InterceptSnapshot& snap, const SlotTable& slots,
                                    DomainIndex::ListId id, uint8_t family,
                                    const std::array<uint8_t, 16>& addr,
                                    uint32_t record_ttl_s, bool use_record_ttl) {
    if (id >= snap.targets.size()) return;
    const InterceptListTarget& target = snap.targets[id];
    const std::string& set = family == 6 ? target.set_v6 : target.set_v4;
    if (set.empty()) return;
    const uint32_t timeout = timeout_for(snap, target, record_ttl_s, use_record_ttl);
    const std::size_t addr_len = family == 6 ? 16 : 4;
    for (nfnl::SetAdd& existing : adds_) {
        if (existing.family == family && existing.set_name == set &&
            std::memcmp(existing.addr.data(), addr.data(), addr_len) == 0) {
            existing.timeout_s = (existing.timeout_s == 0 || timeout == 0)
                                     ? 0
                                     : std::max(existing.timeout_s, timeout);
            return;
        }
    }
    nfnl::SetAdd add;
    add.set_name = set;
    add.family = family;
    add.addr = addr;
    add.timeout_s = timeout;
    adds_.push_back(add);
    add_slots_.push_back(id < slots.by_target.size() ? slots.by_target[id][family == 6 ? 1 : 0]
                                                     : SetElementCache::kNoSlot);
}

InterceptProcessor::DnsDecision InterceptProcessor::process_dns_packet(
    ByteView l3, const DnsRound& round, bool replacement_allowed) {
    pending_valid_ = false;
    try {
        return handle_dns(l3, round, replacement_allowed);
    } catch (const std::exception& e) {
        Logger::instance().debug("intercept: DNS packet handling failed: {}", e.what());
    } catch (...) {  // NOLINT(bugprone-empty-catch): packet handling must never throw into the netfilter loop
    }
    pending_valid_ = false;
    return {};
}

InterceptProcessor::DnsDecision InterceptProcessor::on_dns_packet(
    ByteView l3, const DnsRound& round, bool replacement_allowed) {
    const DnsDecision decision = process_dns_packet(l3, round, replacement_allowed);
    commit_dns_event();
    return decision;
}

void InterceptProcessor::on_l7_packet(ByteView l3, Clock::time_point now) {
    try {
        handle_l7(l3, now);
    } catch (const std::exception& e) {
        Logger::instance().debug("intercept: L7 packet handling failed: {}", e.what());
    } catch (...) {  // NOLINT(bugprone-empty-catch): packet handling must never throw into the netfilter loop
    }
}

InterceptProcessor::DnsDecision InterceptProcessor::handle_dns(ByteView l3, const DnsRound& round,
                                                               bool replacement_allowed) {
    const Clock::time_point deadline = round.deadline;
    const Clock::time_point started = Clock::now();
    // Epoch BEFORE the snapshot: a clear in between only makes records fail.
    const uint64_t cache_epoch = cache_.epoch();
    std::shared_ptr<const SlotTable> slots;
    const auto snap = snapshot_and_slots(slots);
    if (!snap || !snap->index || !slots) return {};

    const auto layout = dns_wire::parse_packet_layout(l3);
    if (!layout || (layout->l4_proto != IPPROTO_UDP && layout->l4_proto != IPPROTO_TCP)) return {};
    if (layout->payload_offset > l3.size() || layout->payload_len > l3.size() - layout->payload_offset) {
        return {};
    }
    const bool udp = layout->l4_proto == IPPROTO_UDP;
    const ByteView payload(l3.data() + layout->payload_offset, layout->payload_len);
    if (!udp) {
        // FIN, RST or SYN ends (or restarts) the stream: drop its reassembly state.
        const std::size_t l4_offset = layout->l3_header_len;
        const std::size_t alen = layout->ip_version == 6 ? 16 : 4;
        const std::size_t src_off = layout->ip_version == 6 ? 8 : 12;
        const std::size_t dst_off = layout->ip_version == 6 ? 24 : 16;
        if (layout->l4_header_len >= 20 && l4_offset + 20 <= l3.size() &&
            dst_off + alen <= l3.size() && (l3.data()[l4_offset + 13] & 0x07) != 0) {
            l7::FlowKey key;
            key.family = layout->ip_version;
            std::memcpy(key.src.data(), l3.data() + src_off, alen);
            std::memcpy(key.dst.data(), l3.data() + dst_off, alen);
            key.sport = load_be16(l3.data() + l4_offset);
            key.dport = load_be16(l3.data() + l4_offset + 2);
            tcp_reassembly_.close(key);
        }
    }
    if (!udp && payload.size() == 0) return {};  // bare ACK/FIN
    {
        // The reply's destination is the asker; conntrack cleanup is scoped to it.
        const std::size_t client_offset = layout->ip_version == 6 ? 24 : 16;
        const std::size_t client_length = layout->ip_version == 6 ? 16 : 4;
        dns_client_.fill(0);
        dns_client_valid_ = client_offset + client_length <= l3.size();
        if (dns_client_valid_) std::memcpy(dns_client_.data(), l3.data() + client_offset, client_length);
    }
    bump(counters_.dns_packets);

    if (udp) {
        return handle_dns_message(l3, *layout, payload, /*udp=*/true, round, started, cache_epoch,
                                  snap, slots, replacement_allowed);
    }

    // TCP: feed the segment to the per-flow reassembler.  A segment that does
    // not complete a message (first of several, duplicate, out of order) gets
    // no work and is accepted at once; only the segment that completes a message
    // is held while the set write runs.
    const std::size_t l4_offset = layout->l3_header_len;
    if (layout->l4_header_len < 20 || l4_offset + 20 > l3.size()) return {};
    const uint8_t* l4 = l3.data() + l4_offset;
    const std::size_t alen = layout->ip_version == 6 ? 16 : 4;
    const std::size_t src_off = layout->ip_version == 6 ? 8 : 12;
    const std::size_t dst_off = layout->ip_version == 6 ? 24 : 16;
    if (dst_off + alen > l3.size()) return {};
    l7::FlowKey key;
    key.family = layout->ip_version;
    std::memcpy(key.src.data(), l3.data() + src_off, alen);
    std::memcpy(key.dst.data(), l3.data() + dst_off, alen);
    key.sport = load_be16(l4);
    key.dport = load_be16(l4 + 2);
    tcp_reassembly_.feed(key, load_be32(l4 + 4), payload, started, tcp_messages_);
    if (tcp_messages_.count == 0) {
        bump(counters_.dns_tcp_partial);
        return {};
    }
    DnsDecision decision;
    for (std::size_t i = 0; i < tcp_messages_.count; ++i) {
        // Earlier messages of the segment publish their event now; the last
        // one is published by the caller after the verdict.
        if (i > 0) commit_dns_event();
        const DnsDecision one = handle_dns_message(
            l3, *layout, tcp_messages_.view[i], /*udp=*/false, round,
            i == 0 ? started : Clock::now(), cache_epoch, snap, slots, false);
        decision.late_write = decision.late_write || one.late_write;
    }
    return decision;
}

InterceptProcessor::DnsDecision InterceptProcessor::handle_dns_message(
    ByteView l3, const dns_wire::PacketLayout& layoutref, ByteView message, bool udp,
    const DnsRound& round, Clock::time_point started, uint64_t cache_epoch,
    const std::shared_ptr<const InterceptSnapshot>& snap,
    const std::shared_ptr<const SlotTable>& slots, bool replacement_allowed) {
    const Clock::time_point deadline = round.deadline;
    const dns_wire::PacketLayout* layout = &layoutref;

    response_.clear();
    if (!dns_wire::parse_response(message, response_)) {
        bump(counters_.dns_parse_errors);
        return {};
    }
    const Clock::time_point parsed_at = Clock::now();

    DnsDecision decision;
    if (dns_wire::is_marker_name(response_.qname, snap->marker_domain)) {
        bump(counters_.marker_hits);
        if (udp && replacement_allowed) {
            auto built = dns_wire::build_marker_packet(l3, *layout, response_, snap->marker_ipv4);
            if (built) {
                replacement_ = std::move(*built);
                decision.replace = true;
                decision.replacement = &replacement_;
            }
        }
        EventRecord& event = stage_event(InterceptSource::marker);
        event.ts_steady_us = steady_us(started);
        if (dns_client_valid_) set_client(event, layout->ip_version, dns_client_.data());
        set_domain(event, response_.qname);
        event.hold_us = micros_since(started, Clock::now());
        return decision;
    }

    const auto add_dns_observation = [&](EventRecord& event) {
        event.ts_steady_us = steady_us(started);
        if (dns_client_valid_) set_client(event, layout->ip_version, dns_client_.data());
        set_domain(event, response_.qname);
        event.parse_us = micros_since(started, parsed_at);
        event.batch_pos = static_cast<int32_t>(round.batch_pos);
        event.queue_wait_us = signed_micros(round.woke, started);
        event.budget_left_us = signed_micros(started, deadline);
        for (const dns_wire::AddressRecord& rec : response_.addresses) {
            add_ip(event, rec.family, rec.addr.data());
        }
    };

    if (response_.rcode != 0 || response_.addresses.empty()) {
        add_dns_observation(stage_event(InterceptSource::dns));
        return {};
    }

    snap->index->lookup(response_.qname, ids_);
    for (const std::string& name : response_.cname_chain) {
        if (name == response_.qname) continue;
        snap->index->lookup(name, ids_tmp_);
        if (ids_tmp_.empty()) continue;
        ids_.insert(ids_.end(), ids_tmp_.begin(), ids_tmp_.end());
    }
    if (ids_.empty()) {
        add_dns_observation(stage_event(InterceptSource::dns));
        return {};
    }
    std::sort(ids_.begin(), ids_.end());
    ids_.erase(std::unique(ids_.begin(), ids_.end()), ids_.end());

    bump(counters_.dns_matched);

    adds_.clear();
    add_slots_.clear();
    for (const dns_wire::AddressRecord& rec : response_.addresses) {
        for (DomainIndex::ListId id : ids_) {
            append_add(*snap, *slots, id, rec.family, rec.addr, rec.ttl, true);
        }
    }

    EventRecord& event = stage_event(InterceptSource::dns);
    add_dns_observation(event);
    record_list_ids(*slots, event);

    // Only addresses the cache does not know block the verdict.  Cached ones
    // are already routed; their timeout refresh (if due) runs after the verdict.
    if (!adds_.empty()) classify_adds(snap, to_ms(clock_now()), /*queue_refreshes=*/true, event);

    if (!adds_.empty()) {
        results_.assign(adds_.size(), nfnl::SetAddResult::Error);
        // The write is deferred (verdict first) when the deadline has passed.
        // This read doubles as the start of the admission wait, the next one
        // as its end and the start of the write: no clock read is spent on
        // timing a path that did not run.
        const Clock::time_point checked_at = Clock::now();
        bool defer = checked_at >= deadline;
        TimeoutCause cause = defer ? TimeoutCause::budget_spent_by_batch : TimeoutCause::other;
        bool after_timeout = false;
        bool ok = false;
        bool write_attempted = false;
        int64_t write_started_ms = 0;
        if (!defer) {
            WriterAdmission dns_admission;
            WriterRelease dns_release;
            {
                std::lock_guard<std::mutex> lock(snapshot_mutex_);
                dns_admission = dns_admission_;
                dns_release = dns_release_;
            }
            const bool admitted = !dns_admission || dns_admission();
            const Clock::time_point admitted_at = Clock::now();
            event.admission_wait_us = micros_since(checked_at, admitted_at);
            if (admitted) {
                if (admitted_at >= deadline) {
                    if (dns_release) dns_release();
                    defer = true;
                    cause = TimeoutCause::admission_blocked;
                } else if (!snapshot_is_current(snap)) {
                    if (dns_release) dns_release();
                    event.errors = static_cast<uint32_t>(adds_.size());
                    bump(counters_.set_errors, event.errors);
                    event.hold_us = micros_since(started, Clock::now());
                    return decision;
                } else {
                    const auto remaining_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        (deadline - admitted_at) + std::chrono::microseconds(999));
                    const int budget_ms = static_cast<int>(std::max<int64_t>(1, remaining_ms.count()));
                    write_attempted = true;
                    event.write_elements = static_cast<uint32_t>(adds_.size());
                    write_started_ms = to_ms(clock_now());
                    try {
                        // One exclusive add: existing elements come back as Exists.
                        ok = writer_.add_new(adds_.data(), results_.data(), adds_.size(), budget_ms);
                    } catch (const std::exception& e) {
                        Logger::instance().debug("intercept: set writer failed: {}", e.what());
                    } catch (...) {
                        if (dns_release) dns_release();
                        throw;
                    }
                    event.set_write_us = micros_since(admitted_at, Clock::now());
                    counters_.dns_write_latency.record(event.set_write_us, event.write_elements);
                    if (!ok) event.write_errno = writer_.last_errno();
                    if (dns_release) dns_release();
                }
            }
        }
        if (write_attempted && !ok && writer_.last_errno() == ENOENT) {
            invalidate_set_cache();  // a set is missing: everything cached is suspect
        }
        // Confirmed results feed the cache; an element that already existed
        // (Exists) is refreshed after the verdict.
        if (write_attempted) {
            for (std::size_t i = 0; i < adds_.size(); ++i) {
                switch (results_[i]) {
                case nfnl::SetAddResult::Added:
                case nfnl::SetAddResult::Refreshed:
                    note_written(add_slots_[i], adds_[i], write_started_ms, cache_epoch);
                    break;
                case nfnl::SetAddResult::Exists:
                    if (queue_refresh(snap, adds_[i], add_slots_[i])) ++event.deferred_refresh;
                    break;
                default: break;
                }
            }
            sync_cache_gauge();
        }
        if (!defer && write_attempted && !ok && writer_.last_errno() == ETIMEDOUT) {
            // ETIMEDOUT means "unknown": the kernel may still apply the batch.
            // Keep what was confirmed, retry only the unconfirmed entries late.
            defer = true;
            after_timeout = true;
            cause = TimeoutCause::own_write_slow;
        }
        if (defer) {
            std::size_t kept = 0;
            for (std::size_t i = 0; i < adds_.size(); ++i) {
                switch (results_[i]) {
                case nfnl::SetAddResult::Added: ++event.added; break;
                case nfnl::SetAddResult::Refreshed:
                case nfnl::SetAddResult::Exists: ++event.refreshed; break;
                default:
                    if (kept != i) {
                        adds_[kept] = adds_[i];
                        add_slots_[kept] = add_slots_[i];
                    }
                    ++kept;
                    break;
                }
            }
            adds_.resize(kept);
            add_slots_.resize(kept);
            bump(counters_.set_added, event.added);
            bump(counters_.set_refreshed, event.refreshed);
            if (kept > 0) {
                decision.late_write = defer_late_write(snap, started, after_timeout, cause);
                return decision;
            }
            // Everything was confirmed in time despite the late ETIMEDOUT mark.
        } else {
            for (nfnl::SetAddResult r : results_) {
                switch (r) {
                case nfnl::SetAddResult::Added: ++event.added; break;
                case nfnl::SetAddResult::Refreshed:
                case nfnl::SetAddResult::Exists: ++event.refreshed; break;
                default: ++event.errors; break;
                }
            }
            bump(counters_.set_added, event.added);
            bump(counters_.set_refreshed, event.refreshed);
            bump(counters_.set_errors, event.errors);
        }
    }
    event.hold_us = micros_since(started, Clock::now());
    return decision;
}

bool InterceptProcessor::defer_late_write(const std::shared_ptr<const InterceptSnapshot>& snap,
                                          Clock::time_point started, bool after_timeout,
                                          TimeoutCause cause) {
    EventRecord& event = pending_;  // the staged event of the packet being handled
    event.timed_out = true;
    event.late_write = true;
    event.timeout_cause = cause;
    event.hold_us = static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - started).count());
    bump(counters_.dns_hold_timeouts);
    const std::size_t n = adds_.size();
    if (late_events_.size() >= kLateBatchCapacity ||
        late_adds_.size() + n > kLateBatchCapacity) {
        // Pending batch full: drop these adds, never grow the batch.
        event.errors += static_cast<uint32_t>(n);
        bump(counters_.set_errors, n);
        bump(counters_.dns_late_write_errors, n);
        event.timeout_cause = TimeoutCause::late_batch_full;
        bump_timeout_cause(event.timeout_cause);
        return false;  // stays staged: published after the verdict
    }
    LateEvent le;
    le.event = event;
    pending_valid_ = false;  // published by flush_late_writes() instead
    le.snapshot = snap;  // adds hold string_views into this snapshot
    le.addresses = response_.addresses;
    le.client = dns_client_;
    le.client_valid = dns_client_valid_;
    le.first = late_adds_.size();
    le.count = n;
    le.after_timeout = after_timeout;
    bump_timeout_cause(cause);
    late_adds_.insert(late_adds_.end(), adds_.begin(), adds_.end());  // within reserved capacity
    late_slots_.insert(late_slots_.end(), add_slots_.begin(), add_slots_.end());
    late_events_.push_back(std::move(le));
    return true;
}

void InterceptProcessor::flush_late_writes() {
    if (late_events_.empty()) {
        flush_refreshes();
        return;
    }
    try {
        const Clock::time_point now = Clock::now();
        const int budget_ms = now < late_backoff_until_ ? kLateBackoffBudgetMs : kLateWriteBudgetMs;

        WriterAdmission dns_admission;
        WriterRelease dns_release;
        {
            std::lock_guard<std::mutex> lock(snapshot_mutex_);
            dns_admission = dns_admission_;
            dns_release = dns_release_;
        }
        const bool admitted = !dns_admission || dns_admission();

        // Per-flush snapshot check: stale events are dropped as errors.
        flush_adds_.clear();
        flush_slots_.clear();
        for (LateEvent& le : late_events_) {
            le.snapshot_ok = admitted && snapshot_is_current(le.snapshot);
            if (le.snapshot_ok) {
                flush_adds_.insert(flush_adds_.end(), late_adds_.begin() + static_cast<std::ptrdiff_t>(le.first),
                                   late_adds_.begin() + static_cast<std::ptrdiff_t>(le.first + le.count));
                flush_slots_.insert(flush_slots_.end(),
                                    late_slots_.begin() + static_cast<std::ptrdiff_t>(le.first),
                                    late_slots_.begin() + static_cast<std::ptrdiff_t>(le.first + le.count));
            }
        }
        flush_results_.assign(flush_adds_.size(), nfnl::SetAddResult::Error);
        int err = 0;
        uint32_t write_us = 0;
        const uint64_t cache_epoch = cache_.epoch();  // before the write begins
        const int64_t write_started_ms = to_ms(clock_now());
        if (!flush_adds_.empty()) {
            const Clock::time_point write_started = Clock::now();
            try {
                writer_.add(flush_adds_.data(), flush_results_.data(), flush_adds_.size(), budget_ms);
                err = writer_.last_errno();
            } catch (const std::exception& e) {
                Logger::instance().debug("intercept: late set write failed: {}", e.what());
            } catch (...) {
                if (admitted && dns_release) dns_release();
                late_adds_.clear();
                late_slots_.clear();
                late_events_.clear();
                throw;
            }
            write_us = static_cast<uint32_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - write_started)
                    .count());
            counters_.late_write_latency.record(write_us, flush_adds_.size());
        }
        if (admitted && dns_release) dns_release();
        if (err == ETIMEDOUT) {
            late_backoff_until_ = Clock::now() + std::chrono::milliseconds(kLateBackoffWindowMs);
        }
        if (err == ENOENT) invalidate_set_cache();
        for (std::size_t i = 0; i < flush_adds_.size(); ++i) {
            if (flush_results_[i] == nfnl::SetAddResult::Added ||
                flush_results_[i] == nfnl::SetAddResult::Refreshed) {
                note_written(flush_slots_[i], flush_adds_[i], write_started_ms, cache_epoch);
            }
        }
        sync_cache_gauge();

        std::size_t pos = 0;
        for (LateEvent& le : late_events_) {
            EventRecord& event = le.event;
            uint32_t added = 0, refreshed = 0, errors = 0;
            if (le.snapshot_ok) {
                for (std::size_t i = 0; i < le.count; ++i) {
                    switch (flush_results_[pos + i]) {
                    case nfnl::SetAddResult::Added: ++added; break;
                    case nfnl::SetAddResult::Refreshed: ++refreshed; break;
                    default: ++errors; break;
                    }
                }
                pos += le.count;
                bump(counters_.dns_late_writes);
                event.set_write_us += write_us;
                event.late_batch_elements = static_cast<uint32_t>(flush_adds_.size());
                if (err != 0) event.write_errno = err;
            } else {
                errors = static_cast<uint32_t>(le.count);
            }
            event.added += added;
            event.refreshed += refreshed;
            event.errors += errors;
            bump(counters_.set_added, added);
            bump(counters_.set_refreshed, refreshed);
            bump(counters_.set_errors, errors);
            bump(counters_.dns_late_write_errors, errors);
            // The client may already be connecting: purge stale flows to these
            // IPs.  After an ETIMEDOUT first attempt an "existing" element may
            // have been added by it, so any success there also warrants a purge.
            if (le.snapshot_ok && le.client_valid &&
                (added > 0 || (le.after_timeout && (refreshed > 0 || (errors > 0 && err == ETIMEDOUT))))) {
                for (const dns_wire::AddressRecord& rec : le.addresses) {
                    cleanup_.request(rec.family, le.client, rec.addr);
                }
            }
            push_event(event);
        }
    } catch (const std::exception& e) {
        Logger::instance().debug("intercept: late DNS write handling failed: {}", e.what());
    } catch (...) {  // NOLINT(bugprone-empty-catch): packet handling must never throw into the netfilter loop
    }
    late_adds_.clear();
    late_slots_.clear();
    late_events_.clear();
    flush_refreshes();
}

void InterceptProcessor::flush_refreshes() {
    if (refresh_adds_.empty()) return;
    try {
        const int budget_ms =
            clock_now() < late_backoff_until_ ? kLateBackoffBudgetMs : kLateWriteBudgetMs;
        WriterAdmission dns_admission;
        WriterRelease dns_release;
        {
            std::lock_guard<std::mutex> lock(snapshot_mutex_);
            dns_admission = dns_admission_;
            dns_release = dns_release_;
        }
        const bool admitted = !dns_admission || dns_admission();

        // Entries of a replaced snapshot are dropped: the element is not lost,
        // the next query for the domain handles it.
        flush_adds_.clear();
        flush_slots_.clear();
        flush_refresh_idx_.clear();
        const InterceptSnapshot* checked = nullptr;
        bool checked_ok = false;
        for (std::size_t i = 0; admitted && i < refresh_adds_.size(); ++i) {
            if (refresh_snaps_[i].get() != checked) {
                checked = refresh_snaps_[i].get();
                checked_ok = snapshot_is_current(refresh_snaps_[i]);
            }
            if (!checked_ok) continue;
            flush_adds_.push_back(refresh_adds_[i]);
            flush_slots_.push_back(refresh_slots_[i]);
            flush_refresh_idx_.push_back(i);
        }
        if (!flush_adds_.empty()) {
            flush_results_.assign(flush_adds_.size(), nfnl::SetAddResult::Error);
            const uint64_t cache_epoch = cache_.epoch();
            const int64_t write_started_ms = to_ms(clock_now());
            int err = 0;
            try {
                writer_.refresh(flush_adds_.data(), flush_results_.data(), flush_adds_.size(),
                                budget_ms);
                err = writer_.last_errno();
            } catch (const std::exception& e) {
                Logger::instance().debug("intercept: set refresh failed: {}", e.what());
            } catch (...) {
                if (admitted && dns_release) dns_release();
                refresh_adds_.clear();
                refresh_slots_.clear();
                refresh_snaps_.clear();
                refresh_clients_.clear();
                refresh_client_valid_.clear();
                throw;
            }
            if (err == ETIMEDOUT) {
                late_backoff_until_ = Clock::now() + std::chrono::milliseconds(kLateBackoffWindowMs);
            }
            if (err == ENOENT) invalidate_set_cache();
            uint64_t added = 0, errors = 0;
            for (std::size_t i = 0; i < flush_adds_.size(); ++i) {
                switch (flush_results_[i]) {
                case nfnl::SetAddResult::Added:
                    // The element had vanished and was recreated: flows that
                    // were routed without it must be reset.
                    ++added;
                    note_written(flush_slots_[i], flush_adds_[i], write_started_ms, cache_epoch);
                    // The refresh carries the asker's address; without it
                    // nothing is deleted rather than every client's flows.
                    if (refresh_client_valid_[flush_refresh_idx_[i]]) {
                        cleanup_.request(flush_adds_[i].family, refresh_clients_[flush_refresh_idx_[i]],
                                         flush_adds_[i].addr);
                    }
                    break;
                case nfnl::SetAddResult::Refreshed:
                case nfnl::SetAddResult::Exists:
                    note_written(flush_slots_[i], flush_adds_[i], write_started_ms, cache_epoch);
                    break;
                default: ++errors; break;
                }
            }
            // Successful refreshes were already counted (or deliberately not)
            // when they were queued; only recreations and failures are news.
            bump(counters_.set_added, added);
            bump(counters_.set_errors, errors);
            sync_cache_gauge();
        }
        if (admitted && dns_release) dns_release();
    } catch (const std::exception& e) {
        Logger::instance().debug("intercept: post-verdict refresh handling failed: {}", e.what());
    } catch (...) {  // NOLINT(bugprone-empty-catch): packet handling must never throw into the netfilter loop
    }
    refresh_adds_.clear();
    refresh_slots_.clear();
    refresh_snaps_.clear();
    refresh_clients_.clear();
    refresh_client_valid_.clear();
}

void InterceptProcessor::handle_l7(ByteView l3, Clock::time_point now) {
    const Clock::time_point started = Clock::now();
    const uint64_t cache_epoch = cache_.epoch();
    std::shared_ptr<const SlotTable> slots;
    const auto snap = snapshot_and_slots(slots);
    if (!snap || !snap->index || !slots) return;

    const auto layout = dns_wire::parse_packet_layout(l3);
    if (!layout) return;
    if (layout->payload_len == 0 || layout->payload_offset > l3.size() ||
        layout->payload_len > l3.size() - layout->payload_offset) {
        return;
    }
    const bool tcp = layout->l4_proto == IPPROTO_TCP;
    const bool udp = layout->l4_proto == IPPROTO_UDP;
    if (!tcp && !udp) return;
    if (layout->l4_header_len < (tcp ? 20u : 8u) ||
        layout->l3_header_len + layout->l4_header_len > l3.size()) {
        return;
    }
    const uint8_t* l4 = l3.data() + layout->l3_header_len;
    const uint16_t dport = load_be16(l4 + 2);

    InterceptSource source;
    if (tcp && dport == 443 && snap->tls) {
        source = InterceptSource::sni;
    } else if (tcp && dport == 80 && snap->http) {
        source = InterceptSource::http;
    } else if (udp && dport == 443 && snap->quic) {
        source = InterceptSource::quic;
    } else {
        return;
    }
    bump(counters_.l7_packets);

    const ByteView payload(l3.data() + layout->payload_offset, layout->payload_len);
    l7::FlowKey key;
    key.family = layout->ip_version;
    const std::size_t alen = layout->ip_version == 6 ? 16 : 4;
    const std::size_t src_off = layout->ip_version == 6 ? 8 : 12;
    const std::size_t dst_off = layout->ip_version == 6 ? 24 : 16;
    if (dst_off + alen > l3.size()) return;
    std::memcpy(key.src.data(), l3.data() + src_off, alen);
    std::memcpy(key.dst.data(), l3.data() + dst_off, alen);
    key.sport = load_be16(l4);
    key.dport = dport;

    l7::ParseStatus status = l7::ParseStatus::NotMatched;
    sni_.clear();
    if (source == InterceptSource::quic) {
        const ByteView stream = quic_.feed(payload, now);
        if (stream.size() == 0) return;
        status = l7::client_hello_sni(stream, sni_);
        if (status == l7::ParseStatus::NeedMore) return;
        quic_.erase_last();
    } else {
        const ByteView stream = flows_.feed(key, load_be32(l4 + 4), payload, now);
        if (stream.size() == 0) return;
        status = source == InterceptSource::sni ? l7::tls_stream_sni(stream, sni_, tls_scratch_)
                                                : l7::http_host(stream, sni_);
        if (status == l7::ParseStatus::NeedMore) return;
        flows_.erase(key);
    }
    if (status != l7::ParseStatus::Found || sni_.empty()) return;

    snap->index->lookup(sni_, ids_);
    if (ids_.empty()) {
        // The full observation stream includes parsed application requests
        // even when no configured list matched them.  There is no set work
        // for these events, but the packet metadata is still useful to the
        // current-requests view.
        EventRecord event;
        event.source = source;
        set_client(event, layout->ip_version, key.src.data());
        set_domain(event, sni_);
        event.parse_us = micros_since(started, Clock::now());
        add_ip(event, layout->ip_version, key.dst.data());
        push_event(event);
        return;
    }
    bump(counters_.l7_matched);

    adds_.clear();
    add_slots_.clear();
    for (DomainIndex::ListId id : ids_) {
        append_add(*snap, *slots, id, layout->ip_version, key.dst, 0, false);
    }

    EventRecord event;
    event.source = source;
    set_client(event, layout->ip_version, key.src.data());
    set_domain(event, sni_);
    event.parse_us = micros_since(started, Clock::now());
    record_list_ids(*slots, event);
    add_ip(event, layout->ip_version, key.dst.data());

    // A trusted cached element needs no write at all (and no conntrack cleanup).
    const int64_t cache_now_ms = to_ms(clock_now());
    if (!adds_.empty()) classify_adds(snap, cache_now_ms, /*queue_refreshes=*/false, event);

    if (!adds_.empty()) {
        InterceptL7Work work;
        work.snapshot = snap;
        work.adds = std::move(adds_);
        work.slots = std::move(add_slots_);
        work.cache_epoch = cache_epoch;
        work.cache_now_ms = cache_now_ms;
        work.event = event;
        work.family = layout->ip_version;
        work.destination = key.dst;
        work.client = key.src;
        L7Submitter submitter;
        {
            std::lock_guard<std::mutex> lock(snapshot_mutex_);
            submitter = l7_submitter_;
        }
        if (submitter) {
            submitter(std::move(work));
            return;
        }
        process_l7_work(std::move(work), writer_);
        return;
    }
    push_event(event);
}

void InterceptProcessor::process_l7_work(InterceptL7Work work,
                                         nfnl::DynamicSetWriter& writer) {
    record_l7_result(std::move(work), writer);
}

void InterceptProcessor::reject_l7_work(InterceptL7Work work) {
    work.event.errors += static_cast<uint32_t>(work.adds.size());
    bump(counters_.set_errors, work.adds.size());
    push_event(work.event);
}

void InterceptProcessor::record_l7_result(InterceptL7Work work,
                                          nfnl::DynamicSetWriter& writer) {
    constexpr int kL7BudgetMs = 100;  // L7 is off the DNS hold thread.
    std::vector<nfnl::SetAddResult> results(work.adds.size(), nfnl::SetAddResult::Error);
    WriterAdmission l7_admission;
    WriterRelease l7_release;
    {
        std::lock_guard<std::mutex> lock(snapshot_mutex_);
        l7_admission = l7_admission_;
        l7_release = l7_release_;
    }
    const bool admitted = !l7_admission || l7_admission();
    const bool current = admitted && snapshot_is_current(work.snapshot);
    if (admitted) {
        if (current) {
            const Clock::time_point write_started = Clock::now();
            try {
                writer.add(work.adds.data(), results.data(), work.adds.size(), kL7BudgetMs);
            } catch (const std::exception& e) {
                Logger::instance().debug("intercept: set writer failed: {}", e.what());
            } catch (...) {
                Logger::instance().debug("intercept: set writer failed with an unknown exception");
            }
            work.event.set_write_us = static_cast<uint32_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - write_started)
                    .count());
            counters_.l7_write_latency.record(work.event.set_write_us, work.adds.size());
        }
        if (l7_release) l7_release();
    }
    bool any_added = false;
    for (std::size_t i = 0; i < results.size(); ++i) {
        const bool known = i < work.slots.size();
        switch (results[i]) {
        case nfnl::SetAddResult::Added:
            ++work.event.added;
            any_added = true;
            if (known) note_written(work.slots[i], work.adds[i], work.cache_now_ms, work.cache_epoch);
            break;
        case nfnl::SetAddResult::Refreshed:
            ++work.event.refreshed;
            if (known) note_written(work.slots[i], work.adds[i], work.cache_now_ms, work.cache_epoch);
            break;
        default: ++work.event.errors; break;
        }
    }
    if (admitted && current && work.event.errors > 0 && writer.last_errno() == ENOENT) {
        invalidate_set_cache();
    }
    sync_cache_gauge();
    bump(counters_.set_added, work.event.added);
    bump(counters_.set_refreshed, work.event.refreshed);
    bump(counters_.set_errors, work.event.errors);
    // Only an address that was actually added needs conntrack cleanup.
    if (any_added) cleanup_.request(work.family, work.client, work.destination);
    push_event(work.event);
}

} // namespace keen_pbr3
