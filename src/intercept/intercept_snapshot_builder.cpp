#include "intercept_snapshot_builder.hpp"

#include "../firewall/firewall.hpp"
#include "../lists/domain_index.hpp"

#include <nlohmann/json.hpp>
#include <set>

namespace keen_pbr3 {

namespace {
constexpr std::size_t kMaxDomainNameLength = 255;

uint32_t ttl_seconds_from_ms(uint64_t milliseconds) {
    return static_cast<uint32_t>(milliseconds / 1000U);
}
}

std::vector<InterceptListBinding> build_intercept_bindings(
    const Config& config,
    const std::vector<FirewallSetDeclaration>& sets,
    bool ipv6_enabled,
    const InterceptEffective& effective) {
    std::set<std::string> declared;
    for (const auto& set : sets) {
        declared.insert(set.name);
    }

    static const std::map<std::string, ListConfig> empty_lists;
    const auto& lists = config.lists ? *config.lists : empty_lists;
    const auto route = config.route.value_or(RouteConfig{});

    std::vector<InterceptListBinding> bindings;
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

            InterceptListBinding binding;
            binding.name = list_name;
            binding.target.set_v4 = has4 ? set4 : std::string{};
            binding.target.set_v6 = has6 ? set6 : std::string{};
            const int64_t ttl_ms = list_it->second.ttl_ms.value_or(0);
            binding.target.min_ttl_s = ttl_ms >= 1000 ? static_cast<uint32_t>(ttl_ms / 1000)
                                                      : ttl_seconds_from_ms(effective.min_ttl_ms);
            binding.signature = nlohmann::json(list_it->second).dump();
            bindings.push_back(std::move(binding));
        }
    }
    return bindings;
}

namespace {

void apply_effective_settings(InterceptSnapshot& snapshot, const InterceptEffective& effective,
                              bool ipv6_enabled) {
    snapshot.max_ttl_s = ttl_seconds_from_ms(effective.max_ttl_ms);
    snapshot.marker_domain = effective.marker_domain;
    snapshot.marker_ipv4 = effective.marker_ipv4;
    snapshot.ipv6_enabled = ipv6_enabled;
    snapshot.tls = effective.tls;
    snapshot.http = effective.http;
    snapshot.quic = effective.quic;
}

} // namespace

std::shared_ptr<const InterceptSnapshot> rebind_intercept_snapshot(
    const InterceptSnapshot* previous,
    const std::vector<InterceptListBinding>& bindings,
    const InterceptEffective& effective,
    bool ipv6_enabled) {
    if (previous == nullptr || !previous->index || previous->index->list_names().empty()) {
        return nullptr;
    }
    const auto& names = previous->index->list_names();
    auto snapshot = std::make_shared<InterceptSnapshot>();
    snapshot->index = previous->index;
    snapshot->targets.resize(names.size());  // empty targets match nothing
    snapshot->list_signatures = previous->list_signatures;
    snapshot->list_signatures.resize(names.size());
    for (const auto& binding : bindings) {
        for (std::size_t id = 0; id < names.size(); ++id) {
            if (names[id] == binding.name && snapshot->list_signatures[id] == binding.signature) {
                snapshot->targets[id] = binding.target;
                break;
            }
        }
    }
    apply_effective_settings(*snapshot, effective, ipv6_enabled);
    return snapshot;
}

std::shared_ptr<const InterceptSnapshot> build_intercept_snapshot(
    const Config& config,
    const std::vector<FirewallSetDeclaration>& sets,
    bool ipv6_enabled,
    const InterceptEffective& effective,
    ListStreamer& streamer) {
    static const std::map<std::string, ListConfig> empty_lists;
    const auto& lists = config.lists ? *config.lists : empty_lists;

    DomainIndex::Builder builder;
    auto snapshot = std::make_shared<InterceptSnapshot>();

    for (auto& binding : build_intercept_bindings(config, sets, ipv6_enabled, effective)) {
        const DomainIndex::ListId id = builder.add_list(binding.name);
        FunctionalVisitor collector([&](EntryType type, std::string_view entry) {
            if (type != EntryType::Domain || entry.empty() ||
                entry.size() > kMaxDomainNameLength) {
                return;
            }
            builder.add_domain(id, entry);
        });
        streamer.stream_list_preferring_cache(binding.name, lists.at(binding.name), collector);

        const std::size_t size = static_cast<std::size_t>(id) + 1;
        if (snapshot->targets.size() < size) {
            snapshot->targets.resize(size);
            snapshot->list_signatures.resize(size);
        }
        snapshot->targets[id] = std::move(binding.target);
        snapshot->list_signatures[id] = std::move(binding.signature);
    }

    snapshot->index = std::make_shared<const DomainIndex>(std::move(builder).build());
    apply_effective_settings(*snapshot, effective, ipv6_enabled);
    return snapshot;
}

} // namespace keen_pbr3
