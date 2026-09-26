#include "firewall_runtime.hpp"
#include "firewall_rule_modules.hpp"

#include "../config/routing_state.hpp"
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

FirewallPlan build_firewall_plan(const FirewallPlanBuildInputs& inputs) {
  FirewallPlan plan;
  plan.fwmark_mask = inputs.fwmark_mask;
  const auto prefilter = build_firewall_prefilter(inputs.config);

  const auto& all_outbounds =
      inputs.config.outbounds.value_or(std::vector<Outbound>{});
  static const std::map<std::string, ListConfig> empty_lists;
  const auto& lists_map = inputs.config.lists ? *inputs.config.lists : empty_lists;
  const auto route_config = inputs.config.route.value_or(RouteConfig{});
  const auto& route_rules =
      route_config.rules.value_or(std::vector<RouteRule>{});
  FirewallRuleRegistrar registrar(plan);
  const bool owned_marks_present = std::any_of(
      inputs.outbound_marks.begin(), inputs.outbound_marks.end(),
      [](const auto& entry) { return entry.second != 0; });
  const bool restore_conntrack_mark =
      inputs.backend == FirewallBackend::iptables || owned_marks_present;
  const FirewallBuildContext context{
      route_rules, all_outbounds, lists_map, inputs.list_usage,
      inputs.main_routes, inputs.interfaces, inputs.backend,
      inputs.ipv6_enabled, inputs.fwmark_mask, inputs.balance_candidates,
      restore_conntrack_mark, prefilter.skip_established_or_dnat,
      prefilter.skip_marked_packets,
      prefilter.inbound_interfaces.value_or(std::vector<std::string>{}),
      &inputs.config, &inputs.outbound_marks};
  for (const auto register_module : route_rule_module_manifest()) {
    register_module(context, registrar);
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
    const FirewallBalanceCandidates* balance_candidates,
    FirewallPlan* applied_plan) {
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
    static const std::map<std::string, ListConfig> empty_lists;
    const auto& lists_map = config.lists ? *config.lists : empty_lists;
    const auto& route_rules = route_config.rules.value_or(std::vector<RouteRule>{});
    const uint32_t fwmark_mask =
        fwmark_mask_value(config.fwmark.value_or(FwmarkConfig{}));

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
    if (defer_rules_only_lists) {
      // RulesOnly defers list inspection until after backend preflight. Mark
      // referenced lists as potentially populated so validation sees every
      // possible canonical action; the plan is rebuilt from realized sets
      // below before applying the completed plan.
      for (const auto& route_rule : route_rules) {
        for (const auto& list_name : route_rule_lists(route_rule)) {
          if (lists_map.find(list_name) != lists_map.end()) {
            list_usage_cache.emplace(list_name, ListSetUsage{true, false, 0});
          }
        }
      }
    }
    FirewallPlanBuildInputs plan_inputs{
        config, outbound_marks, list_usage_cache, main_routes, interfaces,
        balance_candidates, ipv6_decision.enabled, fwmark_mask,
        firewall.backend()};
    FirewallPlan plan = build_firewall_plan(plan_inputs);
    validate_firewall_plan_backend(plan, firewall.backend());

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

    // The finalized plan owns set existence and schemas. Stage its physical
    // declarations before list streaming; apply(plan) repeats these calls as
    // an idempotent direct-apply safeguard.
    for (const auto& declaration : plan.sets) {
      const int family = declaration.family == FirewallFamily::ipv6
                             ? AF_INET6
                             : AF_INET;
      const std::string physical_name =
          firewall.physical_set_name(declaration.name);
      firewall.create_ipset(physical_name, family, declaration.timeout);
    }

    const auto planned_physical_set_name =
        [&](const std::string& logical_name) -> std::optional<std::string> {
      const auto declaration = std::find_if(
          plan.sets.begin(), plan.sets.end(), [&](const auto& candidate) {
            return candidate.name == logical_name;
          });
      if (declaration == plan.sets.end()) {
        return std::nullopt;
      }
      return firewall.physical_set_name(declaration->name);
    };

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
                const auto set4 =
                    planned_physical_set_name("kpbr4_" + list_name);
                const auto set6 =
                    planned_physical_set_name("kpbr6_" + list_name);
                const auto set4d =
                    planned_physical_set_name("kpbr4d_" + list_name);
                const auto set6d =
                    planned_physical_set_name("kpbr6d_" + list_name);

                for (const auto& set_name : {set4, set6, set4d, set6d}) {
                    if (set_name.has_value()) {
                        rule_state.set_names.push_back(*set_name);
                    }
                }

                if ((set4.has_value() || set6.has_value()) &&
                    mode != FirewallApplyMode::RulesOnly) {
                    auto loader4 = set4.has_value()
                        ? firewall.create_batch_loader(*set4)
                        : nullptr;
                    auto loader6 = set6.has_value()
                        ? firewall.create_batch_loader(*set6)
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
                        } else if (loader4) {
                            loader4->on_entry(type, entry);
                        }
                    });
                    list_streamer->stream_list(list_name, list_cfg, splitter);
                    if (loader4) {
                        loader4->finish();
                    }
                    if (loader6) {
                        loader6->finish();
                    }
                }

            }
        }

    }

    firewall.apply(plan, mode);
    if (applied_plan != nullptr) {
      *applied_plan = std::move(plan);
    }
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
                                  balance_candidates,
                                  applied_plan);
  }
}

} // namespace keen_pbr3
