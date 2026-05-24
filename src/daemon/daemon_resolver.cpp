#include "daemon.hpp"

#include "../dns/dns_router.hpp"
#include "../dns/keenetic_dns.hpp"
#include "../dns/dns_txt_client.hpp"
#include "../dns/dnsmasq_gen.hpp"
#include "../lists/list_streamer.hpp"
#include "../log/logger.hpp"
#include "../util/ipv6_support.hpp"
#include "../util/time_utils.hpp"
#include "scheduler.hpp"

#include <fmt/ranges.h>

namespace keen_pbr3 {

namespace {

constexpr auto kResolverConfigHashActualRefreshInterval = std::chrono::seconds{5};

bool dns_config_uses_keenetic_server(const std::optional<DnsConfig>& dns_cfg_opt) {
    if (!dns_cfg_opt.has_value()) {
        return false;
    }

    for (const auto& server : dns_cfg_opt->servers.value_or(std::vector<DnsServer>{})) {
        if (server.type.value_or(api::DnsServerType::STATIC) == api::DnsServerType::KEENETIC) {
            return true;
        }
    }

    return false;
}

} // namespace

void Daemon::update_resolver_config_hash() {
    ListStreamer streamer(list_service_.cache_manager());
    const DnsConfig dns_cfg = config_.dns.value_or(DnsConfig{});
    DnsServerRegistry dns_registry(dns_cfg);
    const Ipv6SupportDecision ipv6_decision = resolve_ipv6_support(config_);
    log_ipv6_support_decision_once(ipv6_decision);
    const std::string resolver_config_hash = DnsmasqGenerator::compute_config_hash(
        dns_registry,
        streamer,
        config_.route.value_or(RouteConfig{}),
        dns_cfg,
        config_.lists.value_or(std::map<std::string, ListConfig>{}),
        KEEN_PBR3_VERSION_FULL_STRING,
        ipv6_decision.enabled);
    resolver_sync_.expected_hash_updated(resolver_config_hash);
    const std::int64_t apply_started_ts =
        apply_started_ts_.load(std::memory_order_acquire);
    if (apply_started_ts > 0) {
        resolver_sync_.apply_started(apply_started_ts, resolver_config_hash);
    }
    Logger::instance().info("Resolver config hash: {}", resolver_config_hash);
}

RuntimeStateSnapshot Daemon::build_runtime_state_snapshot() const {
    RuntimeStateSnapshot snapshot;
    snapshot.firewall_state = firewall_state_;
    snapshot.route_specs = route_table_.get_routes();
    snapshot.policy_rule_specs = policy_rules_.get_rules();
    const auto resolver_snapshot = resolver_sync_.snapshot(unix_timestamp_now_seconds());
    snapshot.resolver_config_hash = resolver_snapshot.expected_hash;
    snapshot.resolver_config_hash_actual = resolver_snapshot.actual_hash;
    snapshot.resolver_config_hash_actual_ts = resolver_snapshot.actual_ts;
    snapshot.resolver_config_sync_state = resolver_snapshot.sync_state;
    snapshot.resolver_config_probe_status = resolver_snapshot.probe_status;
    snapshot.resolver_live_status = resolver_snapshot.live_status;
    snapshot.resolver_last_probe_ts = resolver_snapshot.last_probe_ts;
    snapshot.apply_started_ts = resolver_snapshot.apply_started_ts;
    snapshot.routing_runtime_active = routing_runtime_active_;

    if (urltest_manager_) {
        for (const auto& outbound : config_.outbounds.value_or(std::vector<Outbound>{})) {
            if (outbound.type != OutboundType::URLTEST) {
                continue;
            }
            auto state = urltest_manager_->get_state(outbound.tag);
            if (state.has_value()) {
                snapshot.urltest_states.emplace(outbound.tag, std::move(*state));
            }
        }
    }

    return snapshot;
}

void Daemon::publish_runtime_state() {
    Logger::instance().trace("runtime_state_publish", "routing_runtime_active={}",
                             routing_runtime_active_ ? "true" : "false");
    runtime_state_store_.publish(build_runtime_state_snapshot());
}

void Daemon::schedule_resolver_config_hash_actual_refresh() {
    if (resolver_config_hash_actual_task_id_ >= 0) {
        scheduler_->cancel(resolver_config_hash_actual_task_id_);
    }
    resolver_config_hash_actual_task_id_ = scheduler_->schedule_repeating(
        kResolverConfigHashActualRefreshInterval,
        [this]() {
            maybe_schedule_resolver_config_hash_actual_refresh();
        },
        "resolver-config-hash-actual");
}

void Daemon::schedule_keenetic_dns_refresh() {
    if (keenetic_dns_refresh_task_id_ >= 0) {
        scheduler_->cancel(keenetic_dns_refresh_task_id_);
        keenetic_dns_refresh_task_id_ = -1;
    }

    if (!dns_config_uses_keenetic_server(config_.dns)) {
        return;
    }

    keenetic_dns_refresh_task_id_ = scheduler_->schedule_repeating(
        std::chrono::minutes{5},
        [this]() {
            post_control_task([this]() {
                if (!routing_runtime_active_) {
                    return;
                }
                if (refresh_keenetic_dns_cache(true)) {
                    const std::int64_t apply_started_ts = unix_timestamp_now_seconds();
                    apply_started_ts_.store(apply_started_ts, std::memory_order_release);
                    run_system_resolver_hook_reload();
                    update_resolver_config_hash();
                    refresh_resolver_config_hash_actual_async();
                    publish_runtime_state();
                }
            }, "keenetic-dns-refresh");
        },
        "keenetic-dns-refresh");
}

bool Daemon::refresh_keenetic_dns_cache(bool force_refresh) {
    if (!dns_config_uses_keenetic_server(config_.dns)) {
        return false;
    }

    const KeeneticDnsRefreshResult result = refresh_keenetic_dns_address_cache(force_refresh);
    auto& log = Logger::instance();

    switch (result.status) {
    case KeeneticDnsRefreshStatus::UPDATED:
        if (!result.addresses.empty()) {
            log.info("Keenetic DNS refreshed: {}", fmt::join(result.addresses, ", "));
        }
        return true;
    case KeeneticDnsRefreshStatus::UNCHANGED:
        return false;
    case KeeneticDnsRefreshStatus::FETCH_FAILED_USED_CACHE: {
        const std::string value_suffix =
            result.addresses.size() > 1 ? "s: "
            : (result.addresses.empty() ? "" : ": ");
        log.warn("Keenetic DNS refresh failed; reusing cached value{}{}",
                 value_suffix,
                 fmt::join(result.addresses, ", "));
        if (!result.error.empty()) {
            log.warn("Keenetic DNS refresh error: {}", result.error);
        }
        return false;
    }
    case KeeneticDnsRefreshStatus::FETCH_FAILED_NO_CACHE:
        if (!result.error.empty()) {
            log.warn("Keenetic DNS refresh failed with no cached value: {}", result.error);
        }
        return false;
    }

    return false;
}

void Daemon::schedule_resolver_config_hash_actual_retry() {
    if (resolver_config_hash_actual_retry_task_id_ >= 0) {
        scheduler_->cancel(resolver_config_hash_actual_retry_task_id_);
    }
    resolver_config_hash_actual_retry_task_id_ = scheduler_->schedule_oneshot(
        std::chrono::seconds{1},
        [this]() {
            resolver_config_hash_actual_retry_task_id_ = -1;
            maybe_schedule_resolver_config_hash_actual_refresh();
        },
        "resolver-config-hash-actual-retry");
}

void Daemon::reset_resolver_actual_state() {
    resolver_sync_.resolver_not_configured();
}

void Daemon::commit_resolver_hash_probe_result(
    const std::string& resolver_addr,
    std::uint64_t generation,
    std::optional<ResolverConfigHashProbeResult> probe_result,
    std::optional<std::int64_t> probe_completed_ts,
    TraceId trace_id) {
    post_control_task(
        [this,
         resolver_addr,
         generation,
         probe_result = std::move(probe_result),
         probe_completed_ts,
         trace_id]() mutable {
            ScopedTraceContext trace_scope_inner(trace_id);
            resolver_hash_refresh_inflight_.store(false, std::memory_order_release);

            if (generation != runtime_generation_.load(std::memory_order_acquire)) {
                Logger::instance().trace("resolver_hash_refresh_skip",
                                         "resolver={} generation={} reason=stale_runtime",
                                         resolver_addr,
                                         generation);
                return;
            }

            const std::int64_t apply_started_ts =
                apply_started_ts_.load(std::memory_order_acquire);
            const std::int64_t now_ts = unix_timestamp_now_seconds();
            if (probe_result.has_value() &&
                probe_result->status == ResolverConfigHashProbeStatus::SUCCESS) {
                resolver_sync_.probe_succeeded(probe_result->parsed_value.hash,
                                               probe_result->parsed_value.ts,
                                               probe_completed_ts);
                Logger::instance().verbose("Resolver config hash (actual): {}",
                                           probe_result->parsed_value.hash);
                if (probe_result->parsed_value.ts.has_value() &&
                    apply_started_ts > 0 &&
                    *probe_result->parsed_value.ts < apply_started_ts) {
                    Logger::instance().verbose(
                        "Resolver config hash TXT is older than current apply; using live actual value "
                        "(resolver={}, txt_ts={}, apply_started_ts={})",
                        resolver_addr,
                        *probe_result->parsed_value.ts,
                        apply_started_ts);
                }
            } else if (probe_result.has_value()) {
                resolver_sync_.probe_failed(probe_result->status, probe_completed_ts);
                switch (probe_result->status) {
                case ResolverConfigHashProbeStatus::QUERY_FAILED:
                    Logger::instance().warn(
                        "Resolver config hash TXT query failed via {}: {}; clearing actual value",
                        resolver_addr,
                        probe_result->error);
                    break;
                case ResolverConfigHashProbeStatus::NO_USABLE_TXT:
                    Logger::instance().warn(
                        "Resolver config hash TXT is missing via {}; clearing actual value",
                        resolver_addr);
                    break;
                case ResolverConfigHashProbeStatus::INVALID_TXT:
                    Logger::instance().warn(
                        "Resolver config hash TXT is invalid via {}: {}; clearing actual value",
                        resolver_addr,
                        probe_result->raw_txt.value_or("<empty>"));
                    break;
                case ResolverConfigHashProbeStatus::SUCCESS:
                    break;
                }
            }
            const auto resolver_snapshot = resolver_sync_.snapshot(now_ts);
            if (resolver_snapshot.sync_state ==
                api::ResolverConfigSyncState::CONVERGING) {
                schedule_resolver_config_hash_actual_retry();
            }
            publish_runtime_state();
        },
        "resolver-hash-refresh-commit");
}

void Daemon::refresh_resolver_config_hash_actual_async() {
    const auto dns_cfg_opt = config_.dns;
    if (!routing_runtime_active_ ||
        !dns_cfg_opt.has_value() ||
        !dns_cfg_opt->system_resolver.has_value()) {
        if (!routing_runtime_active_) {
            resolver_sync_.runtime_stopped();
        } else {
            reset_resolver_actual_state();
        }
        publish_runtime_state();
        return;
    }

    const std::string resolver_addr = dns_cfg_opt->system_resolver->address;
    if (resolver_addr.empty()) {
        reset_resolver_actual_state();
        publish_runtime_state();
        return;
    }

    bool expected = false;
    if (!resolver_hash_refresh_inflight_.compare_exchange_strong(expected,
                                                                 true,
                                                                 std::memory_order_acq_rel)) {
        Logger::instance().trace("resolver_hash_refresh_skip", "reason=inflight");
        return;
    }

    const auto generation = runtime_generation_.load(std::memory_order_acquire);
    const TraceId trace_id = ensure_trace_id();
    const bool enqueued = blocking_executor_.try_post(
        "resolver-config-hash-actual",
        [this, resolver_addr, generation, trace_id]() mutable {
            ScopedTraceContext trace_scope(trace_id);
            std::optional<ResolverConfigHashProbeResult> probe_result;
            std::optional<std::int64_t> probe_completed_ts;

            Logger::instance().trace("resolver_hash_refresh_start",
                                     "resolver={} generation={}",
                                     resolver_addr,
                                     generation);
            try {
                probe_result = query_resolver_config_hash_txt(
                    resolver_addr,
                    "config-hash.keen.pbr",
                    std::chrono::milliseconds(2000));
                probe_completed_ts = unix_timestamp_now_seconds();
            } catch (const std::exception& e) {
                ResolverConfigHashProbeResult failed_result;
                failed_result.status = ResolverConfigHashProbeStatus::QUERY_FAILED;
                failed_result.error = e.what();
                probe_result = std::move(failed_result);
                probe_completed_ts = unix_timestamp_now_seconds();
            }

            commit_resolver_hash_probe_result(resolver_addr,
                                              generation,
                                              std::move(probe_result),
                                              probe_completed_ts,
                                              trace_id);
        },
        trace_id);

    if (!enqueued) {
        resolver_hash_refresh_inflight_.store(false, std::memory_order_release);
        Logger::instance().trace("resolver_hash_refresh_skip",
                                 "reason=executor_unavailable");
    }
}

void Daemon::maybe_schedule_resolver_config_hash_actual_refresh() {
    if (resolver_hash_refresh_inflight_.load(std::memory_order_acquire)) {
        Logger::instance().trace("resolver_hash_refresh_skip", "reason=inflight");
        return;
    }
    refresh_resolver_config_hash_actual_async();
}

} // namespace keen_pbr3
