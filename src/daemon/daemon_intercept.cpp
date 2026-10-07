#include "daemon.hpp"

#include <algorithm>

#include "../firewall/firewall_runtime.hpp"
#include "../intercept/intercept_report.hpp"
#include "../intercept/intercept_snapshot_builder.hpp"
#include "../log/logger.hpp"
#include "../netfilter/conntrack.hpp"
#include "../netfilter/nfqueue.hpp"
#include "../netfilter/set_writer.hpp"
#include "../util/ipv6_support.hpp"
#include "../util/kernel_capabilities.hpp"
#include "scheduler.hpp"
#ifdef WITH_API
#include "../api/handler_helpers.hpp"
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

void Daemon::probe_capabilities_at_start() {
  auto &log = Logger::instance();
  const FirewallBackend backend = firewall_->backend();
  // Pure PATH lookups: which executables the selected backend needs.
  const std::string missing = host_tools().missing_required(backend);
  if (!missing.empty()) {
    throw DaemonError(std::string("required tool(s) not found in PATH for the ") +
                      firewall_backend_name(backend) + " firewall backend: " +
                      missing + "; install them and restart the service");
  }
  // IPv6 support per backend, iptables xt_comment / xt_statistic.
  const auto capabilities = kernel_capabilities();
  const bool ipv6_supported = capabilities->system_ipv6 &&
                              capabilities->firewall_ipv6(backend);

  // Interception: modules, /proc, netlink set/conntrack checks and the
  // set-write / nft timeout-update checks on a scratch set.
  auto startup_probe = probe_intercept_startup(backend, ipv6_supported);
  log_intercept_probe(startup_probe.with_ipv6);
  const bool queue_available = startup_probe.with_ipv6.nfqueue;
  // The iptables rules lower against the probe's xt_addrtype verdict (cached,
  // never re-probed at apply).
  firewall_->set_addrtype_support(
      startup_probe.for_ipv6(ipv6_supported).addrtype);
  intercept_startup_probe_ = std::move(startup_probe);

  // Kernel facts that need a netlink conversation: ctnetlink dump pre-filter
  // and NFQUEUE GSO.  Both stay "unknown" when the probe cannot run and are
  // then learned once by the first real use.
  nfnl::probe_conntrack_kernel_filter();
  if (queue_available) {
    nfnl::probe_nfqueue_gso(static_cast<std::uint16_t>(
        config_.intercept.value_or(InterceptConfig{}).dns
            .value_or(InterceptDnsConfig{}).queue_num.value_or(9053)));
  }
  log.info("Kernel capability probes done; they are not repeated until restart "
           "(conntrack pre-filter unsupported={}, NFQUEUE GSO rejected={})",
           nfnl::conntrack_kernel_filter_unsupported(), nfnl::nfqueue_gso_rejected());
}

InterceptEffective Daemon::resolve_intercept_effective() {
    const FirewallBackend backend = firewall_->backend();
    // Both inputs are answers cached at service start; nothing is probed here.
    const bool ipv6_enabled = resolve_ipv6_support(config_).enabled;
    InterceptCapabilities capabilities;
    if (config_.intercept.value_or(InterceptConfig{}).enabled.value_or(true) &&
        intercept_startup_probe_.has_value()) {
        capabilities = intercept_startup_probe_->for_ipv6(ipv6_enabled);
        // Listener bind results are not kernel capabilities: they come from the
        // binds this daemon did (see forget_blocking_listener_results).
        auto& probe = capabilities.probe;
        const auto& bound = intercept_listener_results_;
        if (bound.nfqueue.status != nfnl::ProbeStatus::not_run) {
            probe.nfqueue = bound.nfqueue;
            probe.fail_open = bound.fail_open;
            probe.replacement = bound.replacement;
        }
        if (bound.nflog.status != nfnl::ProbeStatus::not_run) probe.nflog = bound.nflog;
    }
    // The addrtype verdict is a firewall fact, valid even with interception off.
    if (intercept_startup_probe_.has_value()) {
        capabilities.addrtype = intercept_startup_probe_->for_ipv6(ipv6_enabled).addrtype;
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
    // any rule queues to them) and remember it, so a kernel that rejected a
    // listener is not retried on every apply (a re-bind or config apply
    // forgets blocking results and retries the bind, never the probes).
    const InterceptRuntimeProbe& listeners = service.listener_probe();
    apply_listener_probe(effective, listeners);
    auto& cached = intercept_listener_results_;
    if (listeners.nfqueue.status != nfnl::ProbeStatus::not_run) {
        cached.nfqueue = listeners.nfqueue;
        cached.fail_open = listeners.fail_open;
        cached.replacement = listeners.replacement;
    }
    if (listeners.nflog.status != nfnl::ProbeStatus::not_run) {
        cached.nflog = listeners.nflog;
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
        // Startup nft_timeout_update probe: nft writers extend timeouts in
        // place when the kernel proved it, else delete+add.
        const bool nft_in_place_refresh =
            intercept_startup_probe_.has_value() &&
            intercept_startup_probe_->with_ipv6.probe.timeout_update.is_ok();
#ifdef KEEN_PBR_PLATFORM_KEENETIC
        auto writer = nfnl::make_ipset_writer();
#else
        auto writer = backend == FirewallBackend::nftables ? nfnl::make_nft_writer("KeenPbrTable", nft_in_place_refresh)
                                                           : nfnl::make_ipset_writer();
#endif
        std::unique_ptr<nfnl::DynamicSetWriter> l7_writer;
        if (effective.dns_hold && effective.l7) {
#ifdef KEEN_PBR_PLATFORM_KEENETIC
            l7_writer = nfnl::make_ipset_writer();
#else
            l7_writer = backend == FirewallBackend::nftables ? nfnl::make_nft_writer("KeenPbrTable", nft_in_place_refresh)
                                                             : nfnl::make_ipset_writer();
#endif
        }
        service = std::make_shared<InterceptService>(
            std::move(writer), std::move(l7_writer), intercept_counters_);
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

// Everything a (re)try of the background index build needs, captured at apply
// time so a later retry never reads live daemon state.
struct Daemon::InterceptSnapshotJob {
    std::shared_ptr<InterceptService> service;
    std::uint64_t seq{0};
    Config config;
    std::vector<FirewallSetDeclaration> sets;
    bool ipv6_enabled{false};
    InterceptEffective effective;
};

namespace {
constexpr std::chrono::seconds kSnapshotRetryInitial{5};
constexpr std::chrono::seconds kSnapshotRetryMax{60};
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

    // Cheap half, synchronously (the caller still holds the write pause): keep
    // the previous domain index but bind every list to the sets of the new
    // configuration, so learning continues while the new index is built.
    try {
        const auto bindings = build_intercept_bindings(config_, sets, ipv6_enabled, effective);
        const auto previous = service->current_snapshot();
        if (auto rebound = rebind_intercept_snapshot(previous.get(), bindings, effective, ipv6_enabled)) {
            KPBR_LOCK_GUARD(intercept_mutex_);
            if (intercept_service_ == service) service->update_snapshot(std::move(rebound));
        }
    } catch (const std::exception& error) {
        // The apply guard drops the snapshot instead: no learning until the
        // background build below (or its retry) publishes one.
        Logger::instance().error("Interception snapshot rebind failed: {}", error.what());
        service->invalidate_snapshot();
    }

    auto job = std::make_shared<InterceptSnapshotJob>();
    job->service = std::move(service);
    job->seq = seq;
    job->config = config_;
    job->sets = std::move(sets);
    job->ipv6_enabled = ipv6_enabled;
    job->effective = effective;
    queue_intercept_snapshot_build(std::move(job), 0);
}

void Daemon::queue_intercept_snapshot_build(std::shared_ptr<const InterceptSnapshotJob> job,
                                            unsigned attempt) {
    {
        KPBR_LOCK_GUARD(intercept_mutex_);
        if (intercept_snapshot_seq_.load(std::memory_order_acquire) != job->seq ||
            intercept_service_ != job->service) {
            return;  // superseded by a newer apply or a stop
        }
    }
    const bool queued = blocking_executor_.try_post(
        "intercept-snapshot", [this, job, attempt]() {
            try {
                ListStreamer streamer(list_service_.cache_manager());
                auto snapshot = build_intercept_snapshot(job->config, job->sets,
                                                         job->ipv6_enabled, job->effective,
                                                         streamer);
                {
                    KPBR_LOCK_GUARD(intercept_mutex_);
                    if (intercept_snapshot_seq_.load(std::memory_order_acquire) != job->seq ||
                        intercept_service_ != job->service) {
                        return;  // superseded by a newer apply or a stop
                    }
                    job->service->update_snapshot(snapshot);
                }
                Logger::instance().info(
                    "Interception snapshot updated: {} list(s), {} domain(s)",
                    snapshot->index->list_names().size(), snapshot->index->domain_count());
            } catch (const std::exception& error) {
                Logger::instance().error("Interception snapshot build failed: {}", error.what());
                retry_intercept_snapshot_build(job, attempt);
            }
        });
    if (!queued) {
        Logger::instance().warn("Interception snapshot build could not be queued; "
                                "keeping the published snapshot and retrying");
        retry_intercept_snapshot_build(std::move(job), attempt);
    }
}

void Daemon::retry_intercept_snapshot_build(std::shared_ptr<const InterceptSnapshotJob> job,
                                            unsigned attempt) {
    // Exponential backoff 5 s, 10 s, 20 s, 40 s, 60 s, ...  The timer is armed
    // on the event loop (the scheduler is not meant to be driven from the
    // blocking executor); a newer apply or a stop supersedes the chain via
    // intercept_snapshot_seq_ checked in queue_intercept_snapshot_build().
    auto delay = kSnapshotRetryInitial;
    for (unsigned i = 0; i < attempt && delay < kSnapshotRetryMax; ++i) delay *= 2;
    delay = std::min(delay, kSnapshotRetryMax);
    const bool posted = post_control_task(
        [this, job, attempt, delay]() {
            if (!scheduler_) return;
            scheduler_->schedule_oneshot(
                std::chrono::duration_cast<std::chrono::milliseconds>(delay),
                [this, job, attempt]() { queue_intercept_snapshot_build(job, attempt + 1); },
                "intercept-snapshot-retry");
        },
        "intercept-snapshot-retry");
    if (!posted) {
        Logger::instance().warn("Interception snapshot retry could not be scheduled");
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
    InterceptRebindStatus rebind;
    {
        KPBR_LOCK_GUARD(intercept_mutex_);
        rebind = intercept_rebind_status_;
    }
    if (rebind.active) {
        const auto left = std::max(std::chrono::steady_clock::duration::zero(),
                                   rebind.next_due - std::chrono::steady_clock::now());
        health.reasons.push_back(
            "interception listener degraded, re-binding (next attempt " +
            std::to_string(rebind.attempts + 1) + " in " +
            std::to_string(std::chrono::ceil<std::chrono::seconds>(left).count()) + "s)");
    }
    if (service && service->failed()) {
        health.reasons.push_back("interception service stopped after a fatal listener error");
    } else if (service && service->l7_degraded()) {
        health.l7_active = false;
        health.reasons.push_back("L7 interception degraded after NFLOG receive failure");
    }
    return health;
}

void Daemon::publish_intercept_rebind_status() {
    KPBR_LOCK_GUARD(intercept_mutex_);
    intercept_rebind_status_.active = intercept_rebind_.pending();
    intercept_rebind_status_.attempts = intercept_rebind_.attempts();
    intercept_rebind_status_.next_due = intercept_rebind_.next_due();
}

void Daemon::tick_intercept_rebind() {
    if (!routing_runtime_active_) {
        intercept_rebind_.reset();
        publish_intercept_rebind_status();
        return;
    }
    std::shared_ptr<InterceptService> service;
    InterceptEffective effective;
    {
        KPBR_LOCK_GUARD(intercept_mutex_);
        service = intercept_service_;
        effective = intercept_effective_;
    }
    const auto now = std::chrono::steady_clock::now();
    bool healthy;
    if (intercept_rebind_.pending() || intercept_rebind_.attempts() > 0) {
        // Tracking a recovery: healthy again only once every listener that
        // was wanted when it failed is bound.
        healthy = service && service->running() && !service->failed() &&
                  (!intercept_rebind_want_dns_ || service->dns_bound()) &&
                  (!intercept_rebind_want_l7_ || service->l7_bound());
    } else {
        // An NFLOG failure leaves the hot loop alive for DNS, so only the
        // wanted L7 listener counts as degraded.
        healthy = !(service && (service->failed() ||
                                (effective.l7 && service->l7_degraded())));
        if (!healthy) {
            intercept_rebind_want_dns_ = effective.dns_hold;
            intercept_rebind_want_l7_ = effective.l7;
            Logger::instance().warn("Interception listener failed; re-binding with backoff");
        }
    }
    intercept_rebind_.observe(healthy, now);
    if (intercept_rebind_.due(now)) {
#ifdef WITH_API
        // Same lease an apply holds: when one is running it will rebind the
        // listeners itself, so skip and look again on the next tick.
        if (!operation_coordinator_.try_begin("intercept-rebind")) {
            publish_intercept_rebind_status();
            return;
        }
#endif
        // Headless builds run every lifecycle operation on this control
        // thread, so nothing can overlap the re-bind there.
        Logger::instance().info("Interception: re-binding listeners (attempt {})",
                                intercept_rebind_.attempts() + 1);
        intercept_rebind_in_progress_ = true;
        // A failed bind is cached as a blocking verdict; forget it so the
        // bind is retried.  The kernel capabilities stay as probed at start.
        intercept_listener_results_.forget_blocking_listener_results();
        try {
            apply_firewall(runtime_refresh_firewall_mode());
            publish_runtime_state();
        } catch (const std::exception& error) {
            Logger::instance().error("Interception re-bind failed: {}", error.what());
        }
        intercept_rebind_in_progress_ = false;
#ifdef WITH_API
        operation_coordinator_.finish();
#endif
        intercept_rebind_.attempted(std::chrono::steady_clock::now());
    }
    publish_intercept_rebind_status();
}

void Daemon::pump_intercept_events() {
    std::shared_ptr<InterceptService> service;
    {
        KPBR_LOCK_GUARD(intercept_mutex_);
        service = intercept_service_;
    }
    if (service) {
        // The hot thread only records; the timeout warnings are logged here.
        service->log_hold_timeouts();
    }
#ifdef WITH_API
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
        dns_test_broadcaster_->publish(safe_dump(event_gap_to_json(*gap)));
    }
    for (const auto& event : events) {
        SseMessageMeta meta;
        meta.source = intercept_source_name(event.source);
        meta.domain = event.domain;
        dns_test_broadcaster_->publish(safe_dump(intercept_event_to_json(event)), meta);
        intercept_forwarded_seq_ = event.seq;
    }
#endif
}

} // namespace keen_pbr3
