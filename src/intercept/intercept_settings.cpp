#include "intercept_settings.hpp"

#include <arpa/inet.h>

namespace keen_pbr3 {

namespace {

std::string describe(const nfnl::ProbeResult& result) {
    return result.reason.empty() ? std::string(nfnl::probe_status_name(result.status))
                                 : result.reason;
}

// Recomputes the warnings that depend on probe results.
void refresh_warnings(InterceptEffective& eff) {
    eff.warnings.clear();
    if (!eff.active()) return;
    const auto& probe = eff.capabilities.probe;
    if (eff.dns_hold && probe.fail_open.blocks()) {
        eff.warnings.push_back("fail-open unavailable for the DNS queue: " +
                               describe(probe.fail_open) +
                               "; packets queued while the daemon is stuck are not released by the kernel");
    }
    if (probe.conntrack.blocks()) {
        eff.warnings.push_back("conntrack cleanup disabled: " + describe(probe.conntrack));
    }
}

} // namespace

std::optional<InterceptFirewallSettings> InterceptEffective::firewall_settings() const {
    if (!active()) {
        return std::nullopt;
    }
    InterceptFirewallSettings settings;
    settings.dns_hold = dns_hold;
    settings.queue_num = queue_num;
    settings.l7_sniff = l7;
    settings.nflog_group = nflog_group;
    settings.tls = tls;
    settings.http = http;
    settings.quic = quic;
    return settings;
}

bool InterceptEffective::operator==(const InterceptEffective& other) const {
    return config_enabled == other.config_enabled && dns_hold == other.dns_hold &&
           l7 == other.l7 && queue_num == other.queue_num &&
           nflog_group == other.nflog_group && hold_timeout_ms == other.hold_timeout_ms &&
           min_ttl_s == other.min_ttl_s && max_ttl_s == other.max_ttl_s &&
           conntrack_cleanup == other.conntrack_cleanup && marker_domain == other.marker_domain && marker_ipv4 == other.marker_ipv4 &&
           tls == other.tls && http == other.http && quic == other.quic;
}

InterceptEffective resolve_effective_intercept(const Config& config,
                                               FirewallBackend backend,
                                               const InterceptCapabilities& capabilities) {
    (void)backend;
    InterceptEffective eff;
    eff.capabilities = capabilities;

    const InterceptConfig ic = config.intercept.value_or(InterceptConfig{});
    const InterceptDnsConfig dns = ic.dns.value_or(InterceptDnsConfig{});
    const InterceptL7Config l7 = ic.l7.value_or(InterceptL7Config{});
    const auto marker = dns.marker.value_or(api::Marker{});

    eff.config_enabled = ic.enabled.value_or(true);
    eff.queue_num = static_cast<uint16_t>(dns.queue_num.value_or(9053));
    eff.nflog_group = static_cast<uint16_t>(l7.nflog_group.value_or(9054));
    eff.hold_timeout_ms = static_cast<int>(dns.hold_timeout_ms.value_or(30));
    eff.min_ttl_s = static_cast<uint32_t>(ic.min_ttl_s.value_or(300));
    eff.max_ttl_s = static_cast<uint32_t>(ic.max_ttl_s.value_or(86400));
    eff.marker_domain = marker.domain.value_or("check.keen.pbr");
    in_addr addr{};
    if (inet_pton(AF_INET, marker.answer_ipv4.value_or("127.0.0.88").c_str(), &addr) == 1) {
        const auto* bytes = reinterpret_cast<const uint8_t*>(&addr);
        eff.marker_ipv4 = {bytes[0], bytes[1], bytes[2], bytes[3]};
    }
    eff.tls = l7.tls.value_or(true);
    eff.http = l7.http.value_or(true);
    eff.quic = l7.quic.value_or(true);

    if (!eff.config_enabled) {
        return eff;
    }

    const auto& probe = capabilities.probe;
    eff.conntrack_cleanup = !probe.conntrack.blocks();
    // Probe verdicts that apply to every part: the dynamic sets must be
    // writable.  Listener results (bind) are per part.
    const bool sets_usable = !probe.set_backend.blocks() && !probe.set_write.blocks();
    const nfnl::ProbeResult& sets_probe =
        probe.set_backend.blocks() ? probe.set_backend : probe.set_write;
    const char* sets_name = probe.set_backend.blocks() ? "set backend" : "set write";

    if (dns.enabled.value_or(true)) {
        if (!sets_usable) {
            eff.reasons.push_back(std::string("dns hold disabled: ") + sets_name +
                                  " probe failed: " + describe(sets_probe));
        } else if (probe.nfqueue.blocks()) {
            eff.reasons.push_back("dns hold disabled: NFQUEUE bind failed: " +
                                  describe(probe.nfqueue));
        } else if (capabilities.nfqueue) {
            eff.dns_hold = true;
        } else {
            eff.reasons.push_back("dns hold disabled: NFQUEUE/conntrack not available" +
                                  (capabilities.reason.empty() ? std::string{}
                                                               : " (" + capabilities.reason + ")"));
        }
    }
    if (l7.enabled.value_or(true) && (eff.tls || eff.http || eff.quic)) {
        if (!sets_usable) {
            eff.reasons.push_back(std::string("l7 sniffing disabled: ") + sets_name +
                                  " probe failed: " + describe(sets_probe));
        } else if (probe.nflog.blocks()) {
            eff.reasons.push_back("l7 sniffing disabled: NFLOG bind failed: " +
                                  describe(probe.nflog));
        } else if (capabilities.nflog && capabilities.connbytes) {
            eff.l7 = true;
        } else {
            eff.reasons.push_back(
                std::string("l7 sniffing disabled: ") +
                (!capabilities.nflog ? "NFLOG" : "connbytes") + " not available" +
                (capabilities.reason.empty() ? std::string{}
                                             : " (" + capabilities.reason + ")"));
        }
    }
    refresh_warnings(eff);
    return eff;
}

void apply_listener_probe(InterceptEffective& effective, const InterceptRuntimeProbe& listeners) {
    auto& probe = effective.capabilities.probe;
    if (listeners.nfqueue.status != nfnl::ProbeStatus::not_run) {
        probe.nfqueue = listeners.nfqueue;
        probe.fail_open = listeners.fail_open;
        probe.replacement = listeners.replacement;
        if (effective.dns_hold && listeners.nfqueue.blocks()) {
            effective.dns_hold = false;
            effective.reasons.push_back("dns hold disabled: NFQUEUE bind failed: " +
                                        describe(listeners.nfqueue));
        }
    }
    if (listeners.nflog.status != nfnl::ProbeStatus::not_run) {
        probe.nflog = listeners.nflog;
        if (effective.l7 && listeners.nflog.blocks()) {
            effective.l7 = false;
            effective.reasons.push_back("l7 sniffing disabled: NFLOG bind failed: " +
                                        describe(listeners.nflog));
        }
    }
    refresh_warnings(effective);
}

void apply_timeout_update_probe(InterceptEffective& effective,
                                const nfnl::ProbeResult& timeout_update) {
    effective.capabilities.probe.timeout_update = timeout_update;
}

void apply_set_write_probe(InterceptEffective& effective, const nfnl::ProbeResult& set_write) {
    effective.capabilities.probe.set_write = set_write;
    if (set_write.blocks()) {
        if (effective.dns_hold) {
            effective.reasons.push_back("dns hold disabled: set write probe failed: " +
                                        describe(set_write));
        }
        if (effective.l7) {
            effective.reasons.push_back("l7 sniffing disabled: set write probe failed: " +
                                        describe(set_write));
        }
        effective.dns_hold = false;
        effective.l7 = false;
    }
    refresh_warnings(effective);
}

} // namespace keen_pbr3
