#include "daemon.hpp"

#include "../firewall/firewall_runtime.hpp"
#include "../intercept/intercept_report.hpp"
#include "../intercept/intercept_snapshot_builder.hpp"
#include "../log/logger.hpp"
#include "../netfilter/set_writer.hpp"
#include "../util/ipv6_support.hpp"
#ifdef WITH_API
#include "../api/sse_broadcaster.hpp"
#endif

namespace keen_pbr3 {

namespace {

std::shared_ptr<const InterceptSnapshot> make_empty_snapshot(const InterceptEffective& effective) {
    auto snapshot = std::make_shared<InterceptSnapshot>();
    snapshot->index = std::make_shared<const DomainIndex>(DomainIndex::Builder{}.build());
    snapshot->max_ttl_s = effective.max_ttl_s;
    snapshot->marker_domain = effective.marker_domain;
    snapshot->marker_ipv4 = effective.marker_ipv4;
    snapshot->tls = effective.tls;
    snapshot->http = effective.http;
    snapshot->quic = effective.quic;
    return snapshot;
}

InterceptServiceOptions options_for(const InterceptEffective& effective, FirewallBackend backend) {
    InterceptServiceOptions options;
    if (effective.dns_hold) options.queue_num = effective.queue_num;
    if (effective.l7) options.nflog_group = effective.nflog_group;
    options.hold_timeout_ms = effective.hold_timeout_ms;
    options.conntrack_cleanup = effective.conntrack_cleanup;
    options.nft_backend = backend == FirewallBackend::nftables;
    return options;
}

void log_intercept_probe(const InterceptCapabilities& capabilities) {
    const auto& probe = capabilities.probe;
    Logger::instance().info("Interception kernel probe (kernel {}):", probe.kernel_release);
    for (const auto& item : probe.items()) {
        if (item.result.status == nfnl::ProbeStatus::not_run) continue;
        Logger::instance().info("  {}: {}{}{}", item.feature,
                                nfnl::probe_status_name(item.result.status),
                                item.result.reason.empty() ? "" : " - ", item.result.reason);
    }
}

} // namespace

void Daemon::record_intercept_set_write_probe(InterceptEffective& effective,
                                              const nfnl::ProbeResult& set_write,
                                              const nfnl::ProbeResult& timeout_update) {
    apply_set_write_probe(effective, set_write);
    apply_timeout_update_probe(effective, timeout_update);
    // Only a positive, current answer enables the in-place refresh.
    nft_timeout_update_->store(timeout_update.is_ok(), std::memory_order_relaxed);
    if (intercept_capabilities_.has_value()) {
        intercept_capabilities_->probe.set_write = set_write;
        intercept_capabilities_->probe.timeout_update = timeout_update;
    }
    KPBR_LOCK_GUARD(intercept_mutex_);
    intercept_effective_ = effective;
}

InterceptEffective Daemon::resolve_intercept_effective() {
    const FirewallBackend backend = firewall_->backend();
    const bool ipv6_enabled = resolve_ipv6_support(config_).enabled;
    InterceptCapabilities capabilities;
    if (config_.intercept.value_or(InterceptConfig{}).enabled.value_or(true)) {
        if (!intercept_capabilities_.has_value() || intercept_capabilities_stale_ ||
            intercept_capabilities_ipv6_ != ipv6_enabled) {
            auto fresh = probe_intercept_capabilities(backend, ipv6_enabled);
            // Log only what this probe measured, before older verdicts are
            // carried forward.
            log_intercept_probe(fresh);
            if (intercept_capabilities_.has_value()) {
                fresh.probe.carry_forward(intercept_capabilities_->probe);
            }
            intercept_capabilities_ = std::move(fresh);
            intercept_capabilities_ipv6_ = ipv6_enabled;
            intercept_capabilities_stale_ = false;
        }
        capabilities = *intercept_capabilities_;
    }
    auto effective = resolve_effective_intercept(config_, backend, capabilities);
    for (const auto& reason : effective.reasons) {
        Logger::instance().warn("Interception: {}", reason);
    }
    for (const auto& warning : effective.warnings) {
        Logger::instance().warn("Interception (degraded): {}", warning);
    }
    // Without the daemon's DNS hold nothing fills the dynamic sets.
    if (!effective.dns_hold) {
        Logger::instance().warn(
            "DNS interception is unavailable: domains of lists will not be "
            "added to routing sets");
    }
    return effective;
}

void Daemon::fold_intercept_listener_probe(InterceptEffective& effective,
                                           const InterceptService& service) {
    // Fold what the listener binds revealed into the effective settings (before
    // any rule queues to them) and remember it until the next probe refresh, so
    // a kernel that rejected a listener is not retried on every apply.
    const InterceptRuntimeProbe& listeners = service.listener_probe();
    apply_listener_probe(effective, listeners);
    if (intercept_capabilities_.has_value()) {
        auto& cached = intercept_capabilities_->probe;
        if (listeners.nfqueue.status != nfnl::ProbeStatus::not_run) {
            cached.nfqueue = listeners.nfqueue;
            cached.fail_open = listeners.fail_open;
            cached.replacement = listeners.replacement;
        }
        if (listeners.nflog.status != nfnl::ProbeStatus::not_run) {
            cached.nflog = listeners.nflog;
        }
    }
}

void Daemon::start_intercept_service(InterceptEffective& effective) {
    const FirewallBackend backend = firewall_->backend();
    const InterceptServiceOptions options = options_for(effective, backend);
    std::shared_ptr<InterceptService> service;
    const auto fold_listeners = [&] {
        if (!service) return;
        fold_intercept_listener_probe(effective, *service);
        const nfnl::ProbeResult& fail_open = service->listener_probe().fail_open;
        if (fail_open.blocks()) {
            Logger::instance().warn(
                "NFQUEUE fail-open is unavailable ({}); the DNS queue keeps running without it",
                fail_open.reason);
        }
    };
    try {
        auto writer = backend == FirewallBackend::nftables ? nfnl::make_nft_writer()
                                                           : nfnl::make_ipset_writer();
        writer->set_timeout_update_flag(nft_timeout_update_);
        std::unique_ptr<nfnl::DynamicSetWriter> l7_writer;
        if (effective.dns_hold && effective.l7) {
            l7_writer = backend == FirewallBackend::nftables ? nfnl::make_nft_writer()
                                                             : nfnl::make_ipset_writer();
            l7_writer->set_timeout_update_flag(nft_timeout_update_);
        }
        service = std::make_shared<InterceptService>(std::move(writer), std::move(l7_writer));
        service->start(options, make_empty_snapshot(effective));
        const std::size_t reasons_before = effective.reasons.size();
        fold_listeners();
        InterceptServiceOptions bound = options;
        if (!service->dns_bound()) bound.queue_num.reset();
        if (!service->l7_bound()) bound.nflog_group.reset();
        {
            KPBR_LOCK_GUARD(intercept_mutex_);
            intercept_service_ = std::move(service);
        }
        // Record what is actually bound so the next apply compares against it
        // instead of restarting for a part that was disabled by a probe.
        intercept_service_options_ = bound;
        Logger::instance().info("Interception service started (dns hold: {}, l7 sniff: {})",
                                effective.dns_hold ? "on" : "off", effective.l7 ? "on" : "off");
        for (std::size_t i = reasons_before; i < effective.reasons.size(); ++i) {
            Logger::instance().warn("Interception: {}", effective.reasons[i]);
        }
    } catch (const std::exception& error) {
        Logger::instance().error("Interception service failed to start: {}", error.what());
        fold_listeners();
        effective.reasons.push_back(std::string("interception service failed to start: ") +
                                    error.what());
        effective.dns_hold = false;
        effective.l7 = false;
    }
}

void Daemon::quiesce_intercept_service(
    const std::vector<DumpedRoute>& main_routes,
    const std::vector<DumpedInterface>& interfaces,
    const FirewallBalanceCandidates& balance_candidates,
    const Config& quiesce_config,
    const OutboundMarkMap& quiesce_marks) {
    // Packets still queued between the final drain and the unbind would be
    // dropped by the kernel, so the rules that queue must disappear first.
    const auto previous_active = firewall_state_.active_firewall();
    if (previous_active) {
        auto active = apply_runtime_firewall(
            quiesce_config, quiesce_marks, list_service_.cache_manager(), *firewall_,
            FirewallApplyMode::RulesOnly, previous_active.get(), false, main_routes,
            interfaces, &balance_candidates, std::nullopt);
        firewall_state_.publish_active_firewall(std::move(active));
    }
    {
        // The rules apply above may have rebuilt sets; the service stops next,
        // but never leave a window where its cache outlives the sets.
        std::shared_ptr<InterceptService> service;
        {
            KPBR_LOCK_GUARD(intercept_mutex_);
            service = intercept_service_;
        }
        if (service) service->invalidate_set_cache();
    }
    stop_intercept_service();
}

void Daemon::stop_intercept_service() {
    std::shared_ptr<InterceptService> service;
    {
        KPBR_LOCK_GUARD(intercept_mutex_);
        service.swap(intercept_service_);
        intercept_snapshot_seq_.fetch_add(1, std::memory_order_acq_rel);
        intercept_service_options_ = InterceptServiceOptions{};
        intercept_forwarded_seq_ = 0;
    }
    if (service) {
        service->stop();
        Logger::instance().info("Interception service stopped");
    }
}

void Daemon::schedule_intercept_snapshot_update(std::vector<FirewallSetDeclaration> sets,
                                                const InterceptEffective& effective) {
    std::shared_ptr<InterceptService> service;
    std::uint64_t seq = 0;
    {
        KPBR_LOCK_GUARD(intercept_mutex_);
        service = intercept_service_;
        if (service) {
            seq = intercept_snapshot_seq_.fetch_add(1, std::memory_order_acq_rel) + 1;
        }
    }
    if (!service) {
        return;
    }
    const bool ipv6_enabled = firewall_->ipv6_enabled();
    const bool queued = blocking_executor_.try_post(
        "intercept-snapshot",
        [this, service, seq, config = config_, sets = std::move(sets), ipv6_enabled,
         effective]() {
            try {
                ListStreamer streamer(list_service_.cache_manager());
                auto snapshot = build_intercept_snapshot(config, sets, ipv6_enabled, effective,
                                                         streamer);
                {
                    KPBR_LOCK_GUARD(intercept_mutex_);
                    if (intercept_snapshot_seq_.load(std::memory_order_acquire) != seq ||
                        intercept_service_ != service) {
                        return;  // superseded by a newer apply or a stop
                    }
                    service->update_snapshot(snapshot);
                }
                Logger::instance().info(
                    "Interception snapshot updated: {} list(s), {} domain(s)",
                    snapshot->index->list_names().size(), snapshot->index->domain_count());
            } catch (const std::exception& error) {
                Logger::instance().error("Interception snapshot build failed: {}", error.what());
            }
        });
    if (!queued) {
        Logger::instance().warn("Interception snapshot update could not be queued; "
                                "keeping the previous snapshot");
    }
}

InterceptEffective Daemon::intercept_effective_snapshot() const {
    KPBR_LOCK_GUARD(intercept_mutex_);
    return intercept_effective_;
}

api::InterceptHealthClass Daemon::build_intercept_health() const {
    std::shared_ptr<InterceptService> service;
    InterceptEffective effective;
    {
        KPBR_LOCK_GUARD(intercept_mutex_);
        service = intercept_service_;
        effective = intercept_effective_;
    }
    const bool running = service && service->running();
    auto health = make_intercept_health(
        effective, running && (!service || !service->failed()),
        service ? &service->counters() : nullptr,
        service ? service->last_event_seq() : 0,
        service ? service->snapshot_ready() : false);
    if (running && effective.dns_hold && health.counters) {
        health.kernel_queue = read_kernel_queue(effective.queue_num);
    }
    if (service && service->failed()) {
        health.reasons.push_back("interception service stopped after a fatal listener error");
    } else if (service && service->l7_degraded()) {
        health.l7_active = false;
        health.reasons.push_back("L7 interception degraded after NFLOG receive failure");
    }
    return health;
}

void Daemon::pump_intercept_events() {
#ifdef WITH_API
    std::shared_ptr<InterceptService> service;
    {
        KPBR_LOCK_GUARD(intercept_mutex_);
        service = intercept_service_;
    }
    if (!service || !dns_test_broadcaster_) {
        return;
    }
    const std::uint64_t newest = service->last_event_seq();
    if (newest < intercept_forwarded_seq_ || !dns_test_broadcaster_->has_subscribers()) {
        // Service restarted, or nobody listens: do not replay old events later.
        intercept_forwarded_seq_ = newest;
        return;
    }
    // Drain everything the ring still holds; an overwritten run is reported
    // explicitly so clients can tell "no event" from "event lost".
    const auto events =
        service->events_since(intercept_forwarded_seq_, InterceptProcessor::kEventCapacity);
    if (const auto gap = detect_event_gap(intercept_forwarded_seq_, events)) {
        dns_test_broadcaster_->publish(event_gap_to_json(*gap).dump());
    }
    for (const auto& event : events) {
        dns_test_broadcaster_->publish(intercept_event_to_json(event).dump());
        intercept_forwarded_seq_ = event.seq;
    }
#endif
}

} // namespace keen_pbr3
