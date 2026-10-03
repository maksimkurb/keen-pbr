#include "intercept_report.hpp"

namespace keen_pbr3 {

namespace {
int64_t load(const std::atomic<uint64_t>& value) {
    return static_cast<int64_t>(value.load(std::memory_order_relaxed));
}
} // namespace

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
                                                uint64_t events_seq) {
    api::InterceptHealthClass health;
    health.enabled = effective.config_enabled;
    health.running = running;
    health.dns_hold_active = running && effective.dns_hold;
    health.l7_active = running && effective.l7;
    health.capabilities.nfqueue = effective.capabilities.nfqueue;
    health.capabilities.nflog = effective.capabilities.nflog;
    health.capabilities.connbytes = effective.capabilities.connbytes;
    health.reasons = effective.reasons;
    if (!effective.config_enabled) {
        health.reasons.insert(health.reasons.begin(), "interception is disabled by config");
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
        out.dns_tcp_partial = load(counters->dns_tcp_partial);
        out.marker_hits = load(counters->marker_hits);
        out.l7_packets = load(counters->l7_packets);
        out.l7_matched = load(counters->l7_matched);
        out.set_added = load(counters->set_added);
        out.set_refreshed = load(counters->set_refreshed);
        out.set_errors = load(counters->set_errors);
        out.conntrack_requests = load(counters->conntrack_requests);
        out.conntrack_deleted = load(counters->conntrack_deleted);
        out.conntrack_errors = load(counters->conntrack_errors);
        out.queue_overruns = load(counters->queue_overruns);
        out.log_overruns = load(counters->log_overruns);
        health.counters = out;
    }
    return health;
}

nlohmann::json intercept_event_to_json(const InterceptEvent& event) {
    return {
        {"type", "INTERCEPT"},
        {"seq", event.seq},
        {"ts_ms", event.ts_ms},
        {"source", intercept_source_name(event.source)},
        {"domain", event.domain},
        {"lists", event.lists},
        {"ips", event.ips},
        {"added", event.added},
        {"refreshed", event.refreshed},
        {"errors", event.errors},
        {"hold_us", event.hold_us},
        {"timed_out", event.timed_out},
    };
}

} // namespace keen_pbr3
