#include "intercept_snapshot_builder.hpp"

#include "../firewall/firewall.hpp"
#include "../lists/domain_index.hpp"

#include <set>

namespace keen_pbr3 {

namespace {
constexpr std::size_t kMaxDomainNameLength = 255;
}

std::shared_ptr<const InterceptSnapshot> build_intercept_snapshot(
    const Config& config,
    const std::vector<FirewallSetDeclaration>& sets,
    bool ipv6_enabled,
    const InterceptEffective& effective,
    ListStreamer& streamer) {
    std::set<std::string> declared;
    for (const auto& set : sets) {
        declared.insert(set.name);
    }

    static const std::map<std::string, ListConfig> empty_lists;
    const auto& lists = config.lists ? *config.lists : empty_lists;
    const auto route = config.route.value_or(RouteConfig{});

    DomainIndex::Builder builder;
    auto snapshot = std::make_shared<InterceptSnapshot>();
    std::set<std::string> seen;

    for (const auto& rule : route.rules.value_or(std::vector<RouteRule>{})) {
        if (!route_rule_enabled(rule)) {
            continue;
        }
        for (const auto& list_name : route_rule_lists(rule)) {
            const auto list_it = lists.find(list_name);
            if (list_it == lists.end() || !seen.insert(list_name).second) {
                continue;
            }
            const std::string set4 = Firewall::dynamic_set_name(list_name, AF_INET);
            const std::string set6 = Firewall::dynamic_set_name(list_name, AF_INET6);
            const bool has4 = declared.count(set4) > 0;
            const bool has6 = ipv6_enabled && declared.count(set6) > 0;
            if (!has4 && !has6) {
                continue;  // no domain entries: nothing for the daemon to fill
            }

            const DomainIndex::ListId id = builder.add_list(list_name);
            FunctionalVisitor collector([&](EntryType type, std::string_view entry) {
                if (type != EntryType::Domain || entry.empty() ||
                    entry.size() > kMaxDomainNameLength) {
                    return;
                }
                builder.add_domain(id, entry);
            });
            streamer.stream_list_preferring_cache(list_name, list_it->second, collector);

            InterceptListTarget target;
            target.set_v4 = has4 ? set4 : std::string{};
            target.set_v6 = has6 ? set6 : std::string{};
            const int64_t ttl_ms = list_it->second.ttl_ms.value_or(0);
            target.min_ttl_s = ttl_ms >= 1000 ? static_cast<uint32_t>(ttl_ms / 1000)
                                              : effective.min_ttl_s;
            if (snapshot->targets.size() <= id) {
                snapshot->targets.resize(static_cast<std::size_t>(id) + 1);
            }
            snapshot->targets[id] = std::move(target);
        }
    }

    snapshot->index = std::make_shared<const DomainIndex>(std::move(builder).build());
    snapshot->max_ttl_s = effective.max_ttl_s;
    snapshot->marker_domain = effective.marker_domain;
    snapshot->marker_ipv4 = effective.marker_ipv4;
    snapshot->tls = effective.tls;
    snapshot->http = effective.http;
    snapshot->quic = effective.quic;
    return snapshot;
}

} // namespace keen_pbr3
