#include "firewall_runtime.hpp"
#include "firewall_physical.hpp"
#include "firewall_rule_modules.hpp"

#include "../config/list_parser.hpp"
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

void IpFamilySplitVisitor::on_entry(EntryType type, std::string_view entry) {
  if (type == EntryType::Domain) {
    return;
  }
  const auto family = ListParser::entry_family(type, entry);
  if (!family.has_value()) {
    constexpr std::size_t kMaxDetailedInvalidEntries = 5;
    const std::size_t count = ++invalid_entries_;
    if (count <= kMaxDetailedInvalidEntries) {
      Logger::instance().warn(
          "Skipping list entry '{}' in list {}: not a valid IPv4/IPv6 address or CIDR",
          std::string(entry.substr(0, 128)), list_name_);
    } else if (count == kMaxDetailedInvalidEntries + 1) {
      Logger::instance().warn(
          "Too many unclassifiable entries in list {}; further entries will be skipped without warnings",
          list_name_);
    }
    return;
  }
  ListEntryVisitor* loader =
      *family == EntryFamily::Ipv6 ? ipv6_ : ipv4_;
  if (loader != nullptr) {
    loader->on_entry(type, entry);
  }
}

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

ListSetUsage reused_list_set_usage(
    const FirewallPlan* previous_active_plan,
    const std::string& list_name,
    const ListConfig& list_config,
    bool ipv6_enabled) {
    if (previous_active_plan == nullptr) {
        throw FirewallRulesOnlyError(
            "no active firewall plan is available for list " + list_name);
    }

    if (previous_active_plan->referenced_list_names.find(list_name) ==
        previous_active_plan->referenced_list_names.end()) {
        throw FirewallRulesOnlyError(
            "active firewall plan does not contain list " + list_name);
    }

    ListSetUsage usage;
    const auto find_set = [&](const std::string& name) {
        return std::find_if(
            previous_active_plan->sets.begin(), previous_active_plan->sets.end(),
            [&](const FirewallSetDeclaration& declaration) {
                return declaration.name == name;
            });
    };
    const auto static4 = find_set("kpbr4_" + list_name);
    const auto dynamic4 = find_set("kpbr4d_" + list_name);
    usage.has_static_entries = static4 != previous_active_plan->sets.end();
    usage.has_domain_entries = dynamic4 != previous_active_plan->sets.end();
    std::optional<uint32_t> dynamic_timeout;
    if (dynamic4 != previous_active_plan->sets.end()) {
        dynamic_timeout = dynamic4->timeout;
    }

    if (ipv6_enabled) {
        const auto static6 = find_set("kpbr6_" + list_name);
        const auto dynamic6 = find_set("kpbr6d_" + list_name);
        usage.has_static_entries =
            usage.has_static_entries || static6 != previous_active_plan->sets.end();
        usage.has_domain_entries =
            usage.has_domain_entries || dynamic6 != previous_active_plan->sets.end();
        if (dynamic6 != previous_active_plan->sets.end()) {
            if (dynamic_timeout.has_value() &&
                *dynamic_timeout != dynamic6->timeout) {
                throw FirewallRulesOnlyError(
                    "active firewall plan has inconsistent dynamic set timeouts for list " +
                    list_name);
            }
            dynamic_timeout = dynamic6->timeout;
        }
    }

    if (dynamic_timeout.has_value()) {
        const int64_t ttl_ms = list_config.ttl_ms.value_or(0);
        const uint32_t configured_timeout = ttl_ms >= 1000
                                                ? static_cast<uint32_t>(ttl_ms / 1000)
                                                : 0;
        if (*dynamic_timeout != configured_timeout) {
            throw FirewallRulesOnlyError(
                "list " + list_name + " dynamic timeout changed since the active firewall plan");
        }
        usage.dynamic_timeout = *dynamic_timeout;
    }
    return usage;
}

std::vector<RuleState> project_rule_states(
    const Config& config, const FirewallPlan& plan, const Firewall& firewall) {
    const auto route_rules =
        config.route.value_or(RouteConfig{}).rules.value_or(std::vector<RouteRule>{});
    std::vector<RuleState> rule_states(route_rules.size());
    std::vector<bool> projected(route_rules.size(), false);

    for (std::size_t rule_index = 0; rule_index < route_rules.size(); ++rule_index) {
        auto& state = rule_states[rule_index];
        state.rule_index = rule_index;
        state.list_names = route_rule_lists(route_rules[rule_index]);
        state.outbound_tag = route_rules[rule_index].outbound;
        state.action_type = RuleActionType::Skip;
    }

    for (const auto& rule : plan.rules) {
        if (rule.source_rule_index >= rule_states.size()) {
            continue;
        }

        auto& state = rule_states[rule.source_rule_index];
        if (!projected[rule.source_rule_index]) {
            if (const auto* mark = std::get_if<MarkAction>(&rule.action)) {
                state.action_type = RuleActionType::Mark;
                state.fwmark = mark->value;
            } else if (const auto* balance =
                           std::get_if<BalanceAction>(&rule.action)) {
                state.action_type = RuleActionType::Mark;
                state.fwmark = balance->fallback_mark;
            } else if (const auto* verdict =
                           std::get_if<VerdictAction>(&rule.action)) {
                state.action_type = *verdict == VerdictAction::drop
                                        ? RuleActionType::Drop
                                        : RuleActionType::Pass;
            } else {
                continue;
            }
            state.criteria = rule.criteria;
            state.criteria.dst_set_name.reset();
            const auto append_plan_set = [&](const std::string& logical_name) {
                const auto declaration = std::find_if(
                    plan.sets.begin(), plan.sets.end(), [&](const auto& candidate) {
                        return candidate.name == logical_name;
                    });
                if (declaration != plan.sets.end()) {
                    state.set_names.push_back(
                        firewall.physical_set_name(declaration->name));
                }
            };
            for (const auto& list_name : state.list_names) {
                append_plan_set("kpbr4_" + list_name);
                append_plan_set("kpbr6_" + list_name);
                append_plan_set("kpbr4d_" + list_name);
                append_plan_set("kpbr6d_" + list_name);
            }
            projected[rule.source_rule_index] = true;
        }
    }

    return rule_states;
}

} // namespace

FirewallPlan build_firewall_plan(const FirewallPlanBuildInputs& inputs) {
  FirewallPlan plan;
  plan.fwmark_mask = inputs.fwmark_mask;
  for (const auto& [list_name, usage] : inputs.list_usage) {
    (void)usage;
    plan.referenced_list_names.insert(list_name);
  }

  const auto& all_outbounds =
      inputs.config.outbounds.value_or(std::vector<Outbound>{});
  static const std::map<std::string, ListConfig> empty_lists;
  const auto& lists_map = inputs.config.lists ? *inputs.config.lists : empty_lists;
  const auto route_config = inputs.config.route.value_or(RouteConfig{});
  const auto& route_rules =
      route_config.rules.value_or(std::vector<RouteRule>{});
  FirewallRuleRegistrar registrar(plan);
  const FirewallBuildContext context{
      route_rules, all_outbounds, lists_map, inputs.list_usage,
      inputs.main_routes, inputs.interfaces, inputs.backend,
      inputs.ipv6_enabled, inputs.fwmark_mask, inputs.balance_candidates,
      &inputs.config, &inputs.outbound_marks, inputs.intercept};
  for (const auto register_module : route_rule_module_manifest()) {
    register_module(context, registrar);
  }

  registrar.finish();
  return plan;
}

ActiveFirewall apply_runtime_firewall(
    const Config& config,
    const OutboundMarkMap& outbound_marks,
    const CacheManager& cache_manager,
    Firewall& firewall,
    FirewallApplyMode mode,
    const ActiveFirewall* previous_active,
    bool force_clear_dynamic_sets,
    const std::vector<DumpedRoute>& main_routes,
    const std::vector<DumpedInterface>& interfaces,
    const FirewallBalanceCandidates* balance_candidates,
    const std::optional<InterceptFirewallSettings>& intercept) {
  // Success boundary: the returned ActiveFirewall exists only after
  // firewall.apply() returned. A failure while preparing (planning, streaming,
  // RulesOnly preflight) happens before any kernel mutation, so the caller's
  // previous active object stays accurate. A failure inside apply() may have
  // touched the kernel, but nothing new is published either; the exception
  // propagates and the health verifier reports the resulting drift. There is
  // deliberately no rollback here.
  const FirewallPlan* previous_active_plan =
      previous_active != nullptr ? &previous_active->plan : nullptr;
  try {
    std::unique_ptr<ListStreamer> list_streamer;
    if (mode != FirewallApplyMode::RulesOnly) {
      list_streamer = std::make_unique<ListStreamer>(cache_manager);
    }
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
    for (const auto& route_rule : route_rules) {
      for (const auto& list_name : route_rule_lists(route_rule)) {
        const auto list_cfg_it = lists_map.find(list_name);
        if (list_cfg_it == lists_map.end() ||
            list_usage_cache.find(list_name) != list_usage_cache.end()) {
          continue;
        }
        list_usage_cache.emplace(
            list_name,
            mode == FirewallApplyMode::RulesOnly
                ? reused_list_set_usage(previous_active_plan, list_name,
                                        list_cfg_it->second,
                                        ipv6_decision.enabled)
                : analyze_list_set_usage(list_name, list_cfg_it->second,
                                         *list_streamer));
      }
    }
    FirewallPlanBuildInputs plan_inputs{
        config, outbound_marks, list_usage_cache, main_routes, interfaces,
        balance_candidates, ipv6_decision.enabled, fwmark_mask,
        firewall.backend(), intercept};
    FirewallPlan plan = build_firewall_plan(plan_inputs);
    validate_firewall_plan_backend(plan, firewall.backend());

    firewall.set_ipv6_enabled(ipv6_decision.enabled);
    firewall.set_clear_dynamic_sets_on_apply(
        config.daemon.value_or(DaemonConfig{}).clear_dynamic_sets_on_apply.value_or(false));
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
    // RulesOnly reuses the sets of the previous apply under their stable
    // names, so every set the new plan needs must have been realized by it
    // (a newly added list has no set yet).  The backend verifies that the
    // sets still exist in the kernel.
    if (mode == FirewallApplyMode::RulesOnly && previous_active != nullptr) {
      for (const auto& declaration : plan.sets) {
        if (!previous_active->result.has_physical_set(
                firewall.physical_set_name(declaration.name))) {
          throw FirewallRulesOnlyError(
              "active firewall plan does not contain set " + declaration.name);
        }
      }
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

    // Stream each planned static list once. Route rules only project the
    // finalized plan into RuleState below; they must not duplicate resource
    // loading when several rules reference the same list.
    if (mode != FirewallApplyMode::RulesOnly) {
      for (const auto& list_usage : list_usage_cache) {
        const auto& list_name = list_usage.first;
        const auto list_cfg_it = lists_map.find(list_name);
        if (list_cfg_it == lists_map.end()) {
          continue;
        }

        const auto set4 = planned_physical_set_name("kpbr4_" + list_name);
        const auto set6 = planned_physical_set_name("kpbr6_" + list_name);
        if (!set4.has_value() && !set6.has_value()) {
          continue;
        }

        auto loader4 = set4.has_value()
            ? firewall.create_batch_loader(*set4)
            : nullptr;
        auto loader6 = set6.has_value()
            ? firewall.create_batch_loader(*set6)
            : nullptr;
        IpFamilySplitVisitor splitter(loader4.get(), loader6.get(), list_name);
        list_streamer->stream_list(list_name, list_cfg_it->second, splitter);
        if (loader4) {
          loader4->finish();
        }
        if (loader6) {
          loader6->finish();
        }
      }
    }

    auto rule_states = project_rule_states(config, plan, firewall);

    firewall.apply(plan, mode);

    FirewallApplyResult result;
    result.mode = mode;
    result.physical_set_names.reserve(plan.sets.size());
    for (const auto& declaration : plan.sets) {
      result.physical_set_names.push_back(
          firewall.physical_set_name(declaration.name));
    }
    result.expected_ruleset = std::make_shared<const PhysicalRuleset>(
        firewall.expected_ruleset(plan));
    std::sort(result.physical_set_names.begin(), result.physical_set_names.end());
    result.physical_set_names.erase(
        std::unique(result.physical_set_names.begin(),
                    result.physical_set_names.end()),
        result.physical_set_names.end());
    return ActiveFirewall{std::move(plan), std::move(result),
                          std::move(rule_states)};
  } catch (const FirewallRulesOnlyError& error) {
    if (mode != FirewallApplyMode::RulesOnly) {
        throw;
    }
    Logger::instance().warn(
        "RulesOnly firewall preflight failed; falling back to PreserveSets: {}",
        error.what());
    return apply_runtime_firewall(config, outbound_marks, cache_manager, firewall,
                                  FirewallApplyMode::PreserveSets,
                                  previous_active,
                                  /*force_clear_dynamic_sets=*/false,
                                  main_routes,
                                  interfaces,
                                  balance_candidates,
                                  intercept);
  }
}

} // namespace keen_pbr3
