#include "daemon.hpp"

#include "../dns/dns_router.hpp"
#include "../dns/keenetic_dns.hpp"
#include "../dns/dns_txt_client.hpp"
#include "../dns/dnsmasq_gen.hpp"
#include "../lists/list_streamer.hpp"
#include "../log/logger.hpp"
#include "../util/time_utils.hpp"
#include "resolver_health.hpp"
#include "scheduler.hpp"

namespace keen_pbr3 {

namespace {

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
    const ResolverType resolver_type = resolver_type_from_dns_config(dns_cfg);
    DnsServerRegistry dns_registry(dns_cfg);
    resolver_config_hash_ = DnsmasqGenerator::compute_config_hash(
        dns_registry,
        streamer,
        config_.route.value_or(RouteConfig{}),
        dns_cfg,
        config_.lists.value_or(std::map<std::string, ListConfig>{}),
        resolver_type);
    Logger::instance().info("Resolver config hash: {}", resolver_config_hash_);
}

RuntimeStateSnapshot Daemon::build_runtime_state_snapshot() const {
    RuntimeStateSnapshot snapshot;
    snapshot.firewall_state = firewall_state_;
    snapshot.route_specs = route_table_.get_routes();
    snapshot.policy_rule_specs = policy_rules_.get_rules();
    snapshot.resolver_config_hash = resolver_config_hash_;
    snapshot.resolver_config_hash_actual = resolver_config_hash_actual_;
    snapshot.resolver_config_hash_actual_ts = resolver_config_hash_actual_ts_;
    snapshot.resolver_live_status = resolver_live_status_;
    snapshot.resolver_last_probe_ts = resolver_last_probe_ts_;
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
        std::chrono::seconds{30},
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
                    update_resolver_config_hash();
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
        if (result.address.has_value()) {
            log.info("Keenetic DNS refreshed: {}", *result.address);
        }
        return true;
    case KeeneticDnsRefreshStatus::UNCHANGED:
        return false;
    case KeeneticDnsRefreshStatus::FETCH_FAILED_USED_CACHE:
        log.warn("Keenetic DNS refresh failed; reusing cached value{}{}",
                 result.address.has_value() ? " " : "",
                 result.address.has_value() ? *result.address : "");
        if (!result.error.empty()) {
            log.warn("Keenetic DNS refresh error: {}", result.error);
        }
        return false;
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
    resolver_config_hash_actual_.clear();
    resolver_config_hash_actual_ts_.reset();
    resolver_live_status_ = api::ResolverLiveStatus::UNKNOWN;
    resolver_last_probe_ts_.reset();
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

            const ResolverActualState actual_state = build_resolver_actual_state(
                routing_runtime_active_,
                true,
                probe_result,
                probe_completed_ts);
            resolver_live_status_ = actual_state.live_status;
            resolver_last_probe_ts_ = actual_state.last_probe_ts;
            resolver_config_hash_actual_ = actual_state.actual_hash;
            resolver_config_hash_actual_ts_ = actual_state.actual_ts;

            const std::int64_t apply_started_ts =
                apply_started_ts_.load(std::memory_order_acquire);
            const std::int64_t now_ts = unix_timestamp_now_seconds();
            const bool within_converging_window =
                apply_started_ts > 0 && (now_ts - apply_started_ts) <= 15;

            if (resolver_live_status_ == api::ResolverLiveStatus::HEALTHY) {
                Logger::instance().info("Resolver config hash (actual): {}",
                                        resolver_config_hash_actual_);
                if (resolver_config_hash_actual_ts_.has_value() &&
                    apply_started_ts > 0 &&
                    *resolver_config_hash_actual_ts_ < apply_started_ts) {
                    Logger::instance().verbose(
                        "Resolver config hash TXT is older than current apply; using live actual value "
                        "(resolver={}, txt_ts={}, apply_started_ts={})",
                        resolver_addr,
                        *resolver_config_hash_actual_ts_,
                        apply_started_ts);
                    if (within_converging_window) {
                        schedule_resolver_config_hash_actual_retry();
                    }
                }
            } else if (probe_result.has_value()) {
                switch (probe_result->status) {
                case ResolverConfigHashProbeStatus::QUERY_FAILED:
                    Logger::instance().warn(
                        "Resolver config hash TXT query failed via {}: {}; clearing actual value",
                        resolver_addr,
                        probe_result->error);
                    if (within_converging_window) {
                        schedule_resolver_config_hash_actual_retry();
                    }
                    break;
                case ResolverConfigHashProbeStatus::NO_USABLE_TXT:
                    Logger::instance().warn(
                        "Resolver config hash TXT is missing via {}; clearing actual value",
                        resolver_addr);
                    if (within_converging_window) {
                        schedule_resolver_config_hash_actual_retry();
                    }
                    break;
                case ResolverConfigHashProbeStatus::INVALID_TXT:
                    Logger::instance().warn(
                        "Resolver config hash TXT is invalid via {}: {}; clearing actual value",
                        resolver_addr,
                        probe_result->raw_txt.value_or("<empty>"));
                    if (within_converging_window) {
                        schedule_resolver_config_hash_actual_retry();
                    }
                    break;
                case ResolverConfigHashProbeStatus::SUCCESS:
                    break;
                }
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
        reset_resolver_actual_state();
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
