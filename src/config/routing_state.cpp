#include "routing_state.hpp"

#include "../routing/target.hpp"

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

} // anonymous namespace

void populate_routing_state(const Config& cfg,
                            const OutboundMarkMap& marks,
                            RouteTable& routes,
                            PolicyRuleManager& rules) {
    const uint32_t table_start = static_cast<uint32_t>(
        cfg.iproute.value_or(IprouteConfig{}).table_start.value_or(100));
    const uint32_t fwmark_mask = static_cast<uint32_t>(
        cfg.fwmark.value_or(FwmarkConfig{}).mask.value_or(0x00FF0000));

    uint32_t table_offset = 0;
    for (const auto& ob : cfg.outbounds.value_or(std::vector<Outbound>{})) {
        if (ob.type == OutboundType::INTERFACE) {
            auto mark_it = marks.find(ob.tag);
            if (mark_it == marks.end()) continue;

            uint32_t table_id = table_start + table_offset;
            ++table_offset;

            RouteSpec route;
            route.destination = "default";
            route.table = table_id;
            route.interface = ob.interface.value_or("");
            if (ob.gateway) route.gateway = *ob.gateway;
            routes.add(route);

            RouteSpec blackhole_route;
            blackhole_route.destination = "default";
            blackhole_route.table = table_id;
            blackhole_route.blackhole = true;
            blackhole_route.metric = 500;
            routes.add(blackhole_route);

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
            ip_rule.priority = table_start + table_offset;
            ++table_offset;
            rules.add(ip_rule);
        } else if (ob.type == OutboundType::URLTEST) {
            auto mark_it = marks.find(ob.tag);
            if (mark_it == marks.end()) continue;

            uint32_t table_id = table_start + table_offset;
            ++table_offset;

            RouteSpec blackhole_route;
            blackhole_route.destination = "default";
            blackhole_route.table = table_id;
            blackhole_route.blackhole = true;
            blackhole_route.metric = 500;
            routes.add(blackhole_route);

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

        auto decision = resolve_route_action(rule.outbound, all_outbounds);

        if (decision.is_skip) {
            RuleState rs;
            rs.rule_index = rule_idx;
            rs.list_names = rule.list;
            rs.outbound_tag = rule.outbound;
            rs.action_type = RuleActionType::Skip;
            rule_states.push_back(std::move(rs));
            continue;
        }

        if (!decision.outbound.has_value() || !*decision.outbound) {
            RuleState rs;
            rs.rule_index = rule_idx;
            rs.list_names = rule.list;
            rs.outbound_tag = rule.outbound;
            rs.action_type = RuleActionType::Skip;
            rule_states.push_back(std::move(rs));
            continue;
        }

        const Outbound* ob = *decision.outbound;

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
        const bool is_ignore    = (effective_ob->type == OutboundType::IGNORE);

        if (is_ignore) {
            RuleState rs;
            rs.rule_index = rule_idx;
            rs.list_names = rule.list;
            rs.outbound_tag = rule.outbound;
            rs.action_type = RuleActionType::Skip;
            rule_states.push_back(std::move(rs));
            continue;
        }

        RuleState rs;
        rs.rule_index = rule_idx;
        rs.list_names = rule.list;
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

        for (const auto& list_name : rule.list) {
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

} // namespace keen_pbr3
