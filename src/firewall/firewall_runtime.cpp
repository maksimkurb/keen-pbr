#include "firewall_runtime.hpp"

#include "../config/addr_spec.hpp"
#include "../config/routing_state.hpp"
#include "../dns/dns_router.hpp"
#include "../lists/list_entry_visitor.hpp"
#include "../lists/list_set_usage.hpp"
#include "../lists/list_streamer.hpp"

#include <arpa/inet.h>

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace keen_pbr3 {

namespace {

const Outbound* find_outbound_by_tag(const std::vector<Outbound>& outbounds,
                                     const std::string& tag) {
    for (const auto& outbound : outbounds) {
        if (outbound.tag == tag) {
            return &outbound;
        }
    }
    return nullptr;
}

L4Proto parse_rule_proto(const std::optional<std::string>& proto) {
    if (!proto.has_value() || proto->empty()) return L4Proto::Any;
    if (*proto == "tcp") return L4Proto::Tcp;
    if (*proto == "udp") return L4Proto::Udp;
    if (*proto == "tcp/udp") return L4Proto::TcpUdp;
    throw FirewallError("Unsupported route rule protocol: " + *proto);
}

} // namespace

std::vector<RuleState> apply_runtime_firewall(
    const Config& config,
    const OutboundMarkMap& outbound_marks,
    const std::map<std::string, std::string>& urltest_selections,
    const CacheManager& cache_manager,
    Firewall& firewall,
    FirewallApplyMode mode) {
    ListStreamer list_streamer(cache_manager);
    auto rule_states = build_fw_rule_states(config, outbound_marks, &urltest_selections);
    const RouteConfig route_config = config.route.value_or(RouteConfig{});
    firewall.set_global_prefilter(build_firewall_global_prefilter(config));

    const auto& all_outbounds = config.outbounds.value_or(std::vector<Outbound>{});
    static const std::map<std::string, ListConfig> empty_lists;
    const auto& lists_map = config.lists ? *config.lists : empty_lists;
    const auto& route_rules = route_config.rules.value_or(std::vector<RouteRule>{});
    std::map<std::string, ListSetUsage> list_usage_cache;

    for (size_t rule_idx = 0; rule_idx < route_rules.size(); ++rule_idx) {
        const auto& rule = route_rules[rule_idx];
        RuleState& rule_state = rule_states[rule_idx];

        if (rule_state.action_type == RuleActionType::Skip) {
            continue;
        }

        rule_state.set_names.clear();

        const bool is_blackhole = rule_state.action_type == RuleActionType::Drop;
        const bool is_pass = rule_state.action_type == RuleActionType::Pass;
        auto strip_neg = [](const std::string& value) -> std::pair<std::string, bool> {
            if (!value.empty() && value.front() == '!') {
                return {value.substr(1), true};
            }
            return {value, false};
        };

        FirewallRuleCriteria criteria;
        criteria.proto = parse_rule_proto(rule.proto);

        {
            auto [port, negated] = strip_neg(rule.src_port.value_or(""));
            criteria.src_port = std::move(port);
            criteria.negate_src_port = negated;
        }
        {
            auto [port, negated] = strip_neg(rule.dest_port.value_or(""));
            criteria.dst_port = std::move(port);
            criteria.negate_dst_port = negated;
        }
        {
            AddrSpec spec = parse_addr_spec(rule.src_addr.value_or(""));
            criteria.negate_src_addr = spec.negate;
            criteria.src_addr = std::move(spec.addrs);
        }
        {
            AddrSpec spec = parse_addr_spec(rule.dest_addr.value_or(""));
            criteria.negate_dst_addr = spec.negate;
            criteria.dst_addr = std::move(spec.addrs);
        }

        auto apply_rule = [&](const std::optional<std::string>& dst_set_name) {
            FirewallRuleCriteria rule_criteria = criteria;
            rule_criteria.dst_set_name = dst_set_name;

            if (is_blackhole) {
                firewall.create_drop_rule(rule_criteria);
            } else if (is_pass) {
                firewall.create_pass_rule(rule_criteria);
            } else if (rule_state.fwmark != 0) {
                firewall.create_mark_rule(rule_state.fwmark, rule_criteria);
            }
        };

        const auto& list_names = route_rule_lists(rule);
        if (!list_names.empty()) {
            bool emitted_rule = false;

            for (const auto& list_name : list_names) {
                auto list_cfg_it = lists_map.find(list_name);
                if (list_cfg_it == lists_map.end()) {
                    continue;
                }

                const auto& list_cfg = list_cfg_it->second;
                auto usage_it = list_usage_cache.find(list_name);
                if (usage_it == list_usage_cache.end()) {
                    usage_it = list_usage_cache.emplace(
                        list_name,
                        analyze_list_set_usage(list_name, list_cfg, list_streamer)).first;
                }
                const auto& usage = usage_it->second;

                const std::string set4 = "kpbr4_" + list_name;
                const std::string set6 = "kpbr6_" + list_name;
                const std::string set4d = "kpbr4d_" + list_name;
                const std::string set6d = "kpbr6d_" + list_name;

                if (usage.has_static_entries) {
                    firewall.create_ipset(set4, AF_INET, 0);
                    firewall.create_ipset(set6, AF_INET6, 0);
                    rule_state.set_names.push_back(set4);
                    rule_state.set_names.push_back(set6);

                    auto loader4 = firewall.create_batch_loader(set4);
                    auto loader6 = firewall.create_batch_loader(set6);
                    FunctionalVisitor splitter([&](EntryType type, std::string_view entry) {
                        if (type == EntryType::Domain) {
                            return;
                        }
                        const bool is_ipv6 = entry.find(':') != std::string_view::npos;
                        if (is_ipv6) {
                            loader6->on_entry(type, entry);
                        } else {
                            loader4->on_entry(type, entry);
                        }
                    });
                    list_streamer.stream_list(list_name, list_cfg, splitter);
                    loader4->finish();
                    loader6->finish();
                }

                if (usage.has_domain_entries) {
                    firewall.create_ipset(set4d, AF_INET, usage.dynamic_timeout);
                    firewall.create_ipset(set6d, AF_INET6, usage.dynamic_timeout);
                    rule_state.set_names.push_back(set4d);
                    rule_state.set_names.push_back(set6d);
                }

                if (usage.has_static_entries) {
                    apply_rule(set4);
                    apply_rule(set6);
                    emitted_rule = true;
                }
                if (usage.has_domain_entries) {
                    apply_rule(set4d);
                    apply_rule(set6d);
                    emitted_rule = true;
                }
            }

            if (!emitted_rule && criteria.has_rule_selector()) {
                apply_rule(std::nullopt);
            }
        } else if (criteria.has_rule_selector()) {
            apply_rule(std::nullopt);
        }
    }

    if (config.dns.has_value()) {
        const auto& dns_servers = config.dns->servers.value_or(std::vector<DnsServer>{});
        const DnsServerRegistry dns_registry(config.dns.value_or(DnsConfig{}));
        for (const auto& server : dns_servers) {
            if (!server.detour.has_value()) {
                continue;
            }

            const Outbound* detour_outbound =
                find_outbound_by_tag(all_outbounds, server.detour.value());
            if (!detour_outbound) {
                continue;
            }

            std::string effective_tag = detour_outbound->tag;
            if (detour_outbound->type == OutboundType::URLTEST) {
                auto selection_it = urltest_selections.find(effective_tag);
                if (selection_it != urltest_selections.end() && !selection_it->second.empty()) {
                    const Outbound* child =
                        find_outbound_by_tag(all_outbounds, selection_it->second);
                    if (child) {
                        effective_tag = child->tag;
                    }
                }
            }

            auto mark_it = outbound_marks.find(effective_tag);
            if (mark_it == outbound_marks.end()) {
                continue;
            }

            const auto resolved_servers = dns_registry.get_servers(server.tag);
            if (resolved_servers.empty()) {
                throw FirewallError("DNS server tag not found during detour setup: " + server.tag);
            }

            for (const DnsServerConfig* resolved_server : resolved_servers) {
                FirewallRuleCriteria criteria;
                criteria.proto = L4Proto::TcpUdp;
                criteria.dst_port = std::to_string(resolved_server->port);
                criteria.dst_addr = {resolved_server->resolved_ip};
                firewall.create_mark_rule(mark_it->second, criteria);
            }
        }
    }

    firewall.apply(mode);
    return rule_states;
}

} // namespace keen_pbr3
