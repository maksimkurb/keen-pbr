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
    : writer_(writer), cleanup_(cleanup), counters_(counters) {}

void InterceptProcessor::set_snapshot(std::shared_ptr<const InterceptSnapshot> snapshot) {
    std::lock_guard<std::mutex> lock(snapshot_mutex_);
    snapshot_ = std::move(snapshot);
}

std::shared_ptr<const InterceptSnapshot> InterceptProcessor::snapshot() const {
    std::lock_guard<std::mutex> lock(snapshot_mutex_);
    return snapshot_;
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
        event.domain = response_.qname;
        event.hold_us = static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - started).count());
        push_event(std::move(event));
        return decision;
    }

    if (response_.rcode != 0 || response_.addresses.empty()) return {};

    snap->index->lookup(response_.qname, ids_);
    for (const std::string& name : response_.cname_chain) {
        if (name == response_.qname) continue;
        snap->index->lookup(name, ids_tmp_);
        if (ids_tmp_.empty()) continue;
        ids_.insert(ids_.end(), ids_tmp_.begin(), ids_tmp_.end());
    }
    if (ids_.empty()) return {};
    std::sort(ids_.begin(), ids_.end());
    ids_.erase(std::unique(ids_.begin(), ids_.end()), ids_.end());

    bump(counters_.dns_matched);

    adds_.clear();
    for (const dns_wire::AddressRecord& rec : response_.addresses) {
        for (DomainIndex::ListId id : ids_) append_add(*snap, id, rec.family, rec.addr, rec.ttl, true);
    }

    InterceptEvent event;
    event.source = InterceptSource::dns;
    event.domain = response_.qname;
    collect_list_names(*snap, event.lists);
    for (const dns_wire::AddressRecord& rec : response_.addresses) {
        event.ips.push_back(format_ip(rec.family, rec.addr));
    }

    if (!adds_.empty()) {
        results_.assign(adds_.size(), nfnl::SetAddResult::Error);
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - Clock::now() + std::chrono::microseconds(999));
        const int budget_ms = static_cast<int>(std::max<int64_t>(1, remaining.count()));
        bool ok = false;
        try {
            ok = writer_.add(adds_.data(), results_.data(), adds_.size(), budget_ms);
        } catch (const std::exception& e) {
            Logger::instance().debug("intercept: set writer failed: {}", e.what());
        }
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
        if (!ok && event.errors > 0 && writer_.last_errno() == ETIMEDOUT) {
            event.timed_out = true;
            bump(counters_.dns_hold_timeouts);
            // The client may already be connecting: purge stale flows to these IPs.
            for (const dns_wire::AddressRecord& rec : response_.addresses) {
                cleanup_.request(rec.family, rec.addr);
            }
        }
    }
    event.hold_us = static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - started).count());
    push_event(std::move(event));
    return decision;
}

void InterceptProcessor::handle_l7(ByteView l3, Clock::time_point now) {
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
    if (ids_.empty()) return;
    bump(counters_.l7_matched);

    adds_.clear();
    for (DomainIndex::ListId id : ids_) append_add(*snap, id, layout->ip_version, key.dst, 0, false);

    InterceptEvent event;
    event.source = source;
    event.domain = sni_;
    collect_list_names(*snap, event.lists);
    event.ips.push_back(format_ip(layout->ip_version, key.dst));

    if (!adds_.empty()) {
        constexpr int kL7BudgetMs = 100;  // packet is already on its way; no hold deadline
        results_.assign(adds_.size(), nfnl::SetAddResult::Error);
        try {
            writer_.add(adds_.data(), results_.data(), adds_.size(), kL7BudgetMs);
        } catch (const std::exception& e) {
            Logger::instance().debug("intercept: set writer failed: {}", e.what());
        }
        bool any_added = false;
        for (nfnl::SetAddResult r : results_) {
            switch (r) {
            case nfnl::SetAddResult::Added: ++event.added; any_added = true; break;
            case nfnl::SetAddResult::Refreshed: ++event.refreshed; break;
            default: ++event.errors; break;
            }
        }
        bump(counters_.set_added, event.added);
        bump(counters_.set_refreshed, event.refreshed);
        bump(counters_.set_errors, event.errors);
        if (any_added) cleanup_.request(layout->ip_version, key.dst);
    }
    push_event(std::move(event));
}

} // namespace keen_pbr3
