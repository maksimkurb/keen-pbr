#ifdef WITH_API

#include "runtime_outbound_state.hpp"

#include "../config/routing_state.hpp"
#include "../health/circuit_breaker.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <vector>

namespace keen_pbr3 {

namespace {

const Outbound* find_outbound(const std::vector<Outbound>& outbounds,
                              const std::string& tag) {
    for (const auto& outbound : outbounds) {
        if (outbound.tag == tag) {
            return &outbound;
        }
    }
    return nullptr;
}

uint32_t safe_table_id(uint32_t table_start, uint32_t offset) {
    uint32_t id = table_start;
    uint32_t count = 0;
    while (true) {
        if (!is_reserved_table(id)) {
            if (count == offset) {
                return id;
            }
            ++count;
        }
        ++id;
    }
}

std::vector<const Outbound*> ordered_urltest_children(const std::vector<Outbound>& outbounds,
                                                      const Outbound& urltest) {
    std::vector<const Outbound*> ordered;
    if (!urltest.outbound_groups.has_value()) {
        return ordered;
    }

    struct GroupRef {
        size_t index;
        int64_t weight;
    };

    std::vector<GroupRef> groups;
    groups.reserve(urltest.outbound_groups->size());
    for (size_t index = 0; index < urltest.outbound_groups->size(); ++index) {
        groups.push_back({index, urltest.outbound_groups->at(index).weight.value_or(1)});
    }

    std::stable_sort(groups.begin(), groups.end(), [](const GroupRef& lhs, const GroupRef& rhs) {
        return lhs.weight < rhs.weight;
    });

    for (const auto& group : groups) {
        for (const auto& child_tag : urltest.outbound_groups->at(group.index).outbounds) {
            const Outbound* child = find_outbound(outbounds, child_tag);
            if (child) {
                ordered.push_back(child);
            }
        }
    }

    return ordered;
}

const DumpedRoute* find_primary_default_route(const std::vector<DumpedRoute>& routes) {
    const DumpedRoute* primary = nullptr;
    uint32_t best_metric = std::numeric_limits<uint32_t>::max();

    for (const auto& route : routes) {
        if (route.destination != "default" || route.blackhole || route.unreachable) {
            continue;
        }

        if (primary == nullptr || route.metric < best_metric) {
            primary = &route;
            best_metric = route.metric;
        }
    }

    return primary;
}

bool route_matches_outbound(const DumpedRoute& route, const Outbound& outbound) {
    if (route.destination != "default" || route.blackhole || route.unreachable) {
        return false;
    }

    if (outbound.type != OutboundType::INTERFACE) {
        return false;
    }

    if (!route.interface.has_value() || route.interface != outbound.interface) {
        return false;
    }

    if (outbound.gateway.has_value()) {
        return route.gateway == outbound.gateway;
    }

    return true;
}

api::RuntimeInterfaceStatusEnum map_urltest_child_status(
    const Outbound& child,
    bool reachable,
    bool is_active,
    const std::optional<UrltestState>& urltest_state) {
    if (!child.interface.has_value()) {
        return api::RuntimeInterfaceStatusEnum::UNKNOWN;
    }

    if (is_active) {
        return api::RuntimeInterfaceStatusEnum::ACTIVE;
    }

    if (!reachable) {
        return api::RuntimeInterfaceStatusEnum::UNAVAILABLE;
    }

    if (!urltest_state.has_value()) {
        return api::RuntimeInterfaceStatusEnum::BACKUP;
    }

    const auto result_it = urltest_state->last_results.find(child.tag);
    const auto breaker_it = urltest_state->circuit_breakers.find(child.tag);
    const bool breaker_open =
        breaker_it != urltest_state->circuit_breakers.end() &&
        breaker_it->second.state(child.tag) == CircuitState::open;

    if (breaker_open) {
        return api::RuntimeInterfaceStatusEnum::UNAVAILABLE;
    }

    if (result_it == urltest_state->last_results.end()) {
        return api::RuntimeInterfaceStatusEnum::UNKNOWN;
    }

    if (result_it->second.success) {
        return api::RuntimeInterfaceStatusEnum::BACKUP;
    }

    return api::RuntimeInterfaceStatusEnum::DEGRADED;
}

api::RuntimeOutboundStatusEnum derive_overall_status(
    const std::vector<api::RuntimeInterfaceState>& interfaces,
    bool has_active_interface,
    bool has_live_route) {
    if (has_active_interface) {
        return api::RuntimeOutboundStatusEnum::HEALTHY;
    }

    bool has_backup = false;
    bool has_degraded = false;

    for (const auto& interface_state : interfaces) {
        if (interface_state.status == api::RuntimeInterfaceStatusEnum::BACKUP) {
            has_backup = true;
        } else if (interface_state.status == api::RuntimeInterfaceStatusEnum::DEGRADED) {
            has_degraded = true;
        }
    }

    if (has_live_route && (has_backup || has_degraded)) {
        return api::RuntimeOutboundStatusEnum::DEGRADED;
    }

    if (has_backup) {
        return api::RuntimeOutboundStatusEnum::DEGRADED;
    }

    if (has_degraded) {
        return api::RuntimeOutboundStatusEnum::DEGRADED;
    }

    if (!interfaces.empty()) {
        return api::RuntimeOutboundStatusEnum::UNAVAILABLE;
    }

    return has_live_route
        ? api::RuntimeOutboundStatusEnum::HEALTHY
        : api::RuntimeOutboundStatusEnum::UNKNOWN;
}

std::optional<uint32_t> outbound_table_id(const Config& config,
                                          const std::vector<Outbound>& outbounds,
                                          const std::string& outbound_tag) {
    const uint32_t table_start = static_cast<uint32_t>(
        config.iproute.value_or(IprouteConfig{}).table_start.value_or(100));

    uint32_t table_offset = 0;
    for (const auto& outbound : outbounds) {
        const bool routable =
            (outbound.type == OutboundType::INTERFACE ||
             outbound.type == OutboundType::TABLE ||
             outbound.type == OutboundType::URLTEST);
        if (!routable) {
            continue;
        }

        uint32_t table_id = 0;
        if (outbound.type == OutboundType::TABLE) {
            table_id = static_cast<uint32_t>(outbound.table.value_or(0));
        } else {
            table_id = safe_table_id(table_start, table_offset);
        }
        ++table_offset;

        if (outbound.tag == outbound_tag) {
            return table_id;
        }
    }

    return std::nullopt;
}

api::RuntimeOutboundStateElement build_interface_outbound_state(const Config& config,
                                                                const Outbound& outbound,
                                                                NetlinkManager& netlink) {
    api::RuntimeOutboundStateElement state;
    state.tag = outbound.tag;
    state.type = outbound.type;

    const auto table_id = outbound_table_id(config, config.outbounds.value_or(std::vector<Outbound>{}), outbound.tag);
    std::vector<DumpedRoute> routes;
    if (table_id.has_value()) {
        routes = netlink.dump_routes_in_table(*table_id);
    }
    const DumpedRoute* primary_route = find_primary_default_route(routes);

    api::RuntimeInterfaceState interface_state;
    interface_state.outbound_tag = outbound.tag;
    interface_state.interface_name = outbound.interface;
    const bool reachable = is_interface_outbound_reachable(outbound, netlink);
    const bool active = primary_route != nullptr && route_matches_outbound(*primary_route, outbound);
    interface_state.status = active
        ? api::RuntimeInterfaceStatusEnum::ACTIVE
        : reachable
            ? api::RuntimeInterfaceStatusEnum::BACKUP
            : api::RuntimeInterfaceStatusEnum::UNAVAILABLE;

    if (!reachable) {
        interface_state.detail = std::string("interface is not reachable from the main routing table");
    } else if (!active && primary_route == nullptr) {
        interface_state.detail = std::string("no active default route is installed in the outbound table");
    }

    state.interfaces = std::vector<api::RuntimeInterfaceState>{interface_state};
    state.status = derive_overall_status(state.interfaces, active, primary_route != nullptr);
    return state;
}

api::RuntimeOutboundStateElement build_table_outbound_state(const Config& config,
                                                            const Outbound& outbound,
                                                            NetlinkManager& netlink) {
    api::RuntimeOutboundStateElement state;
    state.tag = outbound.tag;
    state.type = outbound.type;

    const auto table_id = outbound_table_id(config, config.outbounds.value_or(std::vector<Outbound>{}), outbound.tag);
    std::vector<DumpedRoute> routes;
    if (table_id.has_value()) {
        routes = netlink.dump_routes_in_table(*table_id);
    }
    const DumpedRoute* primary_route = find_primary_default_route(routes);

    state.status = primary_route != nullptr
        ? api::RuntimeOutboundStatusEnum::HEALTHY
        : api::RuntimeOutboundStatusEnum::UNKNOWN;
    return state;
}

api::RuntimeOutboundStateElement build_urltest_outbound_state(const Config& config,
                                                              const Outbound& outbound,
                                                              NetlinkManager& netlink,
                                                              const UrltestStateLookupFn& urltest_state_lookup) {
    api::RuntimeOutboundStateElement state;
    state.tag = outbound.tag;
    state.type = outbound.type;

    const auto all_outbounds = config.outbounds.value_or(std::vector<Outbound>{});
    const auto children = ordered_urltest_children(all_outbounds, outbound);
    const auto table_id = outbound_table_id(config, all_outbounds, outbound.tag);

    std::vector<DumpedRoute> routes;
    if (table_id.has_value()) {
        routes = netlink.dump_routes_in_table(*table_id);
    }
    const DumpedRoute* primary_route = find_primary_default_route(routes);

    const auto urltest_state = urltest_state_lookup(outbound.tag);
    std::string live_active_child_tag;

    if (primary_route != nullptr) {
        for (const Outbound* child : children) {
            if (child != nullptr && route_matches_outbound(*primary_route, *child)) {
                live_active_child_tag = child->tag;
                break;
            }
        }
    }

    std::vector<api::RuntimeInterfaceState> interfaces;
    interfaces.reserve(children.size());

    for (const Outbound* child : children) {
        if (child == nullptr) {
            continue;
        }

        api::RuntimeInterfaceState interface_state;
        interface_state.outbound_tag = child->tag;
        interface_state.interface_name = child->interface;
        const bool reachable =
            child->type == OutboundType::INTERFACE
                ? is_interface_outbound_reachable(*child, netlink)
                : false;
        const bool is_active = !live_active_child_tag.empty() && live_active_child_tag == child->tag;
        interface_state.status = map_urltest_child_status(*child, reachable, is_active, urltest_state);

        if (urltest_state.has_value()) {
            const auto result_it = urltest_state->last_results.find(child->tag);
            if (result_it != urltest_state->last_results.end()) {
                interface_state.latency_ms = static_cast<int64_t>(result_it->second.latency_ms);
                if (!result_it->second.error.empty()) {
                    interface_state.detail = result_it->second.error;
                }
            }
        }

        if (interface_state.detail == std::nullopt &&
            interface_state.status == api::RuntimeInterfaceStatusEnum::UNAVAILABLE &&
            child->type == OutboundType::INTERFACE) {
            interface_state.detail = std::string("interface is not reachable from the main routing table");
        }

        interfaces.push_back(std::move(interface_state));
    }

    state.interfaces = std::move(interfaces);

    if (primary_route != nullptr &&
        urltest_state.has_value() &&
        !urltest_state->selected_outbound.empty() &&
        !live_active_child_tag.empty() &&
        urltest_state->selected_outbound != live_active_child_tag) {
        state.detail = "live route selection differs from urltest manager selection";
    }

    state.status = derive_overall_status(
        state.interfaces,
        !live_active_child_tag.empty(),
        primary_route != nullptr);
    return state;
}

} // namespace

api::RuntimeOutboundsResponse build_runtime_outbounds_response(
    const Config& config,
    const FirewallState& firewall_state,
    NetlinkManager& netlink,
    const UrltestStateLookupFn& urltest_state_lookup) {
    (void)firewall_state;

    api::RuntimeOutboundsResponse response;
    const auto outbounds = config.outbounds.value_or(std::vector<Outbound>{});
    response.outbounds.reserve(outbounds.size());

    for (const auto& outbound : outbounds) {
        switch (outbound.type) {
            case OutboundType::INTERFACE:
                response.outbounds.push_back(
                    build_interface_outbound_state(config, outbound, netlink));
                break;
            case OutboundType::TABLE:
                response.outbounds.push_back(
                    build_table_outbound_state(config, outbound, netlink));
                break;
            case OutboundType::URLTEST:
                response.outbounds.push_back(
                    build_urltest_outbound_state(config, outbound, netlink, urltest_state_lookup));
                break;
            case OutboundType::BLACKHOLE:
            case OutboundType::IGNORE: {
                api::RuntimeOutboundStateElement state;
                state.tag = outbound.tag;
                state.type = outbound.type;
                state.status = api::RuntimeOutboundStatusEnum::UNKNOWN;
                response.outbounds.push_back(std::move(state));
                break;
            }
        }
    }

    return response;
}

} // namespace keen_pbr3

#endif // WITH_API
