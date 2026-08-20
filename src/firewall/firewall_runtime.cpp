#include "firewall_runtime.hpp"

#include "../config/routing_state.hpp"
#include "../dns/dns_router.hpp"
#include "../lists/list_entry_visitor.hpp"
#include "../lists/list_set_usage.hpp"
#include "../lists/list_streamer.hpp"
#include "../log/logger.hpp"
#include "../util/ipv6_support.hpp"

#include <arpa/inet.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace keen_pbr3 {

namespace {

std::optional<uint32_t> canonical_ipset_hashsize(
    const std::optional<int64_t> &value) {
    const int64_t effective = value.value_or(1024);
    if (effective < 1 || effective > std::numeric_limits<uint32_t>::max()) {
        return std::nullopt;
    }
    return normalize_ipset_hashsize(static_cast<uint32_t>(effective));
}

bool ipset_hashsize_changed(const std::optional<int64_t> &current,
                            const std::optional<int64_t> &candidate) {
    const auto current_canonical = canonical_ipset_hashsize(current);
    const auto candidate_canonical = canonical_ipset_hashsize(candidate);
    if (current_canonical.has_value() && candidate_canonical.has_value()) {
        return current_canonical != candidate_canonical;
    }
    return current != candidate;
}

bool ipset_maxelem_changed(const std::optional<int64_t> &current,
                           const std::optional<int64_t> &candidate) {
    return current.value_or(65536) != candidate.value_or(65536);
}

} // namespace

FirewallConfigApplyPolicy firewall_config_apply_policy(
    FirewallBackend backend, const Config &current, const Config &candidate) {
  if (backend != FirewallBackend::iptables) {
    return {};
  }

  const auto current_daemon = current.daemon.value_or(DaemonConfig{});
  const auto candidate_daemon = candidate.daemon.value_or(DaemonConfig{});
  if (!ipset_hashsize_changed(current_daemon.ipset_hashsize,
                              candidate_daemon.ipset_hashsize) &&
      !ipset_maxelem_changed(current_daemon.ipset_maxelem,
                             candidate_daemon.ipset_maxelem)) {
    return {};
  }

  return {FirewallApplyMode::Destructive, true};
}

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

bool contains_set_name(const RuleState& state, const std::string& name) {
    return std::find(state.set_names.begin(), state.set_names.end(), name) !=
           state.set_names.end();
}

ListSetUsage reused_list_set_usage(
    const std::vector<RuleState>* previous_rule_states,
    size_t rule_index,
    const std::string& list_name,
    const ListConfig& list_config,
    Firewall& firewall,
    bool ipv6_enabled) {
    if (previous_rule_states == nullptr ||
        rule_index >= previous_rule_states->size()) {
        throw FirewallRulesOnlyError(
            "no realized firewall state is available for route rule " +
            std::to_string(rule_index) + " list " + list_name);
    }

    const RuleState& previous = previous_rule_states->at(rule_index);
    if (previous.rule_index != rule_index ||
        std::find(previous.list_names.begin(), previous.list_names.end(),
                  list_name) == previous.list_names.end()) {
        throw FirewallRulesOnlyError(
            "realized firewall state does not contain route rule " +
            std::to_string(rule_index) + " list " + list_name);
    }

    ListSetUsage usage;
    const auto static_usage_for_family = [&](int family) {
        const std::string expected = firewall.static_set_name(list_name, family);
        const auto candidates = firewall.static_set_names(list_name, family);
        const bool has_expected = contains_set_name(previous, expected);
        const bool has_alternate = std::any_of(
            candidates.begin(), candidates.end(), [&](const std::string& name) {
                return name != expected && contains_set_name(previous, name);
            });
        if (has_alternate) {
            throw FirewallRulesOnlyError(
                std::string("realized firewall state references a stale static ipset for ") +
                "route rule " + std::to_string(rule_index) + " list " +
                list_name);
        }
        return has_expected;
    };

    const std::string set4d = firewall.dynamic_set_name(list_name, AF_INET);
    usage.has_static_entries = static_usage_for_family(AF_INET);
    usage.has_domain_entries = contains_set_name(previous, set4d);
    if (ipv6_enabled) {
        const bool has_static_v6 = static_usage_for_family(AF_INET6);
        usage.has_static_entries = usage.has_static_entries || has_static_v6;
        usage.has_domain_entries = usage.has_domain_entries ||
                                   contains_set_name(
                                       previous,
                                       firewall.dynamic_set_name(list_name,
                                                                 AF_INET6));
    }

    const int64_t ttl_ms = list_config.ttl_ms.value_or(0);
    if (ttl_ms >= 1000) {
        usage.dynamic_timeout = static_cast<uint32_t>(ttl_ms / 1000);
    }
    return usage;
}

} // namespace

std::vector<RuleState> apply_runtime_firewall(
    const Config& config,
    const OutboundMarkMap& outbound_marks,
    const std::map<std::string, std::string>& urltest_selections,
    const CacheManager& cache_manager,
    Firewall& firewall,
    FirewallApplyMode mode,
    const std::vector<RuleState>* previous_rule_states,
    bool force_clear_dynamic_sets) {
  try {
    std::unique_ptr<ListStreamer> list_streamer;
    if (mode != FirewallApplyMode::RulesOnly) {
        list_streamer = std::make_unique<ListStreamer>(cache_manager);
    }
    auto rule_states = build_fw_rule_states(config, outbound_marks, &urltest_selections);
    const RouteConfig route_config = config.route.value_or(RouteConfig{});
    const Ipv6SupportDecision ipv6_decision = resolve_ipv6_support(config);
    log_ipv6_support_decision_once(ipv6_decision);
    firewall.set_ipv6_enabled(ipv6_decision.enabled);
    firewall.set_clear_dynamic_sets_on_apply(
        config.daemon.value_or(DaemonConfig{}).clear_dynamic_sets_on_apply.value_or(true));
    if (force_clear_dynamic_sets) {
      firewall.set_clear_dynamic_sets_on_apply(true);
    }
    const auto daemon_config = config.daemon.value_or(DaemonConfig{});
    firewall.set_ipset_hashsize(
        daemon_config.ipset_hashsize.has_value()
            ? std::optional<uint32_t>{static_cast<uint32_t>(
                  *daemon_config.ipset_hashsize)}
            : std::nullopt);
    firewall.set_ipset_maxelem(
        daemon_config.ipset_maxelem.has_value()
            ? std::optional<uint32_t>{static_cast<uint32_t>(
                  *daemon_config.ipset_maxelem)}
            : std::nullopt);
    firewall.prepare_apply(mode);
    auto prefilter = build_firewall_global_prefilter(config);
    prefilter.restore_conntrack_mark = true;
    prefilter.conntrack_mark_mask = fwmark_mask_value(config.fwmark.value_or(FwmarkConfig{}));
    firewall.set_global_prefilter(std::move(prefilter));
    firewall.set_fwmark_mask(fwmark_mask_value(config.fwmark.value_or(FwmarkConfig{})));

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
        FirewallRuleCriteria criteria = build_firewall_rule_criteria(rule);
        rule_state.criteria = criteria;

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
                        mode == FirewallApplyMode::RulesOnly
                            ? reused_list_set_usage(previous_rule_states, rule_idx,
                                                    list_name, list_cfg, firewall,
                                                    ipv6_decision.enabled)
                            : analyze_list_set_usage(list_name, list_cfg,
                                                     *list_streamer)).first;
                }
                const auto& usage = usage_it->second;

                const std::string set4 = firewall.static_set_name(list_name, AF_INET);
                const std::string set6 = firewall.static_set_name(list_name, AF_INET6);
                const std::string set4d = firewall.dynamic_set_name(list_name, AF_INET);
                const std::string set6d = firewall.dynamic_set_name(list_name, AF_INET6);

                if (usage.has_static_entries) {
                    firewall.create_ipset(set4, AF_INET, 0);
                    rule_state.set_names.push_back(set4);
                    if (ipv6_decision.enabled) {
                        firewall.create_ipset(set6, AF_INET6, 0);
                        rule_state.set_names.push_back(set6);
                    }

                    if (mode != FirewallApplyMode::RulesOnly) {
                        auto loader4 = firewall.create_batch_loader(set4);
                        auto loader6 = ipv6_decision.enabled
                            ? firewall.create_batch_loader(set6)
                            : nullptr;
                        FunctionalVisitor splitter([&](EntryType type, std::string_view entry) {
                            if (type == EntryType::Domain) {
                                return;
                            }
                            const bool is_ipv6 = entry.find(':') != std::string_view::npos;
                            if (is_ipv6) {
                                if (loader6) {
                                    loader6->on_entry(type, entry);
                                }
                            } else {
                                loader4->on_entry(type, entry);
                            }
                        });
                        list_streamer->stream_list(list_name, list_cfg, splitter);
                        loader4->finish();
                        if (loader6) {
                            loader6->finish();
                        }
                    }
                }

                if (usage.has_domain_entries) {
                    firewall.create_ipset(set4d, AF_INET, usage.dynamic_timeout);
                    rule_state.set_names.push_back(set4d);
                    if (ipv6_decision.enabled) {
                        firewall.create_ipset(set6d, AF_INET6, usage.dynamic_timeout);
                        rule_state.set_names.push_back(set6d);
                    }
                }

                if (usage.has_static_entries) {
                    apply_rule(set4);
                    if (ipv6_decision.enabled) {
                        apply_rule(set6);
                    }
                    emitted_rule = true;
                }
                if (usage.has_domain_entries) {
                    apply_rule(set4d);
                    if (ipv6_decision.enabled) {
                        apply_rule(set6d);
                    }
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
    if (detour_outbound->type == OutboundType::URLTEST || detour_outbound->type == OutboundType::ICMPTEST) {
                // A URLTEST route is switched behind its stable table/mark.
                // DNS detours must not pin existing flows to the transient
                // selected child mark.
            } else {
                // The internal detour mark has an unconditional terminal guard
                // even when the user-facing outbound is configured non-strict.
                effective_tag = internal_detour_mark_key(detour_outbound->tag);
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
                criteria.apply_output = true;
                firewall.create_mark_rule(mark_it->second, criteria);
            }
        }
    }

    firewall.apply(mode);
    return rule_states;
  } catch (const FirewallRulesOnlyError& error) {
    if (mode != FirewallApplyMode::RulesOnly) {
        throw;
    }
    Logger::instance().warn(
        "RulesOnly firewall preflight failed; falling back to PreserveSets: {}",
        error.what());
    return apply_runtime_firewall(config, outbound_marks, urltest_selections,
                                  cache_manager, firewall,
                                  FirewallApplyMode::PreserveSets,
                                  previous_rule_states,
                                  /*force_clear_dynamic_sets=*/false);
  }
}

} // namespace keen_pbr3
