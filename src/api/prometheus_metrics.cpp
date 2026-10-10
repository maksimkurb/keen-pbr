#include "prometheus_metrics.hpp"

#include <keen-pbr/version.hpp>

#include <algorithm>
#include <iomanip>
#include <map>
#include <array>
#include <atomic>
#include <initializer_list>
#include <optional>
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

// One sample of a labelled family.  `labels` is the already escaped inner part
// of the braces ("" for an unlabelled sample).
template <typename Value>
void sample(std::ostringstream& out, const char* name, const std::string& labels, Value value) {
    out << name;
    if (!labels.empty()) out << '{' << labels << '}';
    out << ' ' << value << '\n';
}

std::string label(const char* key, const std::string& value) {
    return std::string(key) + "=\"" + escape_label(value) + '"';
}

double seconds_from_us(uint64_t us) { return static_cast<double>(us) / 1000000.0; }

// Histogram series without HELP/TYPE so several label sets share one family.
// `le` uses the shortest decimal so that le="0.025" matches in PromQL.
void histogram_series(std::ostringstream& out, const char* name, const std::string& labels,
                      const WriteLatencyCounters& source) {
    const auto& bounds = WriteLatencyCounters::kPrometheusBoundsUs;
    const std::string prefix = labels.empty() ? std::string{} : labels + ',';
    uint64_t cumulative = 0;
    for (std::size_t i = 0; i < WriteLatencyCounters::kPrometheusBuckets; ++i) {
        cumulative += source.prometheus_buckets[i].load(std::memory_order_relaxed);
        out << name << "_bucket{" << prefix << "le=\"";
        if (i < bounds.size()) {
            out << seconds_from_us(bounds[i]);
        } else {
            out << "+Inf";
        }
        out << "\"} " << cumulative << '\n';
    }
    sample(out, (std::string(name) + "_sum").c_str(), labels,
           seconds_from_us(source.sum_us.load(std::memory_order_relaxed)));
    sample(out, (std::string(name) + "_count").c_str(), labels, cumulative);
}

uint64_t load(const std::atomic<uint64_t>& value) {
    return value.load(std::memory_order_relaxed);
}

struct ProbeSample {
    std::string labels;
    bool icmp{false};
    const ProbeMetrics* metrics{nullptr};
};

} // namespace

std::string prometheus_metrics(const InterceptCounters* counters,
                               const NetlinkMetricsSnapshot& netlink,
                               const ControlRuntimeSnapshot& runtime,
                               const OutboundRuntimeSnapshot& outbounds,
                               const Config& config,
                               const DaemonMetrics& daemon,
                               const std::string& firewall_backend) {
    std::ostringstream out;
    out << std::setprecision(12);
    header(out, "keen_pbr_build_info", "gauge", "Build identity.");
    out << "keen_pbr_build_info{version=\"" << escape_label(KEEN_PBR3_VERSION_STRING)
        << "\",commit=\"" << escape_label(KEEN_PBR_GIT_COMMIT)
        << "\",firewall_backend=\"" << escape_label(firewall_backend) << "\"} 1\n";

    header(out, "keen_pbr_process_start_time_seconds", "gauge",
           "Start time of the process as Unix time in seconds.");
    out << "keen_pbr_process_start_time_seconds " << daemon.process_start_unix_s << '\n';

    header(out, "keen_pbr_active_rules", "gauge",
           "Number of realized active firewall rules.");
    out << "keen_pbr_active_rules " << runtime.realized_rules.size() << '\n';

    if (counters != nullptr) {
        header(out, "keen_pbr_set_cache_entries", "gauge",
               "Current number of remembered dynamic set elements.");
        out << "keen_pbr_set_cache_entries " << load(counters->set_cache_entries) << '\n';
    }

    if (daemon.config_reload_last_success_unix_s) {
        header(out, "keen_pbr_config_reload_last_success_timestamp_seconds", "gauge",
               "Unix time when the runtime last finished applying a configuration.");
        out << "keen_pbr_config_reload_last_success_timestamp_seconds "
            << *daemon.config_reload_last_success_unix_s << '\n';
    }
    header(out, "keen_pbr_config_reload_errors_total", "counter",
           "Failed configuration reloads and applies.");
    out << "keen_pbr_config_reload_errors_total " << daemon.config_reload_errors << '\n';

    // Remote list refresh telemetry: only lists that are configured with a URL.
    std::vector<std::string> remote_lists;
    if (config.lists) {
        for (const auto& [name, list] : *config.lists) {
            if (list.url.has_value()) remote_lists.push_back(name);
        }
    }
    if (!remote_lists.empty()) {
        bool any_success = false;
        for (const auto& name : remote_lists) {
            const auto it = daemon.lists.find(name);
            any_success = any_success ||
                (it != daemon.lists.end() && it->second.last_success_unix_s.has_value());
        }
        if (any_success) {
            header(out, "keen_pbr_list_last_update_timestamp_seconds", "gauge",
                   "Unix time of the last successful remote list check; omitted until one succeeded.");
            for (const auto& name : remote_lists) {
                const auto it = daemon.lists.find(name);
                if (it != daemon.lists.end() && it->second.last_success_unix_s) {
                    sample(out, "keen_pbr_list_last_update_timestamp_seconds",
                           label("list", name), *it->second.last_success_unix_s);
                }
            }
        }
        header(out, "keen_pbr_list_update_errors_total", "counter",
               "Failed remote list refresh attempts.");
        for (const auto& name : remote_lists) {
            const auto it = daemon.lists.find(name);
            sample(out, "keen_pbr_list_update_errors_total", label("list", name),
                   it == daemon.lists.end() ? uint64_t{0} : it->second.errors);
        }
    }

    // Errors: one family, every subsystem always present.
    const auto counter_or_zero = [&](const std::atomic<uint64_t> InterceptCounters::*field) {
        return counters == nullptr ? uint64_t{0} : load(counters->*field);
    };
    header(out, "keen_pbr_errors_total", "counter",
           "Cumulative runtime errors grouped by subsystem.");
    const std::array<std::pair<const char*, uint64_t>, 7> errors{{
        {"firewall_apply", daemon.firewall_apply_errors},
        {"netlink", netlink.errors},
        {"set_write", counter_or_zero(&InterceptCounters::set_errors)},
        {"conntrack", counter_or_zero(&InterceptCounters::conntrack_errors)},
        {"dns_parse", counter_or_zero(&InterceptCounters::dns_parse_errors)},
        {"dns_tcp_partial", counter_or_zero(&InterceptCounters::dns_tcp_partial)},
        {"dns_late_write", counter_or_zero(&InterceptCounters::dns_late_write_errors)},
    }};
    for (const auto& [subsystem, value] : errors) {
        sample(out, "keen_pbr_errors_total", label("subsystem", subsystem), value);
    }

    if (counters != nullptr) {
        using Pair = std::pair<const char*, uint64_t>;
        const auto family = [&](const char* name, const char* help, const char* key,
                                std::initializer_list<Pair> values) {
            header(out, name, "counter", help);
            for (const auto& [value_label, value] : values) {
                sample(out, name, label(key, value_label), value);
            }
        };
        family("keen_pbr_intercept_packets_total", "Intercepted packets inspected by path.", "path",
               {{"dns", load(counters->dns_packets)}, {"l7", load(counters->l7_packets)}});
        family("keen_pbr_intercept_matches_total",
               "Intercepted packets that matched a configured domain, by path.", "path",
               {{"dns", load(counters->dns_matched)}, {"l7", load(counters->l7_matched)}});

        header(out, "keen_pbr_dns_hold_duration_seconds", "histogram",
               "Time a processed DNS packet was held before its verdict.");
        histogram_series(out, "keen_pbr_dns_hold_duration_seconds", "", counters->dns_hold_latency);
        header(out, "keen_pbr_dns_queue_wait_duration_seconds", "histogram",
               "Time a DNS packet waited in the receive queue before processing.");
        histogram_series(out, "keen_pbr_dns_queue_wait_duration_seconds", "",
                         counters->dns_queue_wait_latency);

        family("keen_pbr_dns_hold_timeouts_total",
               "DNS holds that reached their deadline, by cause.", "cause",
               {{"batch_budget", load(counters->dns_timeout_budget_spent_by_batch)},
                {"admission_blocked", load(counters->dns_timeout_admission_blocked)},
                {"own_write_slow", load(counters->dns_timeout_own_write_slow)},
                {"late_batch_full", load(counters->dns_timeout_late_batch_full)},
                {"other", load(counters->dns_timeout_other)}});
        header(out, "keen_pbr_dns_late_writes_total", "counter",
               "DNS writes completed after the packet was released.");
        out << "keen_pbr_dns_late_writes_total " << load(counters->dns_late_writes) << '\n';
        family("keen_pbr_queue_overruns_total", "Kernel queue receive overruns.", "queue",
               {{"nfqueue", load(counters->queue_overruns)}, {"nflog", load(counters->log_overruns)}});

        header(out, "keen_pbr_set_write_duration_seconds", "histogram",
               "Dynamic set write duration by write path.");
        histogram_series(out, "keen_pbr_set_write_duration_seconds", "path=\"dns\"",
                         counters->dns_write_latency);
        histogram_series(out, "keen_pbr_set_write_duration_seconds", "path=\"late_dns\"",
                         counters->late_write_latency);
        histogram_series(out, "keen_pbr_set_write_duration_seconds", "path=\"l7\"",
                         counters->l7_write_latency);

        family("keen_pbr_set_writes_total",
               "Dynamic set elements written: newly added or timeout-refreshed.", "kind",
               {{"add", load(counters->set_added)}, {"refresh", load(counters->set_refreshed)}});
        family("keen_pbr_set_refresh_total", "Set element refresh outcomes.", "result",
               {{"skipped", load(counters->refresh_skipped)},
                {"deferred", load(counters->dns_refresh_deferred)},
                {"dropped", load(counters->refresh_dropped)}});
        family("keen_pbr_set_cache_lookups_total", "Dynamic set cache lookups.", "result",
               {{"hit", load(counters->set_cache_hits)}, {"miss", load(counters->set_cache_misses)}});

        header(out, "keen_pbr_conntrack_requests_total", "counter", "Conntrack cleanup requests.");
        out << "keen_pbr_conntrack_requests_total " << load(counters->conntrack_requests) << '\n';
        header(out, "keen_pbr_conntrack_deleted_total", "counter", "Conntrack entries deleted.");
        out << "keen_pbr_conntrack_deleted_total " << load(counters->conntrack_deleted) << '\n';
    }

    // Firewall rule counters (iptables only): parsed from `iptables-save -c`.
    if (!daemon.firewall_counters.balance_classifications.empty()) {
        header(out, "keen_pbr_balance_classifications_total", "counter",
               "New connections classified to a balance candidate by the statistic cascade; "
               "restarts from zero when the firewall rules are rebuilt.");
        for (const auto& item : daemon.firewall_counters.balance_classifications) {
            sample(out, "keen_pbr_balance_classifications_total",
                   label("outbound", item.outbound) + ',' + label("candidate", item.candidate) +
                   ',' + label("family", item.family),
                   item.connections);
        }
    }
    if (!daemon.firewall_counters.skip_marked_packets.empty()) {
        header(out, "keen_pbr_skip_marked_packets_total", "counter",
               "Packets that bypassed keen-pbr because they already carried a mark "
               "(daemon.skip_marked_packets); restarts from zero when the firewall rules are rebuilt.");
        for (const auto& [family, packets] : daemon.firewall_counters.skip_marked_packets) {
            sample(out, "keen_pbr_skip_marked_packets_total", label("family", family), packets);
        }
    }

    // Probes.
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
        const bool test_is_icmp = state.config.type == OutboundType::ICMPTEST;
        for (const auto& [outbound_tag, metrics] : state.probe_metrics) {
            const auto interface = interfaces.find(outbound_tag);
            const auto type = outbound_types.find(outbound_tag);
            const bool icmp = (type != outbound_types.end() && type->second == OutboundType::ICMPTEST) ||
                test_is_icmp;
            probes.push_back({label("outbound", outbound_tag) + ',' + label("test_outbound", test_tag) +
                ',' + label("interface", interface == interfaces.end() ? std::string{} : interface->second) +
                ",type=\"" + (icmp ? "icmptest" : "urltest") + '"', icmp, &metrics});
        }
    }
    const auto probe_family = [&](const char* name, const char* type, const char* help,
                                  bool icmp_only, const auto& value_of) {
        bool header_written = false;
        for (const auto& probe : probes) {
            if (icmp_only && !probe.icmp) continue;
            const auto value = value_of(*probe.metrics);
            if (!value) continue;
            if (!header_written) {
                header(out, name, type, help);
                header_written = true;
            }
            sample(out, name, probe.labels, *value);
        }
    };
    using Metrics = ProbeMetrics;
    probe_family("keen_pbr_probe_attempts_total", "counter",
                 "Accepted URLTEST and ICMPTEST probe results.", false,
                 [](const Metrics& m) { return std::optional<uint64_t>(m.attempts); });
    probe_family("keen_pbr_probe_successes_total", "counter",
                 "Successful URLTEST and ICMPTEST probe results.", false,
                 [](const Metrics& m) { return std::optional<uint64_t>(m.successes); });
    probe_family("keen_pbr_probe_packets_sent_total", "counter",
                 "ICMP echo requests sent (icmptest only).", true,
                 [](const Metrics& m) { return std::optional<uint64_t>(m.packets_sent); });
    probe_family("keen_pbr_probe_packets_received_total", "counter",
                 "ICMP echo replies received (icmptest only).", true,
                 [](const Metrics& m) { return std::optional<uint64_t>(m.packets_received); });
    probe_family("keen_pbr_probe_up", "gauge",
                 "1 when the last probe succeeded, 0 when it failed; omitted before the first probe.",
                 false, [](const Metrics& m) {
                     return m.last_up ? std::optional<int>(*m.last_up ? 1 : 0) : std::nullopt;
                 });
    probe_family("keen_pbr_probe_last_success_timestamp_seconds", "gauge",
                 "Unix time of the last successful probe; omitted until the first success.", false,
                 [](const Metrics& m) { return m.last_success_unix_s; });
    const auto latency = [](std::optional<uint64_t> Metrics::*field) {
        return [field](const Metrics& m) -> std::optional<double> {
            if (!(m.*field)) return std::nullopt;
            return seconds_from_us(*(m.*field));
        };
    };
    probe_family("keen_pbr_probe_latency_seconds", "gauge",
                 "Latency of the last successful probe (ICMP: mean of the replies); omitted when the "
                 "last probe failed or none completed.",
                 false, latency(&Metrics::latency_us));
    probe_family("keen_pbr_probe_latency_min_seconds", "gauge",
                 "Fastest reply of the last successful ICMP probe; omitted on failure.", true,
                 latency(&Metrics::latency_min_us));
    probe_family("keen_pbr_probe_latency_max_seconds", "gauge",
                 "Slowest reply of the last successful ICMP probe; omitted on failure.", true,
                 latency(&Metrics::latency_max_us));

    // urltest / icmptest group selection.
    bool selection_header = false;
    for (const auto& [group_tag, state] : outbounds.urltest_states) {
        std::vector<std::string> members;
        for (const auto& group : state.config.outbound_groups.value_or(std::vector<OutboundGroup>{})) {
            for (auto& tag : outbound_group_tags(group)) {
                if (std::find(members.begin(), members.end(), tag) == members.end()) {
                    members.push_back(std::move(tag));
                }
            }
        }
        for (const auto& member : members) {
            if (!selection_header) {
                header(out, "keen_pbr_urltest_selected", "gauge",
                       "1 for the currently selected outbound of a urltest/icmptest group, 0 for other members.");
                selection_header = true;
            }
            sample(out, "keen_pbr_urltest_selected",
                   label("group", group_tag) + ',' + label("outbound", member),
                   state.selected_outbound == member ? 1 : 0);
        }
    }
    if (!outbounds.urltest_states.empty()) {
        header(out, "keen_pbr_urltest_selection_changes_total", "counter",
               "Selected outbound changes of a urltest/icmptest group since process start.");
        for (const auto& [group_tag, state] : outbounds.urltest_states) {
            sample(out, "keen_pbr_urltest_selection_changes_total", label("group", group_tag),
                   state.selection_changes);
        }
    }
    return out.str();
}

} // namespace keen_pbr3
