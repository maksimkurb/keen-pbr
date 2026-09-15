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

namespace {

constexpr std::size_t kNoFirewallRuleSource =
    std::numeric_limits<std::size_t>::max();

std::string logical_set_name(const std::string& list_name, FirewallFamily family,
                             bool dynamic) {
    const std::string prefix = family == FirewallFamily::ipv6 ? "kpbr6" : "kpbr4";
    return prefix + (dynamic ? "d_" : "_") + list_name;
}

FirewallFamily family_for_criteria(const FirewallRuleCriteria& criteria) {
    if (criteria.default_gateway == DefaultGatewayFamily::Ipv6) {
        return FirewallFamily::ipv6;
    }
    if (criteria.default_gateway == DefaultGatewayFamily::Ipv4) {
        return FirewallFamily::ipv4;
    }
    bool has_ipv4 = false;
    bool has_ipv6 = false;
    const auto inspect_addresses = [&](const std::vector<std::string>& addresses) {
        for (const auto& address : addresses) {
            if (address.find(':') == std::string::npos) {
                has_ipv4 = true;
            } else {
                has_ipv6 = true;
            }
        }
    };
    inspect_addresses(criteria.src_addr);
    inspect_addresses(criteria.dst_addr);
    if (has_ipv6 && !has_ipv4) {
        return FirewallFamily::ipv6;
    }
    if (has_ipv4 && !has_ipv6) {
        return FirewallFamily::ipv4;
    }
    return FirewallFamily::any;
}

FirewallFamily family_for_set(const std::string& set_name,
                              const FirewallRuleCriteria& criteria) {
    if (set_name.rfind("kpbr6", 0) == 0) {
        return FirewallFamily::ipv6;
    }
    if (set_name.rfind("kpbr4", 0) == 0) {
        return FirewallFamily::ipv4;
    }
    return family_for_criteria(criteria);
}

} // namespace

FirewallPlan build_firewall_plan(const FirewallPlanBuildInputs& inputs) {
  FirewallPlan plan;
  plan.fwmark_mask = inputs.fwmark_mask;
  plan.global_prefilter = build_firewall_global_prefilter(inputs.config);
  plan.global_prefilter.restore_conntrack_mark = true;
  plan.global_prefilter.conntrack_mark_mask = inputs.fwmark_mask;

  const auto& all_outbounds =
      inputs.config.outbounds.value_or(std::vector<Outbound>{});
  static const std::map<std::string, ListConfig> empty_lists;
  const auto& lists_map = inputs.config.lists ? *inputs.config.lists : empty_lists;
  const auto route_config = inputs.config.route.value_or(RouteConfig{});
  const auto& route_rules =
      route_config.rules.value_or(std::vector<RouteRule>{});
  const auto rule_states =
      build_fw_rule_states(inputs.config, inputs.outbound_marks);
  FirewallRuleRegistrar registrar(plan);

  const auto add_rule = [&](std::size_t rule_index, FirewallRuleStage stage,
                            int priority,
                            const std::string& module,
                            const std::string& instance,
                            FirewallRuleCriteria criteria,
                            FirewallRuleAction action,
                            const std::optional<std::string>& set_name) {
    criteria.dst_set_name = set_name;
    FirewallRuleInstance rule;
    rule.key = FirewallRuleKey::compact(module, instance);
    rule.stage = stage;
    rule.priority = priority;
    rule.hook = criteria.apply_output ? FirewallHook::output
                                      : FirewallHook::prerouting;
    rule.family = set_name.has_value()
                      ? family_for_set(*set_name, criteria)
                      : family_for_criteria(criteria);
    rule.criteria = std::move(criteria);
    rule.action = std::move(action);
    rule.source_rule_index = rule_index;
    registrar.register_rule(std::move(rule));
  };

  for (std::size_t rule_idx = 0; rule_idx < route_rules.size(); ++rule_idx) {
    const auto& route_rule = route_rules[rule_idx];
    const auto& rule_state = rule_states[rule_idx];
    if (rule_state.action_type == RuleActionType::Skip) {
      continue;
    }

    FirewallRuleCriteria criteria = build_firewall_rule_criteria(
        route_rule, inputs.main_routes, inputs.interfaces);
    const auto outbound = find_outbound_by_tag(all_outbounds, route_rule.outbound);
    const auto module_for_action = [&]() -> std::string {
      if (rule_state.action_type == RuleActionType::Drop) return "route.drop";
      if (rule_state.action_type == RuleActionType::Pass) return "route.pass";
      if (outbound != nullptr && outbound_uses_balance(*outbound)) {
        return "route.balance";
      }
      return "route.mark";
    };
    const std::string module = module_for_action();
    const auto balance_candidates_for_rule = [&]()
        -> const std::vector<FirewallBalanceCandidate>& {
      static const std::vector<FirewallBalanceCandidate> empty_candidates;
      if (outbound == nullptr || !outbound_uses_balance(*outbound) ||
          inputs.balance_candidates == nullptr) {
        return empty_candidates;
      }
      const auto it = inputs.balance_candidates->find(outbound->tag);
      return it == inputs.balance_candidates->end() ? empty_candidates
                                                     : it->second;
    };
    const auto action_for_rule = [&]() -> FirewallRuleAction {
      if (rule_state.action_type == RuleActionType::Drop) {
        return VerdictAction::drop;
      }
      if (rule_state.action_type == RuleActionType::Pass) {
        return VerdictAction::pass;
      }
      if (outbound != nullptr && outbound_uses_balance(*outbound)) {
        const auto& candidates = balance_candidates_for_rule();
        BalanceAction action;
        action.fallback_mark = rule_state.fwmark;
        action.candidates = candidates;
        return action;
      }
      return MarkAction{rule_state.fwmark, inputs.fwmark_mask};
    };

    const auto add_for_set = [&](const std::optional<std::string>& set_name,
                                 std::size_t occurrence) {
      if (rule_state.action_type == RuleActionType::Mark &&
          rule_state.fwmark == 0) {
        return;
      }
      std::string instance = "rule=" + std::to_string(rule_idx) +
                             ";occurrence=" + std::to_string(occurrence) +
                             ";set=" + (set_name.has_value() ? *set_name : "none");
      const auto action = action_for_rule();
      add_rule(rule_idx, FirewallRuleStage::route_classification,
               static_cast<int>(rule_idx), module, instance,
               criteria, action, set_name);
    };

    const auto& list_names = route_rule_lists(route_rule);
    if (!list_names.empty()) {
      bool emitted_rule = false;
      std::size_t list_occurrence = 0;
      for (const auto& list_name : list_names) {
        const auto list_config = lists_map.find(list_name);
        const auto usage = inputs.list_usage.find(list_name);
        if (list_config == lists_map.end() || usage == inputs.list_usage.end()) {
          ++list_occurrence;
          continue;
        }
        const auto append_set = [&](FirewallFamily family, bool dynamic) {
          const std::string name = logical_set_name(list_name, family, dynamic);
          registrar.register_set({name, family,
                                  dynamic ? usage->second.dynamic_timeout : 0});
          return name;
        };
        if (usage->second.has_static_entries) {
          const auto set4 = append_set(FirewallFamily::ipv4, false);
          const auto set6 = inputs.ipv6_enabled
                                ? std::optional<std::string>{
                                      append_set(FirewallFamily::ipv6, false)}
                                : std::nullopt;
          add_for_set(set4, list_occurrence * 4U);
          if (set6.has_value()) {
            add_for_set(set6, list_occurrence * 4U + 1U);
          }
          emitted_rule = true;
        }
        if (usage->second.has_domain_entries) {
          const auto set4 = append_set(FirewallFamily::ipv4, true);
          const auto set6 = inputs.ipv6_enabled
                                ? std::optional<std::string>{
                                      append_set(FirewallFamily::ipv6, true)}
                                : std::nullopt;
          add_for_set(set4, list_occurrence * 4U + 2U);
          if (set6.has_value()) {
            add_for_set(set6, list_occurrence * 4U + 3U);
          }
          emitted_rule = true;
        }
        ++list_occurrence;
      }
      if (!emitted_rule && criteria.has_rule_selector()) {
        add_for_set(std::nullopt, list_names.size() * 4U);
      }
    } else if (criteria.has_rule_selector()) {
      add_for_set(std::nullopt, 0);
    }
  }

  if (inputs.config.dns.has_value()) {
    const auto& dns_servers =
        inputs.config.dns->servers.value_or(std::vector<DnsServer>{});
    const DnsServerRegistry dns_registry(inputs.config.dns.value_or(DnsConfig{}));
    const int dns_priority_start = static_cast<int>(route_rules.size());
    for (std::size_t server_idx = 0; server_idx < dns_servers.size(); ++server_idx) {
      const auto& server = dns_servers[server_idx];
      if (!server.detour.has_value()) {
        continue;
      }
      const Outbound* detour_outbound =
          find_outbound_by_tag(all_outbounds, server.detour.value());
      if (!detour_outbound) {
        continue;
      }
      std::string effective_tag = detour_outbound->tag;
      if (detour_outbound->type != OutboundType::URLTEST &&
          detour_outbound->type != OutboundType::ICMPTEST) {
        effective_tag = internal_detour_mark_key(detour_outbound->tag);
      }
      const auto mark_it = inputs.outbound_marks.find(effective_tag);
      if (mark_it == inputs.outbound_marks.end()) {
        continue;
      }
      const auto resolved_servers = dns_registry.get_servers(server.tag);
      if (resolved_servers.empty()) {
        throw FirewallError("DNS server tag not found during detour setup: " +
                            server.tag);
      }
      for (std::size_t endpoint_idx = 0; endpoint_idx < resolved_servers.size();
           ++endpoint_idx) {
        const DnsServerConfig* resolved_server = resolved_servers[endpoint_idx];
        FirewallRuleCriteria criteria;
        criteria.proto = L4Proto::TcpUdp;
        criteria.dst_port = std::to_string(resolved_server->port);
        criteria.dst_addr = {resolved_server->resolved_ip};
        criteria.apply_output = true;
        const std::string instance =
            "server=" + std::to_string(server_idx) +
            ";endpoint=" + std::to_string(endpoint_idx) +
            ";address=" + resolved_server->resolved_ip +
            ";port=" + std::to_string(resolved_server->port);
        // DNS detours are replayed after every route rule, so they share the
        // route stage and use priorities after the route config range.
        add_rule(kNoFirewallRuleSource, FirewallRuleStage::route_classification,
                 dns_priority_start +
                                             static_cast<int>(server_idx),
                 "dns.detour", instance, std::move(criteria),
                 MarkAction{mark_it->second, inputs.fwmark_mask}, std::nullopt);
      }
    }
  }

  registrar.finish();
  return plan;
}

std::vector<RuleState> apply_runtime_firewall(
    const Config& config,
    const OutboundMarkMap& outbound_marks,
    const CacheManager& cache_manager,
    Firewall& firewall,
    FirewallApplyMode mode,
    const std::vector<RuleState>* previous_rule_states,
    bool force_clear_dynamic_sets,
    const std::vector<DumpedRoute>& main_routes,
    const std::vector<DumpedInterface>& interfaces,
    const FirewallBalanceCandidates* balance_candidates) {
  try {
    std::unique_ptr<ListStreamer> list_streamer;
    if (mode != FirewallApplyMode::RulesOnly) {
      list_streamer = std::make_unique<ListStreamer>(cache_manager);
    }
    auto rule_states = build_fw_rule_states(config, outbound_marks);
    const RouteConfig route_config = config.route.value_or(RouteConfig{});
    const Ipv6SupportDecision ipv6_decision = resolve_ipv6_support(config);
    log_ipv6_support_decision_once(ipv6_decision);
    const auto daemon_config = config.daemon.value_or(DaemonConfig{});
    const auto& all_outbounds = config.outbounds.value_or(std::vector<Outbound>{});
    static const std::map<std::string, ListConfig> empty_lists;
    const auto& lists_map = config.lists ? *config.lists : empty_lists;
    const auto& route_rules = route_config.rules.value_or(std::vector<RouteRule>{});
    const uint32_t fwmark_mask =
        fwmark_mask_value(config.fwmark.value_or(FwmarkConfig{}));
    const bool needs_nftables = std::any_of(
        route_rules.begin(), route_rules.end(), [](const RouteRule& rule) {
          return rule.default_gateway.has_value();
        }) || std::any_of(all_outbounds.begin(), all_outbounds.end(),
                          [](const Outbound& outbound) {
                            return (outbound.type == OutboundType::URLTEST ||
                                    outbound.type == OutboundType::ICMPTEST) &&
                                   outbound_uses_balance(outbound);
                          });
    if (needs_nftables && firewall.backend() != FirewallBackend::nftables) {
      throw FirewallError(
          "default_gateway and test-group balance require the nftables firewall backend");
    }

    std::map<std::string, ListSetUsage> list_usage_cache;
    const bool has_route_lists = std::any_of(
        route_rules.begin(), route_rules.end(), [](const RouteRule& rule) {
          return !route_rule_lists(rule).empty();
        });
    const bool defer_rules_only_lists =
        mode == FirewallApplyMode::RulesOnly && has_route_lists;
    if (!defer_rules_only_lists) {
      for (std::size_t rule_idx = 0; rule_idx < route_rules.size(); ++rule_idx) {
        for (const auto& list_name : route_rule_lists(route_rules[rule_idx])) {
          const auto list_cfg_it = lists_map.find(list_name);
          if (list_cfg_it == lists_map.end() ||
              list_usage_cache.find(list_name) != list_usage_cache.end()) {
            continue;
          }
          list_usage_cache.emplace(
              list_name,
              analyze_list_set_usage(list_name, list_cfg_it->second,
                                     *list_streamer));
        }
      }
    }
    FirewallPlanBuildInputs plan_inputs{
        config, outbound_marks, list_usage_cache, main_routes, interfaces,
        balance_candidates, ipv6_decision.enabled, fwmark_mask};
    FirewallPlan plan = build_firewall_plan(plan_inputs);

    firewall.set_ipv6_enabled(ipv6_decision.enabled);
    firewall.set_clear_dynamic_sets_on_apply(
        config.daemon.value_or(DaemonConfig{}).clear_dynamic_sets_on_apply.value_or(true));
    if (force_clear_dynamic_sets) {
      firewall.set_clear_dynamic_sets_on_apply(true);
    }
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
    if (defer_rules_only_lists) {
      list_usage_cache.clear();
      for (std::size_t rule_idx = 0; rule_idx < route_rules.size(); ++rule_idx) {
        for (const auto& list_name : route_rule_lists(route_rules[rule_idx])) {
          const auto list_cfg_it = lists_map.find(list_name);
          if (list_cfg_it == lists_map.end() ||
              list_usage_cache.find(list_name) != list_usage_cache.end()) {
            continue;
          }
          list_usage_cache.emplace(
              list_name,
              reused_list_set_usage(previous_rule_states, rule_idx,
                                    list_name, list_cfg_it->second, firewall,
                                    ipv6_decision.enabled));
        }
      }
      plan = build_firewall_plan(plan_inputs);
    }
    configure_firewall_plan(plan, firewall);
    std::vector<uint32_t> owned_marks;
    owned_marks.reserve(outbound_marks.size());
    for (const auto& [tag, mark] : outbound_marks) {
        (void)tag;
        owned_marks.push_back(mark);
    }
    firewall.set_owned_marks(owned_marks);

    for (size_t rule_idx = 0; rule_idx < route_rules.size(); ++rule_idx) {
        const auto& rule = route_rules[rule_idx];
        RuleState& rule_state = rule_states[rule_idx];

        if (rule_state.action_type == RuleActionType::Skip) {
            continue;
        }

        rule_state.set_names.clear();

        FirewallRuleCriteria criteria = build_firewall_rule_criteria(
            rule, main_routes, interfaces);
        rule_state.criteria = criteria;

        const auto& list_names = route_rule_lists(rule);
        if (!list_names.empty()) {
            for (const auto& list_name : list_names) {
                auto list_cfg_it = lists_map.find(list_name);
                if (list_cfg_it == lists_map.end()) {
                    continue;
                }

                const auto& list_cfg = list_cfg_it->second;
                const auto usage_it = list_usage_cache.find(list_name);
                if (usage_it == list_usage_cache.end()) {
                    continue;
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

            }
        }

        // ponytail: O(routes * rules) preserves set/rule interleaving; index
        // by source when route/rule counts make this measurable.
        for (const auto& planned_rule : plan.rules) {
            if (planned_rule.source_rule_index == rule_idx) {
                replay_firewall_rule(planned_rule, firewall);
            }
        }
    }

    for (const auto& planned_rule : plan.rules) {
        if (planned_rule.source_rule_index == kNoFirewallRuleSource) {
            replay_firewall_rule(planned_rule, firewall);
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
    return apply_runtime_firewall(config, outbound_marks, cache_manager, firewall,
                                  FirewallApplyMode::PreserveSets,
                                  previous_rule_states,
                                  /*force_clear_dynamic_sets=*/false,
                                  main_routes,
                                  interfaces,
                                  balance_candidates);
  }
}

} // namespace keen_pbr3
