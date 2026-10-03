#include "intercept_settings.hpp"

#include <arpa/inet.h>

namespace keen_pbr3 {

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
           marker_domain == other.marker_domain && marker_ipv4 == other.marker_ipv4 &&
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

    if (dns.enabled.value_or(true)) {
        if (capabilities.nfqueue) {
            eff.dns_hold = true;
        } else {
            eff.reasons.push_back("dns hold disabled: NFQUEUE/conntrack not available" +
                                  (capabilities.reason.empty() ? std::string{}
                                                               : " (" + capabilities.reason + ")"));
        }
    }
    if (l7.enabled.value_or(true) && (eff.tls || eff.http || eff.quic)) {
        if (capabilities.nflog && capabilities.connbytes) {
            eff.l7 = true;
        } else {
            eff.reasons.push_back(
                std::string("l7 sniffing disabled: ") +
                (!capabilities.nflog ? "NFLOG" : "connbytes") + " not available" +
                (capabilities.reason.empty() ? std::string{}
                                             : " (" + capabilities.reason + ")"));
        }
    }
    return eff;
}

} // namespace keen_pbr3
