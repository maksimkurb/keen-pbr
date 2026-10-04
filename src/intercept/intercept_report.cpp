#include "intercept_report.hpp"

#include <fstream>
#include <sstream>

namespace keen_pbr3 {

namespace {
int64_t load(const std::atomic<uint64_t>& value) {
    return static_cast<int64_t>(value.load(std::memory_order_relaxed));
}

api::DnsWriteLatency make_write_latency(const WriteLatencyCounters& hist) {
    api::DnsWriteLatency out;
    out.lt_1_ms = load(hist.buckets[0]);
    out.lt_5_ms = load(hist.buckets[1]);
    out.lt_10_ms = load(hist.buckets[2]);
    out.lt_30_ms = load(hist.buckets[3]);
    out.lt_100_ms = load(hist.buckets[4]);
    out.ge_100_ms = load(hist.buckets[5]);
    out.max_us = load(hist.max_us);
    out.max_elements = load(hist.max_elements);
    return out;
}
} // namespace

static api::InterceptProbeFeatureStatus probe_status_to_api(nfnl::ProbeStatus status) {
    switch (status) {
    case nfnl::ProbeStatus::ok: return api::InterceptProbeFeatureStatus::OK;
    case nfnl::ProbeStatus::unsupported: return api::InterceptProbeFeatureStatus::UNSUPPORTED;
    case nfnl::ProbeStatus::error: return api::InterceptProbeFeatureStatus::ERROR;
    case nfnl::ProbeStatus::skipped: return api::InterceptProbeFeatureStatus::SKIPPED;
    case nfnl::ProbeStatus::not_run: break;
    }
    return api::InterceptProbeFeatureStatus::NOT_RUN;
}

const char* intercept_source_name(InterceptSource source) {
    switch (source) {
    case InterceptSource::dns: return "dns";
    case InterceptSource::sni: return "sni";
    case InterceptSource::http: return "http";
    case InterceptSource::quic: return "quic";
    case InterceptSource::marker: return "marker";
    }
    return "dns";
}

api::InterceptHealthClass make_intercept_health(const InterceptEffective& effective,
                                                bool running,
                                                const InterceptCounters* counters,
                                                uint64_t events_seq,
                                                bool snapshot_ready) {
    api::InterceptHealthClass health;
    health.enabled = effective.config_enabled;
    health.running = running;
    health.dns_hold_active = running && effective.dns_hold && snapshot_ready;
    health.l7_active = running && effective.l7 && snapshot_ready;
    health.capabilities.nfqueue = effective.capabilities.nfqueue;
    health.capabilities.nflog = effective.capabilities.nflog;
    health.capabilities.connbytes = effective.capabilities.connbytes;
    const auto& probe = effective.capabilities.probe;
    health.capabilities.fail_open = probe.fail_open.status == nfnl::ProbeStatus::not_run
                                        ? std::nullopt
                                        : std::optional<bool>(probe.fail_open.is_ok());
    if (probe.nfqueue.status != nfnl::ProbeStatus::not_run) {
        switch (probe.replacement) {
        case nfnl::ReplacementCapability::supported:
            health.capabilities.payload_replacement = api::PayloadReplacement::SUPPORTED;
            break;
        case nfnl::ReplacementCapability::unsupported:
            health.capabilities.payload_replacement = api::PayloadReplacement::UNSUPPORTED;
            break;
        case nfnl::ReplacementCapability::unknown:
            health.capabilities.payload_replacement = api::PayloadReplacement::UNKNOWN;
            break;
        }
    }
    if (probe.conntrack.status != nfnl::ProbeStatus::not_run) {
        health.capabilities.conntrack_cleanup = effective.conntrack_cleanup;
    }
    if (!probe.kernel_release.empty()) health.kernel_release = probe.kernel_release;
    if (probe.ipset_protocol != 0) health.ipset_protocol = probe.ipset_protocol;
    std::vector<api::InterceptProbeFeatureElement> probes;
    for (const auto& item : probe.items()) {
        api::InterceptProbeFeatureElement out;
        out.feature = item.feature;
        out.status = probe_status_to_api(item.result.status);
        if (!item.result.reason.empty()) out.reason = item.result.reason;
        probes.push_back(std::move(out));
    }
    health.probes = std::move(probes);
    if (!effective.warnings.empty()) health.warnings = effective.warnings;
    health.reasons = effective.reasons;
    if (!effective.config_enabled) {
        health.reasons.insert(health.reasons.begin(), "interception is disabled by config");
    }
    if (running && !snapshot_ready) {
        health.reasons.push_back("interception snapshot is still initializing");
    }
    health.queue_num = effective.queue_num;
    health.nflog_group = effective.nflog_group;
    health.events_seq = static_cast<int64_t>(events_seq);
    if (counters != nullptr) {
        api::Counters out;
        out.dns_packets = load(counters->dns_packets);
        out.dns_parse_errors = load(counters->dns_parse_errors);
        out.dns_matched = load(counters->dns_matched);
        out.dns_hold_timeouts = load(counters->dns_hold_timeouts);
        out.dns_late_writes = load(counters->dns_late_writes);
        out.dns_late_write_errors = load(counters->dns_late_write_errors);
        out.set_write_slow = load(counters->set_write_slow);
        out.dns_tcp_partial = load(counters->dns_tcp_partial);
        out.marker_hits = load(counters->marker_hits);
        out.l7_packets = load(counters->l7_packets);
        out.l7_matched = load(counters->l7_matched);
        out.set_added = load(counters->set_added);
        out.set_refreshed = load(counters->set_refreshed);
        out.set_errors = load(counters->set_errors);
        out.set_cache_hits = load(counters->set_cache_hits);
        out.set_cache_misses = load(counters->set_cache_misses);
        out.set_cache_entries = load(counters->set_cache_entries);
        out.dns_refresh_deferred = load(counters->dns_refresh_deferred);
        out.refresh_dropped = load(counters->refresh_dropped);
        out.conntrack_requests = load(counters->conntrack_requests);
        out.conntrack_deleted = load(counters->conntrack_deleted);
        out.conntrack_errors = load(counters->conntrack_errors);
        out.queue_overruns = load(counters->queue_overruns);
        out.log_overruns = load(counters->log_overruns);
        out.dns_timeout_budget_spent_by_batch = load(counters->dns_timeout_budget_spent_by_batch);
        out.dns_timeout_admission_blocked = load(counters->dns_timeout_admission_blocked);
        out.dns_timeout_own_write_slow = load(counters->dns_timeout_own_write_slow);
        out.dns_timeout_late_batch_full = load(counters->dns_timeout_late_batch_full);
        out.dns_timeout_other = load(counters->dns_timeout_other);
        out.dns_write_latency = make_write_latency(counters->dns_write_latency);
        out.late_write_latency = make_write_latency(counters->late_write_latency);
        out.l7_write_latency = make_write_latency(counters->l7_write_latency);
        health.counters = out;
    }
    return health;
}

nlohmann::json intercept_event_to_json(const InterceptEvent& event) {
    nlohmann::json json = {
        {"type", "INTERCEPT"},
        {"seq", event.seq},
        {"ts_ms", event.ts_ms},
        {"source", intercept_source_name(event.source)},
        {"client_ip", event.client_ip},
        {"domain", event.domain},
        {"lists", event.lists},
        {"ips", event.ips},
        {"added", event.added},
        {"refreshed", event.refreshed},
        {"errors", event.errors},
        {"cache_hits", event.cache_hits},
        {"deferred_refresh", event.deferred_refresh},
        {"hold_us", event.hold_us},
        {"parse_us", event.parse_us},
        {"set_write_us", event.set_write_us},
        {"timed_out", event.timed_out},
        {"late_write", event.late_write},
    };
    if (event.batch_pos >= 0) {
        json["batch_pos"] = event.batch_pos;
        if (event.batch_size > 0) json["batch_size"] = event.batch_size;
        json["queue_wait_us"] = event.queue_wait_us;
        json["budget_left_us"] = event.budget_left_us;
        json["admission_wait_us"] = event.admission_wait_us;
    }
    if (event.write_elements > 0) json["write_elements"] = event.write_elements;
    if (event.late_batch_elements > 0) json["late_batch_elements"] = event.late_batch_elements;
    if (event.write_errno != 0) json["write_errno"] = event.write_errno;
    if (event.timed_out && event.timeout_cause != TimeoutCause::none) {
        json["timeout_cause"] = timeout_cause_name(event.timeout_cause);
    }
    return json;
}

std::optional<EventGap> detect_event_gap(uint64_t forwarded_seq,
                                         const std::vector<InterceptEvent>& events) {
    if (events.empty() || events.front().seq <= forwarded_seq + 1) return std::nullopt;
    return EventGap{forwarded_seq + 1, events.front().seq - 1};
}

nlohmann::json event_gap_to_json(const EventGap& gap) {
    return {{"type", "GAP"}, {"from_seq", gap.from_seq}, {"to_seq", gap.to_seq}};
}

std::string gap_notice_for_dropped(const std::string& first, const std::string& last) {
    const auto range = [](const std::string& text, bool want_to) -> std::optional<uint64_t> {
        const auto json = nlohmann::json::parse(text, nullptr, false);
        if (!json.is_object()) return std::nullopt;
        const char* key = json.contains("from_seq") ? (want_to ? "to_seq" : "from_seq") : "seq";
        const auto it = json.find(key);
        if (it == json.end() || !it->is_number_unsigned()) return std::nullopt;
        return it->get<uint64_t>();
    };
    const auto from = range(first, false);
    const auto to = range(last, true);
    if (!from || !to) return {};
    return event_gap_to_json(EventGap{*from, *to}).dump();
}

std::optional<api::KernelQueue> parse_nfnetlink_queue(const std::string& text, int queue_num) {
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        std::istringstream cols(line);
        int64_t number = 0, peer = 0, total = 0, mode = 0, range = 0, dropped = 0, user = 0, seq = 0;
        if (!(cols >> number >> peer >> total >> mode >> range >> dropped >> user >> seq)) continue;
        if (number != queue_num) continue;
        api::KernelQueue out;
        out.queue_total = total;
        out.queue_dropped = dropped;
        out.user_dropped = user;
        out.id_sequence = seq;
        return out;
    }
    return std::nullopt;
}

std::optional<api::KernelQueue> read_kernel_queue(int queue_num) {
    std::ifstream in("/proc/net/netfilter/nfnetlink_queue");
    if (!in) return std::nullopt;
    std::ostringstream buf;
    buf << in.rdbuf();
    return parse_nfnetlink_queue(buf.str(), queue_num);
}

} // namespace keen_pbr3
