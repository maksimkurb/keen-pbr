#include "prometheus_metrics.hpp"

#include <keen-pbr/version.hpp>

#include <algorithm>
#include <iomanip>
#include <map>
#include <numeric>
#include <sstream>
#include <utility>
#include <vector>

#ifndef KEEN_PBR_GIT_COMMIT
#define KEEN_PBR_GIT_COMMIT "unknown"
#endif

namespace keen_pbr3 {
namespace {

std::string escape_label(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const char c : value) {
        if (c == '\\' || c == '"') escaped.push_back('\\');
        if (c == '\n') {
            escaped += "\\n";
        } else {
            escaped.push_back(c);
        }
    }
    return escaped;
}

void header(std::ostringstream& out, const char* name, const char* type, const char* help) {
    out << "# HELP " << name << ' ' << help << '\n'
        << "# TYPE " << name << ' ' << type << '\n';
}

void counter(std::ostringstream& out, const char* name, const char* help, uint64_t value) {
    header(out, name, "counter", help);
    out << name << ' ' << value << '\n';
}

void histogram(std::ostringstream& out, const char* name, const char* help,
               const std::string& labels, const uint64_t* buckets, std::size_t bucket_count,
               const uint64_t* bounds, std::size_t bound_count, uint64_t sum_us) {
    header(out, name, "histogram", help);
    uint64_t cumulative = 0;
    for (std::size_t i = 0; i < bucket_count; ++i) {
        cumulative += buckets[i];
        if (i < bound_count) {
            out << name << "_bucket{" << labels;
            if (!labels.empty()) out << ',';
            out << "le=\"" << std::fixed
                << std::setprecision(6) << static_cast<double>(bounds[i]) / 1000000.0
                << "\"} " << cumulative << '\n';
        } else {
            out << name << "_bucket{" << labels;
            if (!labels.empty()) out << ',';
            out << "le=\"+Inf\"} "
                << cumulative << '\n';
        }
    }
    out << name << "_sum";
    if (!labels.empty()) out << '{' << labels << '}';
    out << ' ' << std::defaultfloat << std::setprecision(12) << static_cast<double>(sum_us) / 1000000.0 << '\n'
        << name << "_count";
    if (!labels.empty()) out << '{' << labels << '}';
    out << ' ' << cumulative << '\n';
}

template <typename AtomicHistogram>
void write_latency(std::ostringstream& out, const char* name, const char* help,
                   const std::string& labels, const AtomicHistogram& source) {
    std::array<uint64_t, AtomicHistogram::kBuckets> bins{};
    for (std::size_t i = 0; i < bins.size(); ++i) {
        bins[i] = source.buckets[i].load(std::memory_order_relaxed);
    }
    const auto sum = source.sum_us.load(std::memory_order_relaxed);
    histogram(out, name, help, labels, bins.data(), bins.size(),
              AtomicHistogram::kBoundsUs.data(), AtomicHistogram::kBoundsUs.size(), sum);
}

} // namespace

std::string prometheus_metrics(const InterceptCounters* counters,
                               const NetlinkMetricsSnapshot& netlink,
                               const ControlRuntimeSnapshot& runtime,
                               const OutboundRuntimeSnapshot& outbounds,
                               const Config& config,
                               uint64_t firewall_apply_errors,
                               const std::string& firewall_backend) {
    std::ostringstream out;
    out << std::setprecision(12);
    out << "# HELP keen_pbr_build_info Build identity.\n"
        << "# TYPE keen_pbr_build_info gauge\n"
        << "keen_pbr_build_info{version=\"" << escape_label(KEEN_PBR3_VERSION_STRING)
        << "\",commit=\"" << escape_label(KEEN_PBR_GIT_COMMIT)
        << "\",firewall_backend=\"" << escape_label(firewall_backend) << "\"} 1\n";

    header(out, "keen_pbr_active_rules", "gauge",
           "Number of realized active firewall rules.");
    out << "keen_pbr_active_rules " << runtime.realized_rules.size() << '\n';

    counter(out, "keen_pbr_firewall_apply_errors_total",
            "Failed firewall apply attempts.", firewall_apply_errors);
    counter(out, "keen_pbr_netlink_errors_total",
            "Failed route or policy-rule netlink operations.", netlink.errors);

    if (counters != nullptr) {
#define KPBR_COUNTER(field, description) \
        counter(out, "keen_pbr_intercept_" #field "_total", description, \
                counters->field.load(std::memory_order_relaxed))
        KPBR_COUNTER(dns_packets, "DNS packets observed.");
        KPBR_COUNTER(dns_parse_errors, "DNS packet parse errors.");
        KPBR_COUNTER(dns_matched, "DNS answers matched configured domains.");
        KPBR_COUNTER(dns_aaaa_ignored, "AAAA answers ignored while IPv6 is disabled.");
        KPBR_COUNTER(dns_hold_timeouts, "DNS holds that reached their deadline.");
        KPBR_COUNTER(dns_late_writes, "DNS writes completed after releasing the packet.");
        KPBR_COUNTER(dns_late_write_errors, "Failed deferred DNS writes.");
        KPBR_COUNTER(set_write_slow, "Set writes slower than 20 milliseconds.");
        KPBR_COUNTER(dns_timeout_budget_spent_by_batch, "DNS hold timeouts caused by batch budget.");
        KPBR_COUNTER(dns_timeout_admission_blocked, "DNS hold timeouts blocked from write admission.");
        KPBR_COUNTER(dns_timeout_own_write_slow, "DNS hold timeouts caused by a slow own write.");
        KPBR_COUNTER(dns_timeout_late_batch_full, "DNS hold timeouts caused by a full late-write batch.");
        KPBR_COUNTER(dns_timeout_other, "DNS hold timeouts from other causes.");
        KPBR_COUNTER(dns_tcp_partial, "DNS TCP messages not fully reassembled.");
        KPBR_COUNTER(marker_hits, "DNS marker responses observed.");
        KPBR_COUNTER(l7_packets, "Layer 7 packets inspected.");
        KPBR_COUNTER(l7_matched, "Layer 7 destinations matched configured domains.");
        KPBR_COUNTER(set_added, "Dynamic set elements added.");
        KPBR_COUNTER(set_refreshed, "Dynamic set elements refreshed.");
        KPBR_COUNTER(set_errors, "Dynamic set write errors.");
        KPBR_COUNTER(set_cache_hits, "Dynamic set cache hits.");
        KPBR_COUNTER(set_cache_misses, "Dynamic set cache misses.");
        KPBR_COUNTER(dns_refresh_deferred, "DNS refreshes deferred until after verdict.");
        KPBR_COUNTER(refresh_skipped, "Cached set entries whose refresh was skipped.");
        KPBR_COUNTER(refresh_dropped, "Refresh requests dropped because the queue was full.");
        KPBR_COUNTER(conntrack_requests, "Conntrack cleanup requests.");
        KPBR_COUNTER(conntrack_deleted, "Conntrack entries deleted.");
        KPBR_COUNTER(conntrack_errors, "Conntrack cleanup errors.");
        KPBR_COUNTER(queue_overruns, "NFQUEUE receive overruns.");
        KPBR_COUNTER(log_overruns, "NFLOG receive overruns.");
#undef KPBR_COUNTER
        header(out, "keen_pbr_intercept_set_cache_entries", "gauge",
               "Current number of remembered dynamic set elements.");
        out << "keen_pbr_intercept_set_cache_entries "
            << counters->set_cache_entries.load(std::memory_order_relaxed) << '\n';

        const auto& dns_bounds = WriteLatencyCounters::kPrometheusBoundsUs;
        const auto write_intercept_histogram = [&](const char* name, const char* help,
                                                   const WriteLatencyCounters& value) {
            std::array<uint64_t, WriteLatencyCounters::kPrometheusBuckets> bins{};
            for (std::size_t i = 0; i < bins.size(); ++i) {
                bins[i] = value.prometheus_buckets[i].load(std::memory_order_relaxed);
            }
            histogram(out, name, help, "", bins.data(), bins.size(), dns_bounds.data(),
                      dns_bounds.size(), value.sum_us.load(std::memory_order_relaxed));
        };
        write_intercept_histogram("keen_pbr_dns_write_duration_seconds",
                                  "Synchronous DNS dynamic set write duration.",
                                  counters->dns_write_latency);
        write_intercept_histogram("keen_pbr_dns_late_write_duration_seconds",
                                  "Deferred DNS dynamic set write duration.",
                                  counters->late_write_latency);
        write_intercept_histogram("keen_pbr_l7_write_duration_seconds",
                                  "Layer 7 dynamic set write duration.",
                                  counters->l7_write_latency);
        write_intercept_histogram("keen_pbr_dns_hold_duration_seconds",
                                  "Time a processed DNS packet was held before its verdict.",
                                  counters->dns_hold_latency);
        write_intercept_histogram("keen_pbr_dns_queue_wait_duration_seconds",
                                  "Time a DNS packet waited in the receive queue before processing.",
                                  counters->dns_queue_wait_latency);
        write_intercept_histogram("keen_pbr_dns_admission_wait_duration_seconds",
                                  "Time a DNS packet waited for write admission.",
                                  counters->dns_admission_wait_latency);
        header(out, "keen_pbr_set_write_max_microseconds", "gauge",
               "Maximum observed write duration in microseconds by write path.");
        header(out, "keen_pbr_set_write_max_elements", "gauge",
               "Element count in the slowest observed write by write path.");
        const std::array<std::pair<const char*, const WriteLatencyCounters*>, 3> write_paths{{
            {"dns", &counters->dns_write_latency},
            {"late_dns", &counters->late_write_latency},
            {"l7", &counters->l7_write_latency},
        }};
        for (const auto& [path, latency] : write_paths) {
            out << "keen_pbr_set_write_max_microseconds{path=\"" << path << "\"} "
                << latency->max_us.load(std::memory_order_relaxed) << '\n'
                << "keen_pbr_set_write_max_elements{path=\"" << path << "\"} "
                << latency->max_elements.load(std::memory_order_relaxed) << '\n';
        }

        const auto& writer = counters->netlink_write_metrics;
        write_latency(out, "keen_pbr_netlink_write_total_duration_seconds",
                      "Total dynamic set netlink write duration.", "", writer.total);
        write_latency(out, "keen_pbr_netlink_write_send_duration_seconds",
                      "Dynamic set netlink sendto duration.", "", writer.send);
        write_latency(out, "keen_pbr_netlink_write_remainder_duration_seconds",
                      "Dynamic set netlink write time excluding sendto.", "", writer.remainder);

    }

    const uint64_t set_errors = counters == nullptr ? 0 :
        counters->set_errors.load(std::memory_order_relaxed);
    const uint64_t conntrack_errors = counters == nullptr ? 0 :
        counters->conntrack_errors.load(std::memory_order_relaxed);
    const uint64_t queue_overruns = counters == nullptr ? 0 :
        counters->queue_overruns.load(std::memory_order_relaxed);
    const uint64_t log_overruns = counters == nullptr ? 0 :
        counters->log_overruns.load(std::memory_order_relaxed);
    const uint64_t parser_errors = counters == nullptr ? 0 :
        counters->dns_parse_errors.load(std::memory_order_relaxed);
    header(out, "keen_pbr_errors_total", "counter",
           "Cumulative runtime errors grouped by subsystem.");
    out << "keen_pbr_errors_total{category=\"firewall\"} " << firewall_apply_errors + set_errors << '\n'
        << "keen_pbr_errors_total{category=\"kernel\"} "
        << netlink.errors + conntrack_errors + queue_overruns + log_overruns << '\n'
        << "keen_pbr_errors_total{category=\"parser\"} " << parser_errors << '\n';

    static constexpr std::array<uint32_t, 10> probe_bounds_ms{
        1, 2, 5, 10, 25, 50, 100, 250, 500, 1000};
    struct ProbeSample {
        std::string labels;
        const ProbeMetrics* metrics;
    };
    std::vector<ProbeSample> probes;
    std::map<std::string, std::string> interfaces;
    std::map<std::string, OutboundType> outbound_types;
    for (const auto& outbound : config.outbounds.value_or(std::vector<Outbound>{})) {
        outbound_types[outbound.tag] = outbound.type;
        if (outbound.type == OutboundType::INTERFACE) {
            interfaces[outbound.tag] = outbound.interface.value_or("");
        }
    }
    for (const auto& [test_tag, state] : outbounds.urltest_states) {
        const char* test_type = state.config.type == OutboundType::ICMPTEST ? "icmptest" : "urltest";
        for (const auto& [outbound_tag, metrics] : state.probe_metrics) {
            const auto interface = interfaces.find(outbound_tag);
            const auto type = outbound_types.find(outbound_tag);
            const std::string type_name = type != outbound_types.end() && type->second == OutboundType::ICMPTEST
                ? "icmptest" : test_type;
            probes.push_back({"outbound=\"" + escape_label(outbound_tag) +
                "\",test_outbound=\"" + escape_label(test_tag) + "\",interface=\"" +
                escape_label(interface == interfaces.end() ? std::string{} : interface->second) +
                "\",type=\"" + type_name + "\"", &metrics});
        }
    }
    header(out, "keen_pbr_probe_attempts_total", "counter",
           "Accepted URLTEST and ICMPTEST probe results.");
    for (const auto& probe : probes) {
        out << "keen_pbr_probe_attempts_total{" << probe.labels << "} "
            << probe.metrics->attempts << '\n';
    }
    header(out, "keen_pbr_probe_successes_total", "counter",
           "Successful URLTEST and ICMPTEST probe results.");
    for (const auto& probe : probes) {
        out << "keen_pbr_probe_successes_total{" << probe.labels << "} "
            << probe.metrics->successes << '\n';
    }
    header(out, "keen_pbr_probe_success_ratio", "gauge",
           "Successful probes divided by observed probes; omitted before the first result.");
    for (const auto& probe : probes) {
        if (probe.metrics->attempts != 0) {
            out << "keen_pbr_probe_success_ratio{" << probe.labels << "} "
                << static_cast<double>(probe.metrics->successes) / probe.metrics->attempts << '\n';
        }
    }
    header(out, "keen_pbr_probe_packets_attempted_total", "counter",
           "ICMP probe packets attempted.");
    for (const auto& probe : probes) {
        out << "keen_pbr_probe_packets_attempted_total{" << probe.labels << "} "
            << probe.metrics->packets_attempted << '\n';
    }
    header(out, "keen_pbr_probe_packets_sent_total", "counter",
           "ICMP probe packets sent.");
    for (const auto& probe : probes) {
        out << "keen_pbr_probe_packets_sent_total{" << probe.labels << "} "
            << probe.metrics->packets_sent << '\n';
    }
    header(out, "keen_pbr_probe_packets_received_total", "counter",
           "ICMP probe packets received.");
    for (const auto& probe : probes) {
        out << "keen_pbr_probe_packets_received_total{" << probe.labels << "} "
            << probe.metrics->packets_received << '\n';
    }
    header(out, "keen_pbr_probe_packets_failed_total", "counter",
           "ICMP probe packets failed.");
    for (const auto& probe : probes) {
        out << "keen_pbr_probe_packets_failed_total{" << probe.labels << "} "
            << probe.metrics->packets_failed << '\n';
    }
    header(out, "keen_pbr_probe_latency_seconds", "histogram",
           "Observed URLTEST and ICMPTEST probe latency.");
    for (const auto& probe : probes) {
        const auto& metrics = *probe.metrics;
        if (metrics.latency_count == 0) continue;
            uint64_t cumulative = 0;
            for (std::size_t i = 0; i < metrics.latency_buckets.size(); ++i) {
                cumulative += metrics.latency_buckets[i];
                out << "keen_pbr_probe_latency_seconds_bucket{" << probe.labels << ",le=\"";
                if (i < probe_bounds_ms.size()) {
                    out << static_cast<double>(probe_bounds_ms[i]) / 1000.0;
                } else {
                    out << "+Inf";
                }
                out << "\"} " << cumulative << '\n';
            }
    }
    for (const auto& probe : probes) {
        const auto& metrics = *probe.metrics;
        if (metrics.latency_count == 0) continue;
        out << "keen_pbr_probe_latency_seconds_sum{" << probe.labels << "} "
                << static_cast<double>(metrics.latency_sum_ms) / 1000.0 << '\n'
            << "keen_pbr_probe_latency_seconds_count{" << probe.labels << "} "
            << std::accumulate(metrics.latency_buckets.begin(), metrics.latency_buckets.end(), uint64_t{0})
            << '\n';
    }
    return out.str();
}

} // namespace keen_pbr3
