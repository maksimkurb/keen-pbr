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
    options.nft_backend = backend == FirewallBackend::nftables;
    return options;
}

} // namespace

InterceptEffective Daemon::resolve_intercept_effective() {
    const FirewallBackend backend = firewall_->backend();
    const bool ipv6_enabled = resolve_ipv6_support(config_).enabled;
    InterceptCapabilities capabilities;
    if (config_.intercept.value_or(InterceptConfig{}).enabled.value_or(true)) {
        if (!intercept_capabilities_.has_value() ||
            intercept_capabilities_ipv6_ != ipv6_enabled) {
            intercept_capabilities_ = probe_intercept_capabilities(backend, ipv6_enabled);
            intercept_capabilities_ipv6_ = ipv6_enabled;
        }
        capabilities = *intercept_capabilities_;
    }
    auto effective = resolve_effective_intercept(config_, backend, capabilities);
    for (const auto& reason : effective.reasons) {
        Logger::instance().warn("Interception: {}", reason);
    }
    // Without the daemon's DNS hold nothing fills the dynamic sets unless the
    // dnsmasq integration (ipset=/nftset= fallback) is enabled.
    if (!effective.dns_hold &&
        effective_resolver_integration(config_) == ResolverIntegrationMode::NONE) {
        Logger::instance().warn(
            "DNS interception is unavailable and dns.resolver_integration is 'none': "
            "domains of lists will not be added to routing sets");
    }
    return effective;
}

void Daemon::start_intercept_service(InterceptEffective& effective) {
    const FirewallBackend backend = firewall_->backend();
    const InterceptServiceOptions options = options_for(effective, backend);
    try {
        auto writer = backend == FirewallBackend::nftables ? nfnl::make_nft_writer()
                                                           : nfnl::make_ipset_writer();
        auto service = std::make_shared<InterceptService>(std::move(writer));
        service->start(options, make_empty_snapshot(effective));
        {
            KPBR_LOCK_GUARD(intercept_mutex_);
            intercept_service_ = std::move(service);
        }
        intercept_service_options_ = options;
        Logger::instance().info("Interception service started (dns hold: {}, l7 sniff: {})",
                                effective.dns_hold ? "on" : "off", effective.l7 ? "on" : "off");
    } catch (const std::exception& error) {
        Logger::instance().error("Interception service failed to start: {}", error.what());
        effective.reasons.push_back(std::string("interception service failed to start: ") +
                                    error.what());
        effective.dns_hold = false;
        effective.l7 = false;
    }
}

void Daemon::quiesce_intercept_service(
    const std::vector<DumpedRoute>& main_routes,
    const std::vector<DumpedInterface>& interfaces,
    const FirewallBalanceCandidates& balance_candidates) {
    // Packets still queued between the final drain and the unbind would be
    // dropped by the kernel, so the rules that queue must disappear first.
    const auto previous_active = firewall_state_.active_firewall();
    if (previous_active) {
        try {
            auto active = apply_runtime_firewall(
                config_, outbound_marks_, list_service_.cache_manager(), *firewall_,
                FirewallApplyMode::RulesOnly, previous_active.get(), false, main_routes,
                interfaces, &balance_candidates, std::nullopt);
            firewall_state_.publish_active_firewall(std::move(active));
        } catch (const std::exception& error) {
            Logger::instance().warn(
                "Interception: detaching firewall rules before restart failed: {}", error.what());
        }
    }
    stop_intercept_service();
}

void Daemon::stop_intercept_service() {
    std::shared_ptr<InterceptService> service;
    {
        KPBR_LOCK_GUARD(intercept_mutex_);
        service.swap(intercept_service_);
    }
    intercept_snapshot_seq_.fetch_add(1, std::memory_order_acq_rel);
    intercept_service_options_ = InterceptServiceOptions{};
    intercept_forwarded_seq_ = 0;
    if (service) {
        service->stop();
        Logger::instance().info("Interception service stopped");
    }
}

void Daemon::schedule_intercept_snapshot_update(std::vector<FirewallSetDeclaration> sets,
                                                const InterceptEffective& effective) {
    std::shared_ptr<InterceptService> service;
    {
        KPBR_LOCK_GUARD(intercept_mutex_);
        service = intercept_service_;
    }
    if (!service) {
        return;
    }
    const std::uint64_t seq =
        intercept_snapshot_seq_.fetch_add(1, std::memory_order_acq_rel) + 1;
    const bool ipv6_enabled = firewall_->ipv6_enabled();
    const bool queued = blocking_executor_.try_post(
        "intercept-snapshot",
        [this, service, seq, config = config_, sets = std::move(sets), ipv6_enabled,
         effective]() {
            try {
                ListStreamer streamer(list_service_.cache_manager());
                auto snapshot = build_intercept_snapshot(config, sets, ipv6_enabled, effective,
                                                         streamer);
                if (intercept_snapshot_seq_.load(std::memory_order_acquire) != seq) {
                    return;  // superseded by a newer apply or a stop
                }
                service->update_snapshot(snapshot);
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
    return make_intercept_health(effective, running, service ? &service->counters() : nullptr,
                                 service ? service->last_event_seq() : 0);
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
    for (const auto& event : service->events_since(intercept_forwarded_seq_, 128)) {
        dns_test_broadcaster_->publish(intercept_event_to_json(event).dump());
        intercept_forwarded_seq_ = event.seq;
    }
#endif
}

} // namespace keen_pbr3
