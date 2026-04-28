#include "routing_state.hpp"

#include "../routing/target.hpp"

#include <arpa/inet.h>
#include <cstring>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <net/if.h>

namespace keen_pbr3 {

namespace {

const Outbound* find_outbound(const std::vector<Outbound>& outbounds,
                              const std::string& tag) {
    for (const auto& ob : outbounds) {
        if (ob.tag == tag) {
            return &ob;
        }
    }
    return nullptr;
}

std::string resolve_urltest_selection(
    const std::map<std::string, std::string>* selections,
    const std::string& urltest_tag) {
    if (!selections) return {};
    auto it = selections->find(urltest_tag);
    if (it == selections->end()) return {};
    return it->second;
}

bool strict_enforcement_enabled(const Config& cfg, const Outbound& ob) {
    if (ob.strict_enforcement.has_value()) {
        return *ob.strict_enforcement;
    }
    return cfg.daemon.value_or(DaemonConfig{}).strict_enforcement.value_or(false);
}

bool parse_ip(const std::string& ip, int family, void* out) {
    return inet_pton(family, ip.c_str(), out) == 1;
}

int detect_ip_family(const std::string& ip) {
    in_addr addr4{};
    if (inet_pton(AF_INET, ip.c_str(), &addr4) == 1) {
        return AF_INET;
    }

    in6_addr addr6{};
    if (inet_pton(AF_INET6, ip.c_str(), &addr6) == 1) {
        return AF_INET6;
    }

    throw ConfigError("Invalid IP address: " + ip);
}

bool is_interface_up(const std::string& iface) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return false;
    }

    struct ifreq ifr {};
    std::strncpy(ifr.ifr_name, iface.c_str(), IFNAMSIZ - 1);
    ifr.ifr_name[IFNAMSIZ - 1] = '\0';

    const int rc = ioctl(fd, SIOCGIFFLAGS, &ifr);
    close(fd);
    if (rc < 0) {
        return false;
    }

    return (ifr.ifr_flags & IFF_UP) != 0;
}

bool ipv4_prefix_contains(const in_addr& network, const in_addr& candidate, int prefix_len) {
    if (prefix_len <= 0) return true;
    const uint32_t network_bits = ntohl(network.s_addr);
    const uint32_t candidate_bits = ntohl(candidate.s_addr);
    const uint32_t mask = (prefix_len >= 32) ? 0xFFFFFFFFu : (~0u << (32 - prefix_len));
    return (network_bits & mask) == (candidate_bits & mask);
}

bool ipv6_prefix_contains(const in6_addr& network, const in6_addr& candidate, int prefix_len) {
    if (prefix_len <= 0) return true;
    const int full_bytes = prefix_len / 8;
    const int extra_bits = prefix_len % 8;

    if (full_bytes > 0 &&
        std::memcmp(network.s6_addr, candidate.s6_addr, static_cast<size_t>(full_bytes)) != 0) {
        return false;
    }
    if (extra_bits == 0) return true;

    const uint8_t mask = static_cast<uint8_t>(0xFFu << (8 - extra_bits));
    return (network.s6_addr[full_bytes] & mask) == (candidate.s6_addr[full_bytes] & mask);
}

bool route_contains_ip(const DumpedRoute& route, const std::string& ip) {
    if (route.destination == "default") {
        return true;
    }

    const auto slash = route.destination.find('/');
    if (slash == std::string::npos) {
        return route.destination == ip;
    }

    const std::string network = route.destination.substr(0, slash);
    const int prefix_len = std::stoi(route.destination.substr(slash + 1));
    const int family = (ip.find(':') != std::string::npos) ? AF_INET6 : AF_INET;
    const int network_family = (network.find(':') != std::string::npos) ? AF_INET6 : AF_INET;
    if (family != network_family) {
        return false;
    }

    if (family == AF_INET) {
        in_addr network_addr{};
        in_addr ip_addr{};
        return parse_ip(network, AF_INET, &network_addr) &&
               parse_ip(ip, AF_INET, &ip_addr) &&
               ipv4_prefix_contains(network_addr, ip_addr, prefix_len);
    }

    in6_addr network_addr{};
    in6_addr ip_addr{};
    return parse_ip(network, AF_INET6, &network_addr) &&
           parse_ip(ip, AF_INET6, &ip_addr) &&
           ipv6_prefix_contains(network_addr, ip_addr, prefix_len);
}

std::vector<RouteSpec> make_default_routes(uint32_t table_id, const Outbound& ob) {
    std::vector<RouteSpec> routes;

    auto build_route = [&](int family, const std::optional<std::string>& gateway) {
        RouteSpec route;
        route.destination = "default";
        route.table = table_id;
        route.family = family;
        route.interface = ob.interface.value_or("");
        route.gateway = gateway;
        routes.push_back(std::move(route));
    };

    if (ob.gateway.has_value()) {
        build_route(detect_ip_family(*ob.gateway), ob.gateway);
        return routes;
    }

    // Link-scope interface outbounds can carry both families, so install
    // both defaults to keep marked IPv6 traffic from bypassing the policy table.
    build_route(AF_INET, std::nullopt);
    build_route(AF_INET6, std::nullopt);
    return routes;
}

std::vector<RouteSpec> make_family_closure_routes(uint32_t table_id, const Outbound& ob,
                                                  uint32_t metric = 1000) {
    std::vector<RouteSpec> routes;
    if (!ob.gateway.has_value()) {
        return routes;
    }

    const int gateway_family = detect_ip_family(*ob.gateway);
    const int missing_family = gateway_family == AF_INET ? AF_INET6 : AF_INET;

    RouteSpec route;
    route.destination = "default";
    route.table = table_id;
    route.unreachable = true;
    route.metric = metric;
    route.family = missing_family;
    routes.push_back(std::move(route));
    return routes;
}

std::vector<RouteSpec> make_unreachable_routes(uint32_t table_id, uint32_t metric = 1000) {
    std::vector<RouteSpec> routes;
    for (int family : {AF_INET, AF_INET6}) {
        RouteSpec route;
        route.destination = "default";
        route.table = table_id;
        route.unreachable = true;
        route.metric = metric;
        route.family = family;
        routes.push_back(std::move(route));
    }
    return routes;
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
    for (size_t i = 0; i < urltest.outbound_groups->size(); ++i) {
        groups.push_back({i, urltest.outbound_groups->at(i).weight.value_or(1)});
    }

    std::stable_sort(groups.begin(), groups.end(),
                     [](const GroupRef& a, const GroupRef& b) {
                         return a.weight < b.weight;
                     });

    for (const auto& group_ref : groups) {
        const auto& group = urltest.outbound_groups->at(group_ref.index);
        for (const auto& child_tag : group.outbounds) {
            const Outbound* child = find_outbound(outbounds, child_tag);
            if (child) {
                ordered.push_back(child);
            }
        }
    }
    return ordered;
}

// Returns the (offset)th non-reserved table ID starting from table_start.
static uint32_t safe_table_id(uint32_t table_start, uint32_t offset) {
    uint32_t id = table_start;
    uint32_t count = 0;
    while (true) {
        if (!is_reserved_table(id)) {
            if (count == offset) return id;
            ++count;
        }
        ++id;
    }
}

} // anonymous namespace

void populate_routing_state(const Config& cfg,
                            const OutboundMarkMap& marks,
                            RouteTable& routes,
                            PolicyRuleManager& rules,
                            OutboundReachabilityFn reachability_check,
                            const std::map<std::string, std::string>* urltest_selections) {
    const auto& outbounds = cfg.outbounds.value_or(std::vector<Outbound>{});
    const uint32_t table_start = static_cast<uint32_t>(
        cfg.iproute.value_or(IprouteConfig{}).table_start.value_or(150));
    const uint32_t fwmark_mask = fwmark_mask_value(cfg.fwmark.value_or(FwmarkConfig{}));

    uint32_t table_offset = 0;
    for (const auto& ob : outbounds) {
        if (ob.type == OutboundType::INTERFACE) {
            auto mark_it = marks.find(ob.tag);
            if (mark_it == marks.end()) continue;

            uint32_t table_id = safe_table_id(table_start, table_offset);
            ++table_offset;

            const bool strict = strict_enforcement_enabled(cfg, ob);
            const bool reachable = !reachability_check || reachability_check(ob);
            if (reachable) {
                for (const auto& route : make_default_routes(table_id, ob)) {
                    routes.add(route);
                }
                for (const auto& route : make_family_closure_routes(table_id, ob)) {
                    routes.add(route);
                }
            }
            if (strict) {
                for (const auto& route : make_unreachable_routes(table_id)) {
                    routes.add(route);
                }
            }

            RuleSpec ip_rule;
            ip_rule.fwmark = mark_it->second;
            ip_rule.fwmask = fwmark_mask;
            ip_rule.table = table_id;
            ip_rule.priority = table_id;
            rules.add(ip_rule);
        } else if (ob.type == OutboundType::TABLE) {
            auto mark_it = marks.find(ob.tag);
            if (mark_it == marks.end()) continue;

            RuleSpec ip_rule;
            ip_rule.fwmark = mark_it->second;
            ip_rule.fwmask = fwmark_mask;
            ip_rule.table = static_cast<uint32_t>(ob.table.value_or(0));
            ip_rule.priority = safe_table_id(table_start, table_offset);
            ++table_offset;
            rules.add(ip_rule);
        } else if (ob.type == OutboundType::URLTEST) {
            auto mark_it = marks.find(ob.tag);
            if (mark_it == marks.end()) continue;

            uint32_t table_id = safe_table_id(table_start, table_offset);
            ++table_offset;

            const bool strict = strict_enforcement_enabled(cfg, ob);
            const auto ordered_children = ordered_urltest_children(outbounds, ob);

            const std::string selected_tag = resolve_urltest_selection(urltest_selections, ob.tag);
            const bool selection_ready = !selected_tag.empty();
            if (selection_ready) {
                const Outbound* selected = find_outbound(outbounds, selected_tag);
                if (selected &&
                    selected->type == OutboundType::INTERFACE &&
                    (!reachability_check || reachability_check(*selected))) {
                    for (const auto& route : make_default_routes(table_id, *selected)) {
                        routes.add(route);
                    }
                    for (const auto& route : make_family_closure_routes(table_id, *selected)) {
                        routes.add(route);
                    }
                }

                uint32_t metric = 1;
                for (const Outbound* child : ordered_children) {
                    if (child->type != OutboundType::INTERFACE) {
                        continue;
                    }
                    if (reachability_check && !reachability_check(*child)) {
                        continue;
                    }
                    for (auto route : make_default_routes(table_id, *child)) {
                        route.metric = metric;
                        routes.add(route);
                    }
                    for (auto route : make_family_closure_routes(table_id, *child)) {
                        route.metric = metric;
                        routes.add(route);
                    }
                    ++metric;
                }
            }

            if (strict) {
                for (const auto& route : make_unreachable_routes(table_id)) {
                    routes.add(route);
                }
            }

            RuleSpec ip_rule;
            ip_rule.fwmark = mark_it->second;
            ip_rule.fwmask = fwmark_mask;
            ip_rule.table = table_id;
            ip_rule.priority = table_id;
            rules.add(ip_rule);
        }
        // BLACKHOLE: no routing table, no ip rule
        // IGNORE: no routing needed
    }
}

bool is_interface_outbound_reachable(const Outbound& outbound, NetlinkManager& netlink) {
    if (outbound.type != OutboundType::INTERFACE) {
        return true;
    }

    const auto iface = outbound.interface.value_or("");
    if (iface.empty() || if_nametoindex(iface.c_str()) == 0) {
        return false;
    }
    if (!is_interface_up(iface)) {
        return false;
    }

    auto routes = netlink.dump_routes_in_table(254);

    if (outbound.gateway.has_value()) {
        for (const auto& route : routes) {
            if (route.blackhole || route.unreachable) continue;
            if (!route.interface || *route.interface != iface) continue;
            if (route_contains_ip(route, *outbound.gateway)) {
                return true;
            }
        }
        return false;
    }

    return true;
}

FirewallGlobalPrefilter build_firewall_global_prefilter(const Config& cfg) {
    FirewallGlobalPrefilter prefilter;
    prefilter.skip_established_or_dnat = true;
    prefilter.skip_marked_packets = cfg.daemon.value_or(DaemonConfig{}).skip_marked_packets.value_or(true);

    const auto route_cfg = cfg.route.value_or(RouteConfig{});
    if (route_cfg.inbound_interfaces.has_value()
        && !route_cfg.inbound_interfaces->empty()) {
        prefilter.inbound_interfaces = *route_cfg.inbound_interfaces;
    }

    return prefilter;
}

std::vector<RuleState> build_fw_rule_states(
    const Config& cfg,
    const OutboundMarkMap& marks,
    const std::map<std::string, std::string>* urltest_selections) {
    std::vector<RuleState> rule_states;

    const auto& all_outbounds = cfg.outbounds.value_or(std::vector<Outbound>{});
    static const std::map<std::string, ListConfig> empty_lists;
    const auto& lists_map = cfg.lists ? *cfg.lists : empty_lists;
    const auto& route_rules =
        cfg.route.value_or(RouteConfig{}).rules.value_or(std::vector<RouteRule>{});

    for (size_t rule_idx = 0; rule_idx < route_rules.size(); ++rule_idx) {
        const auto& rule = route_rules[rule_idx];

        if (!route_rule_enabled(rule)) {
            RuleState rs;
            rs.rule_index = rule_idx;
            rs.list_names = route_rule_lists(rule);
            rs.outbound_tag = rule.outbound;
            rs.action_type = RuleActionType::Skip;
            rule_states.push_back(std::move(rs));
            continue;
        }

        auto decision = resolve_route_action(rule.outbound, all_outbounds);

        if (decision.is_skip) {
            RuleState rs;
            rs.rule_index = rule_idx;
            rs.list_names = route_rule_lists(rule);
            rs.outbound_tag = rule.outbound;
            rs.action_type = RuleActionType::Skip;
            rule_states.push_back(std::move(rs));
            continue;
        }

        if (!decision.outbound.has_value() || !*decision.outbound) {
            RuleState rs;
            rs.rule_index = rule_idx;
            rs.list_names = route_rule_lists(rule);
            rs.outbound_tag = rule.outbound;
            rs.action_type = RuleActionType::Skip;
            rule_states.push_back(std::move(rs));
            continue;
        }

        const Outbound* ob = *decision.outbound;

        if (decision.is_passthrough) {
            RuleState rs;
            rs.rule_index = rule_idx;
            rs.list_names = route_rule_lists(rule);
            rs.outbound_tag = rule.outbound;
            rs.action_type = RuleActionType::Pass;

            for (const auto& list_name : route_rule_lists(rule)) {
                auto list_cfg_it = lists_map.find(list_name);
                if (list_cfg_it == lists_map.end()) continue;

                const std::string set4  = "kpbr4_"  + list_name;
                const std::string set6  = "kpbr6_"  + list_name;
                const std::string set4d = "kpbr4d_" + list_name;
                const std::string set6d = "kpbr6d_" + list_name;

                rs.set_names.push_back(set4);
                rs.set_names.push_back(set6);
                rs.set_names.push_back(set4d);
                rs.set_names.push_back(set6d);
            }

            rule_states.push_back(std::move(rs));
            continue;
        }

        std::string effective_tag = ob->tag;
        const Outbound* effective_ob = ob;

        if (ob->type == OutboundType::URLTEST) {
            auto selected = resolve_urltest_selection(urltest_selections, effective_tag);
            if (!selected.empty()) {
                const Outbound* child = find_outbound(all_outbounds, selected);
                if (child) {
                    effective_ob = child;
                    effective_tag = selected;
                }
            }
        }

        const bool is_blackhole = (effective_ob->type == OutboundType::BLACKHOLE);

        RuleState rs;
        rs.rule_index = rule_idx;
        rs.list_names = route_rule_lists(rule);
        rs.outbound_tag = rule.outbound;

        if (is_blackhole) {
            rs.action_type = RuleActionType::Drop;
        } else {
            rs.action_type = RuleActionType::Mark;
            auto mark_it = marks.find(effective_tag);
            if (mark_it != marks.end()) {
                rs.fwmark = mark_it->second;
            }
        }

        for (const auto& list_name : route_rule_lists(rule)) {
            auto list_cfg_it = lists_map.find(list_name);
            if (list_cfg_it == lists_map.end()) continue;

            const std::string set4  = "kpbr4_"  + list_name;
            const std::string set6  = "kpbr6_"  + list_name;
            const std::string set4d = "kpbr4d_" + list_name;
            const std::string set6d = "kpbr6d_" + list_name;

            rs.set_names.push_back(set4);
            rs.set_names.push_back(set6);
            rs.set_names.push_back(set4d);
            rs.set_names.push_back(set6d);
        }

        rule_states.push_back(std::move(rs));
    }

    return rule_states;
}

void prune_fw_rule_states_to_realized_sets(
    const Config& cfg,
    std::vector<RuleState>& rule_states,
    const ListSetUsageFn& list_usage_fn) {
    static const std::map<std::string, ListConfig> empty_lists;
    const auto& lists_map = cfg.lists ? *cfg.lists : empty_lists;
    const auto& route_rules =
        cfg.route.value_or(RouteConfig{}).rules.value_or(std::vector<RouteRule>{});

    std::map<std::string, ListSetUsage> usage_cache;

    for (auto& rs : rule_states) {
        if (rs.action_type == RuleActionType::Skip) {
            continue;
        }
        if (rs.rule_index >= route_rules.size()) {
            continue;
        }

        rs.set_names.clear();

        const auto& rule = route_rules[rs.rule_index];
        for (const auto& list_name : route_rule_lists(rule)) {
            auto list_cfg_it = lists_map.find(list_name);
            if (list_cfg_it == lists_map.end()) continue;

            auto usage_it = usage_cache.find(list_name);
            if (usage_it == usage_cache.end()) {
                usage_it = usage_cache.emplace(
                    list_name,
                    list_usage_fn(list_name, list_cfg_it->second)).first;
            }
            const auto& usage = usage_it->second;

            if (usage.has_static_entries) {
                rs.set_names.push_back("kpbr4_" + list_name);
                rs.set_names.push_back("kpbr6_" + list_name);
            }
            if (usage.has_domain_entries) {
                rs.set_names.push_back("kpbr4d_" + list_name);
                rs.set_names.push_back("kpbr6d_" + list_name);
            }
        }
    }
}

} // namespace keen_pbr3
