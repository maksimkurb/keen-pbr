#include "daemon.hpp"

#include <algorithm>

#include "../log/logger.hpp"
#include "scheduler.hpp"

#ifdef WITH_API
#include "../api/status_stream.hpp"
#endif

namespace keen_pbr3 {

namespace {

// Wakes up often enough to notice an open dashboard; the router itself is
// only read when someone watches or the last read is older than the idle limit.
constexpr auto kKeeneticDnsPollTick = std::chrono::seconds{60};
constexpr auto kKeeneticDnsIdlePollInterval = std::chrono::minutes{5};

bool config_uses_keenetic_dns(const Config& config) {
    const auto servers = config.dns.value_or(DnsConfig{}).servers.value_or(std::vector<DnsServer>{});
    return std::any_of(servers.begin(), servers.end(), [](const DnsServer& server) {
        return server.type.value_or(api::DnsServerType::STATIC) == api::DnsServerType::KEENETIC;
    });
}

} // namespace

void Daemon::start_keenetic_dns_poll() {
#ifdef USE_KEENETIC_API
    keenetic_dns_poll_task_id_ = scheduler_->schedule_repeating(
        kKeeneticDnsPollTick, [this] { keenetic_dns_poll_tick(); }, "keenetic-dns-poll");
#endif
}

void Daemon::keenetic_dns_poll_tick() {
    if (!config_uses_keenetic_dns(config_)) {
        return;
    }
    bool watched = false;
#ifdef WITH_API
    watched = status_stream_ && status_stream_->has_subscribers();
#endif
    if (!watched) {
        const auto last = keenetic_dns_last_attempt();
        if (last.has_value() &&
            std::chrono::steady_clock::now() - *last < kKeeneticDnsIdlePollInterval) {
            return;
        }
    }
    request_keenetic_dns_poll(std::chrono::seconds{0});
}

void Daemon::request_keenetic_dns_poll(std::chrono::steady_clock::duration max_age) {
    if (!config_uses_keenetic_dns(config_store_.active_config())) {
        return;
    }
    const auto last = keenetic_dns_last_attempt();
    if (last.has_value() && std::chrono::steady_clock::now() - *last < max_age) {
        return;
    }
    bool expected = false;
    if (!keenetic_dns_poll_inflight_.compare_exchange_strong(expected, true,
                                                             std::memory_order_acq_rel)) {
        return;
    }
    const bool queued = blocking_executor_.try_post("keenetic-dns-poll", [this] {
        try {
            (void)refresh_keenetic_dns_now();
        } catch (const std::exception& e) {
            Logger::instance().warn("Keenetic DNS poll failed: {}", e.what());
        }
        keenetic_dns_poll_inflight_.store(false, std::memory_order_release);
    });
    if (!queued) {
        keenetic_dns_poll_inflight_.store(false, std::memory_order_release);
    }
}

KeeneticDnsCacheView Daemon::refresh_keenetic_dns_now() {
    KeeneticDnsCacheView view = refresh_keenetic_dns_cache(/*force_refresh=*/true);
    auto& log = Logger::instance();
    if (!view.error.empty()) {
        // One line per outage, not one per poll.
        if (!keenetic_dns_poll_failing_.exchange(true, std::memory_order_acq_rel)) {
            log.warn("Keenetic DNS proxy groups cannot be read: {}", view.error);
        }
    } else if (keenetic_dns_poll_failing_.exchange(false, std::memory_order_acq_rel)) {
        log.info("Keenetic DNS proxy groups can be read again");
    }
    if (view.status == KeeneticDnsRefreshStatus::UPDATED) {
        post_control_task([this] { on_keenetic_dns_changed(); }, "keenetic-dns-changed");
    }
    return view;
}

void Daemon::on_keenetic_dns_changed() {
    Logger::instance().info("Keenetic DNS proxy groups changed");
    // The content hash makes this a no-op when the generated config is the same.
    if (dnsmasq_integration_enabled(config_)) {
        schedule_dnsmasq_sync("keenetic dns changed", /*explicit_apply=*/false);
    }
#ifdef WITH_API
    // A policy may have appeared or disappeared.
    if (status_stream_) {
        status_stream_->reconcile(StatusUpdate::Issues);
    }
#endif
}

} // namespace keen_pbr3
