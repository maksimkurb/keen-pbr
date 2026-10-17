#ifdef WITH_API

#include "handler_keenetic_dns.hpp"
#include "generated/api_types.hpp"

#include <nlohmann/json.hpp>

#include <ctime>

namespace keen_pbr3 {

namespace {

std::string iso_timestamp(std::int64_t unix_seconds) {
    const std::time_t seconds = static_cast<std::time_t>(unix_seconds);
    std::tm tm{};
    gmtime_r(&seconds, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

api::KeeneticDnsProtocol upstream_protocol(const std::string& kind) {
    if (kind == "DoH") return api::KeeneticDnsProtocol::DO_H;
    if (kind == "DoT") return api::KeeneticDnsProtocol::DO_T;
    return api::KeeneticDnsProtocol::PLAIN;
}

// Unset optional fields are left out instead of sent as null.
void drop_nulls(nlohmann::json& value) {
    if (value.is_object()) {
        for (auto it = value.begin(); it != value.end();) {
            if (it.value().is_null()) {
                it = value.erase(it);
            } else {
                drop_nulls(it.value());
                ++it;
            }
        }
    } else if (value.is_array()) {
        for (auto& item : value) {
            drop_nulls(item);
        }
    }
}

std::string response_body(const KeeneticDnsCacheView& view) {
    // A read that failed with nothing cached has nothing to show.
    if (!view.snapshot.has_value()) {
        throw ApiError(view.error.empty() ? "Keenetic DNS upstreams are unavailable"
                                          : view.error,
                       502);
    }

    api::KeeneticDnsProxiesResponseClass response;
    for (const auto& proxy : view.snapshot->proxies) {
        api::KeeneticDnsProxyElement element;
        element.name = proxy.name;
        for (const auto& upstream : proxy.upstreams) {
            api::KeeneticDnsProxyUpstreamElement upstream_element;
            upstream_element.protocol = upstream_protocol(upstream.kind);
            upstream_element.target = upstream.target;
            element.upstreams.push_back(std::move(upstream_element));
        }
        if (!proxy.domain_servers.empty()) {
            std::vector<api::KeeneticDnsDomainServersElement> domain_servers;
            for (const auto& entry : proxy.domain_servers) {
                api::KeeneticDnsDomainServersElement item;
                item.domain = entry.domain;
                item.addresses = entry.addresses;
                domain_servers.push_back(std::move(item));
            }
            element.domain_servers = std::move(domain_servers);
        }
        response.proxies.push_back(std::move(element));
    }
    response.fetched_at = iso_timestamp(view.fetched_at_unix);
    if (!view.error.empty()) {
        response.error = view.error;
    }
    nlohmann::json body = response;
    drop_nulls(body);
    return body.dump();
}

} // namespace

void register_keenetic_dns_handler(ApiServer& server, ApiContext& ctx) {
    // GET /api/keenetic/dns-proxies - the built-in DNS proxy as last read.
    server.get("/api/keenetic/dns-proxies", [&ctx]() -> std::string {
        if (!ctx.get_keenetic_dns_proxies_fn) {
            throw ApiError("Keenetic API is not supported by this build", 503);
        }
        return response_body(ctx.get_keenetic_dns_proxies_fn());
    });

    // POST /api/keenetic/dns-proxies/refresh - read the router right now.
    server.post("/api/keenetic/dns-proxies/refresh", [&ctx](const std::string&) -> std::string {
        if (!ctx.refresh_keenetic_dns_fn) {
            throw ApiError("Keenetic API is not supported by this build", 503);
        }
        return response_body(ctx.refresh_keenetic_dns_fn());
    });
}

} // namespace keen_pbr3

#endif // WITH_API
