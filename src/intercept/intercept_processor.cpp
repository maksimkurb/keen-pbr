#include "intercept_processor.hpp"

#include "../l7/http_host.hpp"
#include "../l7/tls_client_hello.hpp"
#include "../log/logger.hpp"

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

std::string format_ip(uint8_t family, const std::array<uint8_t, 16>& addr) {
    char buf[INET6_ADDRSTRLEN] = {};
    if (::inet_ntop(family == 6 ? AF_INET6 : AF_INET, addr.data(), buf, sizeof(buf)) == nullptr) {
        return {};
    }
    return buf;
}

uint16_t be16(const uint8_t* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }
uint32_t be32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | p[3];
}

uint32_t timeout_for(const InterceptSnapshot& snap, const InterceptListTarget& target,
                     uint32_t record_ttl_s, bool use_record_ttl) {
    if (target.min_ttl_s == 0) return 0;
    const uint32_t wanted = use_record_ttl ? std::max(record_ttl_s, target.min_ttl_s) : target.min_ttl_s;
    return std::clamp<uint32_t>(wanted, 1, std::max<uint32_t>(snap.max_ttl_s, 1));
}

void bump(std::atomic<uint64_t>& counter, uint64_t by = 1) {
    counter.fetch_add(by, std::memory_order_relaxed);
}

} // namespace

InterceptProcessor::InterceptProcessor(nfnl::DynamicSetWriter& writer, ConntrackCleanupSink& cleanup,
                                       InterceptCounters& counters)
    : writer_(writer), cleanup_(cleanup), counters_(counters) {
    late_adds_.reserve(kLateBatchCapacity);
    late_events_.reserve(kLateBatchCapacity);
    flush_adds_.reserve(kLateBatchCapacity);
    flush_results_.reserve(kLateBatchCapacity);
}

void InterceptProcessor::set_snapshot(std::shared_ptr<const InterceptSnapshot> snapshot) {
    std::lock_guard<std::mutex> lock(snapshot_mutex_);
    snapshot_ = std::move(snapshot);
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

bool InterceptProcessor::snapshot_is_current(
    const std::shared_ptr<const InterceptSnapshot>& snapshot) const {
    std::lock_guard<std::mutex> lock(snapshot_mutex_);
    return snapshot_ == snapshot;
}

void InterceptProcessor::push_event(InterceptEvent&& event) {
    std::lock_guard<std::mutex> lock(events_mutex_);
    event.seq = next_seq_++;
    event.ts_ms = wall_ms();
    events_.push_back(std::move(event));
    while (events_.size() > kEventCapacity) events_.pop_front();
}

std::vector<InterceptEvent> InterceptProcessor::events_since(uint64_t after_seq,
                                                             std::size_t max) const {
    std::vector<InterceptEvent> out;
    std::lock_guard<std::mutex> lock(events_mutex_);
    for (const InterceptEvent& event : events_) {
        if (event.seq <= after_seq) continue;
        if (out.size() >= max) break;
        out.push_back(event);
    }
    return out;
}

uint64_t InterceptProcessor::last_event_seq() const {
    std::lock_guard<std::mutex> lock(events_mutex_);
    return next_seq_ - 1;
}

void InterceptProcessor::collect_list_names(const InterceptSnapshot& snap,
                                            std::vector<std::string>& out) const {
    const auto& names = snap.index->list_names();
    for (DomainIndex::ListId id : ids_) {
        if (id < names.size()) out.push_back(names[id]);
    }
}

void InterceptProcessor::append_add(const InterceptSnapshot& snap, DomainIndex::ListId id,
                                    uint8_t family, const std::array<uint8_t, 16>& addr,
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
}

InterceptProcessor::DnsDecision InterceptProcessor::on_dns_packet(
    ByteView l3, Clock::time_point deadline, bool replacement_allowed) {
    try {
        return handle_dns(l3, deadline, replacement_allowed);
    } catch (const std::exception& e) {
        Logger::instance().debug("intercept: DNS packet handling failed: {}", e.what());
    } catch (...) {
    }
    return {};
}

void InterceptProcessor::on_l7_packet(ByteView l3, Clock::time_point now) {
    try {
        handle_l7(l3, now);
    } catch (const std::exception& e) {
        Logger::instance().debug("intercept: L7 packet handling failed: {}", e.what());
    } catch (...) {
    }
}

InterceptProcessor::DnsDecision InterceptProcessor::handle_dns(ByteView l3, Clock::time_point deadline,
                                                               bool replacement_allowed) {
    const Clock::time_point started = Clock::now();
    const auto snap = snapshot();
    if (!snap || !snap->index) return {};

    const auto layout = dns_wire::parse_packet_layout(l3);
    if (!layout || (layout->l4_proto != IPPROTO_UDP && layout->l4_proto != IPPROTO_TCP)) return {};
    if (layout->payload_offset > l3.size() || layout->payload_len > l3.size() - layout->payload_offset) {
        return {};
    }
    const bool udp = layout->l4_proto == IPPROTO_UDP;
    const ByteView payload(l3.data() + layout->payload_offset, layout->payload_len);
    if (!udp && payload.size() == 0) return {};  // bare ACK/FIN
    bump(counters_.dns_packets);

    ByteView message = payload;
    if (!udp) {
        const auto single = dns_wire::tcp_single_message(payload);
        if (!single) {
            bump(counters_.dns_tcp_partial);
            return {};
        }
        message = *single;
    }

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
        InterceptEvent event;
        event.source = InterceptSource::marker;
        const std::size_t client_offset = layout->ip_version == 6 ? 24 : 16;
        const std::size_t client_length = layout->ip_version == 6 ? 16 : 4;
        if (client_offset + client_length <= l3.size()) {
            std::array<uint8_t, 16> client{};
            std::memcpy(client.data(), l3.data() + client_offset, client_length);
            event.client_ip = format_ip(layout->ip_version, client);
        }
        event.domain = response_.qname;
        event.hold_us = static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - started).count());
        push_event(std::move(event));
        return decision;
    }

    const auto add_dns_observation = [&](InterceptEvent& event) {
        const std::size_t client_offset = layout->ip_version == 6 ? 24 : 16;
        const std::size_t client_length = layout->ip_version == 6 ? 16 : 4;
        if (client_offset + client_length <= l3.size()) {
            std::array<uint8_t, 16> client{};
            std::memcpy(client.data(), l3.data() + client_offset, client_length);
            event.client_ip = format_ip(layout->ip_version, client);
        }
        event.domain = response_.qname;
        event.parse_us = static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(parsed_at - started).count());
        for (const dns_wire::AddressRecord& rec : response_.addresses) {
            event.ips.push_back(format_ip(rec.family, rec.addr));
        }
    };

    if (response_.rcode != 0 || response_.addresses.empty()) {
        InterceptEvent event;
        event.source = InterceptSource::dns;
        add_dns_observation(event);
        push_event(std::move(event));
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
        InterceptEvent event;
        event.source = InterceptSource::dns;
        add_dns_observation(event);
        push_event(std::move(event));
        return {};
    }
    std::sort(ids_.begin(), ids_.end());
    ids_.erase(std::unique(ids_.begin(), ids_.end()), ids_.end());

    bump(counters_.dns_matched);

    adds_.clear();
    for (const dns_wire::AddressRecord& rec : response_.addresses) {
        for (DomainIndex::ListId id : ids_) append_add(*snap, id, rec.family, rec.addr, rec.ttl, true);
    }

    InterceptEvent event;
    event.source = InterceptSource::dns;
    add_dns_observation(event);
    collect_list_names(*snap, event.lists);

    if (!adds_.empty()) {
        results_.assign(adds_.size(), nfnl::SetAddResult::Error);
        // The write is deferred (verdict first) when the deadline has passed.
        bool defer = Clock::now() >= deadline;
        bool after_timeout = false;
        bool ok = false;
        bool write_attempted = false;
        if (!defer) {
            WriterAdmission dns_admission;
            WriterRelease dns_release;
            {
                std::lock_guard<std::mutex> lock(snapshot_mutex_);
                dns_admission = dns_admission_;
                dns_release = dns_release_;
            }
            const bool admitted = !dns_admission || dns_admission();
            if (admitted) {
                if (Clock::now() >= deadline) {
                    if (dns_release) dns_release();
                    defer = true;
                } else if (!snapshot_is_current(snap)) {
                    if (dns_release) dns_release();
                    event.errors = static_cast<uint32_t>(adds_.size());
                    bump(counters_.set_errors, event.errors);
                    event.hold_us = static_cast<uint32_t>(
                        std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - started)
                            .count());
                    push_event(std::move(event));
                    return decision;
                } else {
                    const auto remaining_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        (deadline - Clock::now()) + std::chrono::microseconds(999));
                    const int budget_ms = static_cast<int>(std::max<int64_t>(1, remaining_ms.count()));
                    write_attempted = true;
                    const Clock::time_point write_started = Clock::now();
                    try {
                        ok = writer_.add(adds_.data(), results_.data(), adds_.size(), budget_ms);
                    } catch (const std::exception& e) {
                        Logger::instance().debug("intercept: set writer failed: {}", e.what());
                    } catch (...) {
                        if (dns_release) dns_release();
                        throw;
                    }
                    event.set_write_us = static_cast<uint32_t>(
                        std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() -
                                                                              write_started)
                            .count());
                    if (dns_release) dns_release();
                }
            }
        }
        if (!defer && write_attempted && !ok && writer_.last_errno() == ETIMEDOUT) {
            // ETIMEDOUT means "unknown": the kernel may still apply the batch.
            // Keep what was confirmed, retry only the unconfirmed entries late.
            defer = true;
            after_timeout = true;
        }
        if (defer) {
            std::size_t kept = 0;
            for (std::size_t i = 0; i < adds_.size(); ++i) {
                switch (results_[i]) {
                case nfnl::SetAddResult::Added: ++event.added; break;
                case nfnl::SetAddResult::Refreshed: ++event.refreshed; break;
                default:
                    if (kept != i) adds_[kept] = adds_[i];
                    ++kept;
                    break;
                }
            }
            adds_.resize(kept);
            bump(counters_.set_added, event.added);
            bump(counters_.set_refreshed, event.refreshed);
            if (kept > 0) {
                decision.late_write =
                    defer_late_write(snap, std::move(event), started, after_timeout);
                return decision;
            }
            // Everything was confirmed in time despite the late ETIMEDOUT mark.
        } else {
            for (nfnl::SetAddResult r : results_) {
                switch (r) {
                case nfnl::SetAddResult::Added: ++event.added; break;
                case nfnl::SetAddResult::Refreshed: ++event.refreshed; break;
                default: ++event.errors; break;
                }
            }
            bump(counters_.set_added, event.added);
            bump(counters_.set_refreshed, event.refreshed);
            bump(counters_.set_errors, event.errors);
        }
    }
    event.hold_us = static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - started).count());
    push_event(std::move(event));
    return decision;
}

bool InterceptProcessor::defer_late_write(const std::shared_ptr<const InterceptSnapshot>& snap,
                                          InterceptEvent&& event, Clock::time_point started,
                                          bool after_timeout) {
    event.timed_out = true;
    event.late_write = true;
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
        push_event(std::move(event));
        return false;
    }
    LateEvent le;
    le.event = std::move(event);
    le.snapshot = snap;  // adds hold string_views into this snapshot
    le.addresses = response_.addresses;
    le.first = late_adds_.size();
    le.count = n;
    le.after_timeout = after_timeout;
    late_adds_.insert(late_adds_.end(), adds_.begin(), adds_.end());  // within reserved capacity
    late_events_.push_back(std::move(le));
    return true;
}

void InterceptProcessor::flush_late_writes() {
    if (late_events_.empty()) return;
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
        for (LateEvent& le : late_events_) {
            le.snapshot_ok = admitted && snapshot_is_current(le.snapshot);
            if (le.snapshot_ok) {
                flush_adds_.insert(flush_adds_.end(), late_adds_.begin() + static_cast<std::ptrdiff_t>(le.first),
                                   late_adds_.begin() + static_cast<std::ptrdiff_t>(le.first + le.count));
            }
        }
        flush_results_.assign(flush_adds_.size(), nfnl::SetAddResult::Error);
        int err = 0;
        uint32_t write_us = 0;
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
                late_events_.clear();
                throw;
            }
            write_us = static_cast<uint32_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - write_started)
                    .count());
        }
        if (admitted && dns_release) dns_release();
        if (err == ETIMEDOUT) {
            late_backoff_until_ = Clock::now() + std::chrono::milliseconds(kLateBackoffWindowMs);
        }

        std::size_t pos = 0;
        for (LateEvent& le : late_events_) {
            InterceptEvent event = std::move(le.event);
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
            if (le.snapshot_ok &&
                (added > 0 || (le.after_timeout && (refreshed > 0 || (errors > 0 && err == ETIMEDOUT))))) {
                for (const dns_wire::AddressRecord& rec : le.addresses) {
                    cleanup_.request(rec.family, rec.addr);
                }
            }
            push_event(std::move(event));
        }
    } catch (const std::exception& e) {
        Logger::instance().debug("intercept: late DNS write handling failed: {}", e.what());
    } catch (...) {
    }
    late_adds_.clear();
    late_events_.clear();
}

void InterceptProcessor::handle_l7(ByteView l3, Clock::time_point now) {
    const Clock::time_point started = Clock::now();
    const auto snap = snapshot();
    if (!snap || !snap->index) return;

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
    const uint16_t dport = be16(l4 + 2);

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
    key.sport = be16(l4);
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
        const ByteView stream = flows_.feed(key, be32(l4 + 4), payload, now);
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
        InterceptEvent event;
        event.source = source;
        event.client_ip = format_ip(layout->ip_version, key.src);
        event.domain = sni_;
        event.parse_us = static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - started).count());
        event.ips.push_back(format_ip(layout->ip_version, key.dst));
        push_event(std::move(event));
        return;
    }
    bump(counters_.l7_matched);

    adds_.clear();
    for (DomainIndex::ListId id : ids_) append_add(*snap, id, layout->ip_version, key.dst, 0, false);

    InterceptEvent event;
    event.source = source;
    event.client_ip = format_ip(layout->ip_version, key.src);
    event.domain = sni_;
    event.parse_us = static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - started).count());
    collect_list_names(*snap, event.lists);
    event.ips.push_back(format_ip(layout->ip_version, key.dst));

    if (!adds_.empty()) {
        InterceptL7Work work;
        work.snapshot = snap;
        work.adds = std::move(adds_);
        work.event = std::move(event);
        work.family = layout->ip_version;
        work.destination = key.dst;
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
    push_event(std::move(event));
}

void InterceptProcessor::process_l7_work(InterceptL7Work work,
                                         nfnl::DynamicSetWriter& writer) {
    record_l7_result(std::move(work), writer);
}

void InterceptProcessor::reject_l7_work(InterceptL7Work work) {
    work.event.errors += static_cast<uint32_t>(work.adds.size());
    bump(counters_.set_errors, work.adds.size());
    push_event(std::move(work.event));
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
        }
        if (l7_release) l7_release();
    }
    bool any_added = false;
    for (nfnl::SetAddResult result : results) {
        switch (result) {
        case nfnl::SetAddResult::Added:
            ++work.event.added;
            any_added = true;
            break;
        case nfnl::SetAddResult::Refreshed: ++work.event.refreshed; break;
        default: ++work.event.errors; break;
        }
    }
    bump(counters_.set_added, work.event.added);
    bump(counters_.set_refreshed, work.event.refreshed);
    bump(counters_.set_errors, work.event.errors);
    // Only an address that was actually added needs conntrack cleanup.
    if (any_added) cleanup_.request(work.family, work.destination);
    push_event(std::move(work.event));
}

} // namespace keen_pbr3
