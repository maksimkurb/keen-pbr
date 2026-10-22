#include <doctest/doctest.h>

#include "../src/config/config.hpp"
#include "../src/config/routing_state.hpp"
#include "../src/health/host_health_warnings.hpp"
#include "../src/routing/routing_reconciler.hpp"
#include "../src/routing/urltest_manager.hpp"

#include <algorithm>
#include <netinet/in.h>
#include <string>
#include <tuple>
#include <vector>

namespace keen_pbr3 {
namespace {

DumpedRule dumped(const RuleSpec& rule, int family) {
    return {rule.priority, rule.fwmark, rule.fwmask,
            rule.action == RuleAction::lookup ? rule.table : 0U, family, rule.action};
}

auto key(const DumpedRule& r) {
    return std::make_tuple(r.priority, r.fwmark, r.fwmask, r.table, r.family, r.action);
}

// Applies rule mutations to an in-memory kernel rule list.
class KernelRules final : public RoutingNetlinkOperations {
public:
    RouteAddResult add_route(const RouteSpec&) override { return RouteAddResult::Created; }
    void delete_route(const RouteSpec&) override {}
    RuleAddResult add_rule_for_family(const RuleSpec& rule, int family) override {
        rules.push_back(dumped(rule, family));
        return RuleAddResult::Created;
    }
    void delete_rule_for_family(const RuleSpec& rule, int family) override {
        const auto target = dumped(rule, family);
        const auto it = std::find_if(rules.begin(), rules.end(), [&](const DumpedRule& r) {
            return key(r) == key(target);
        });
        REQUIRE(it != rules.end());
        rules.erase(it);
    }
    std::vector<DumpedRoute> dump_routes_in_table(uint32_t, int = 0) override { return {}; }
    std::vector<DumpedRoute> dump_routes(int = 0) override { return {}; }
    std::vector<DumpedRule> dump_policy_rules(int = 0) override { return rules; }

    std::vector<DumpedRule> rules;
};

Config reload_config(bool with_group, bool work_strict,
                     const std::string& fwmark_start = "0x00010000",
                     const std::string& fwmark_mask = "0x00FF0000") {
    // "auto" sorts before "warp" and "work", so adding it shifts their marks
    // by two while their tables (interface outbounds first) stay put.
    std::string outbounds =
        R"({"tag":"warp","type":"interface","interface":"wg_cf"},)"
        R"({"tag":"work","type":"interface","interface":"wg_work",)"
        R"("strict_enforcement":)" + std::string(work_strict ? "true" : "false") + "}";
    if (with_group) {
        outbounds += R"(,{"tag":"auto","type":"urltest","url":"http://example.com",)"
                     R"("outbound_groups":[{"weight":1,"outbounds":["warp"]}]})";
    }
    Config cfg = parse_config(R"({
        "fwmark":{"start":")" + fwmark_start + R"(","mask":")" + fwmark_mask + R"("},
        "iproute":{"table_start":401},
        "daemon":{"strict_enforcement":true},
        "dns":{"servers":[{"tag":"onecell","address":"10.3.5.18","detour":"work"}]},
        "outbounds":[)" + outbounds + R"(]
    })");
    validate_config(cfg);
    return cfg;
}

// Non-strict test group; `extra` adds a leading interface that shifts the marks
// of everything after it, and `warp_strict` makes the group child strict.
Config probe_config(bool extra, bool warp_strict = false) {
    std::string outbounds =
        R"({"tag":"warp","type":"interface","interface":"wg_cf","strict_enforcement":)" +
        std::string(warp_strict ? "true" : "false") + "},"
        R"({"tag":"wan","type":"interface","interface":"eth1","strict_enforcement":false},)"
        R"({"tag":"auto","type":"urltest","url":"http://example.com",)"
        R"("outbound_groups":[{"weight":1,"outbounds":["warp","wan"]}]})";
    if (extra) outbounds += R"(,{"tag":"aaa","type":"interface","interface":"wg_x"})";
    Config cfg = parse_config(R"({
        "fwmark":{"start":"0x00010000","mask":"0x00FF0000"},
        "iproute":{"table_start":401},
        "daemon":{"strict_enforcement":false},
        "outbounds":[)" + outbounds + R"(]
    })");
    validate_config(cfg);
    return cfg;
}

std::vector<RuleSpec> planned_rules(const Config& cfg) {
    const auto marks = allocate_outbound_marks(cfg);
    KernelRules unused;
    RouteTable routes(unused, true);
    PolicyRuleManager rules(unused, true);
    populate_routing_state(cfg, marks, routes, rules, [](const Outbound&) { return true; });
    return rules.get_rules();
}

std::vector<DumpedRule> expanded(const std::vector<RuleSpec>& rules) {
    std::vector<DumpedRule> out;
    for (const auto& rule : rules) {
        for (int family : {AF_INET, AF_INET6}) {
            if (rule.family == 0 || rule.family == family) {
                out.push_back(dumped(rule, family));
            }
        }
    }
    return out;
}

bool same_rule_set(std::vector<DumpedRule> a, std::vector<DumpedRule> b) {
    const auto less = [](const DumpedRule& l, const DumpedRule& r) {
        return key(l) < key(r);
    };
    std::sort(a.begin(), a.end(), less);
    std::sort(b.begin(), b.end(), less);
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(), [](const auto& l, const auto& r) {
               return key(l) == key(r);
           });
}

const RuleSpec* rule_at(const std::vector<RuleSpec>& rules, uint32_t priority) {
    const auto it = std::find_if(rules.begin(), rules.end(), [&](const RuleSpec& r) {
        return r.priority == priority;
    });
    return it == rules.end() ? nullptr : &*it;
}

// A hot reload knows the rules applied before and matches them exactly; the
// first apply after a restart only has the rule namespace, as in the daemon.
void check_reload(const Config& before, const Config& after, bool remember_previous) {
    const auto old_rules = planned_rules(before);
    const auto new_rules = planned_rules(after);
    KernelRules kernel;
    RoutingReconciler(kernel).reconcile({}, old_rules, {}, policy_rule_namespace(before));
    REQUIRE(same_rule_set(kernel.rules, expanded(old_rules)));

    if (remember_previous) {
        RoutingReconciler(kernel).reconcile({}, new_rules, old_rules);
    } else {
        RoutingReconciler(kernel).reconcile({}, new_rules, {}, policy_rule_namespace(after));
    }

    CHECK(same_rule_set(kernel.rules, expanded(new_rules)));
    const auto ns = policy_rule_namespace(after);
    CHECK(evaluate_fwmark_conflict_warnings({}, kernel.rules, new_rules, ns.fwmask, ns)
              .empty());
    CHECK(evaluate_stale_policy_rule_warnings(kernel.rules, new_rules, ns).empty());
}

} // namespace

TEST_CASE("policy rule reload: a shifted mark at the same priority replaces the old one") {
    const auto before = reload_config(false, false);
    const auto after = reload_config(true, false);
    const auto old_rules = planned_rules(before);
    const auto new_rules = planned_rules(after);
    // The work detour keeps its priority and table but changes its mark, and
    // its old mark is still allocated (to warp's unused detour) in `after`.
    const auto* old_detour = rule_at(old_rules, old_rules.back().priority - 1);
    REQUIRE(old_detour != nullptr);
    const auto* new_detour = rule_at(new_rules, old_detour->priority);
    REQUIRE(new_detour != nullptr);
    REQUIRE(new_detour->table == old_detour->table);
    REQUIRE(new_detour->fwmark != old_detour->fwmark);
    REQUIRE(std::none_of(new_rules.begin(), new_rules.end(), [&](const RuleSpec& r) {
        return r.fwmark == old_detour->fwmark;
    }));

    SUBCASE("hot reload") { check_reload(before, after, true); }
    SUBCASE("restart without the previous generation") { check_reload(before, after, false); }
}

TEST_CASE("policy rule reload: priorities that are no longer used are removed") {
    const auto before = reload_config(true, false);
    const auto after = reload_config(false, false);
    REQUIRE(planned_rules(after).size() < planned_rules(before).size());

    SUBCASE("hot reload") { check_reload(before, after, true); }
    SUBCASE("restart without the previous generation") { check_reload(before, after, false); }
}

TEST_CASE("policy rule reload: turning strict enforcement off removes the guard") {
    const auto before = reload_config(false, true);
    const auto after = reload_config(false, false);
    const auto old_rules = planned_rules(before);
    const auto new_rules = planned_rules(after);
    REQUIRE(std::count_if(new_rules.begin(), new_rules.end(), [](const RuleSpec& r) {
                return r.action == RuleAction::unreachable;
            }) < std::count_if(old_rules.begin(), old_rules.end(), [](const RuleSpec& r) {
                return r.action == RuleAction::unreachable;
            }));

    check_reload(before, after, true);
}

TEST_CASE("policy rule reload: a hot reload that moves fwmark bits removes the old rules") {
    const auto before = reload_config(true, false);
    const auto after = reload_config(true, false, "0x01000000", "0xFF000000");
    const auto ns = policy_rule_namespace(after);
    // The old rules are outside the new namespace; only tracking claims them.
    for (const auto& rule : expanded(planned_rules(before))) {
        REQUIRE_FALSE(rule_in_namespace(rule, ns));
    }

    check_reload(before, after, true);
}

TEST_CASE("policy rule health: a leftover keen-pbr rule is stale, not a foreign conflict") {
    const auto before = reload_config(false, false);
    const auto after = reload_config(true, false);
    const auto new_rules = planned_rules(after);
    const auto ns = policy_rule_namespace(after);
    const auto kernel = expanded(planned_rules(before));

    CHECK(evaluate_fwmark_conflict_warnings({}, kernel, new_rules, ns.fwmask, ns).empty());
    const auto stale = evaluate_stale_policy_rule_warnings(kernel, new_rules, ns);
    REQUIRE_FALSE(stale.empty());
    CHECK(stale.front().code == HealthWarningCode::stale_policy_rule);
    CHECK(stale.front().message.find("Outdated keen-pbr ip rule") != std::string::npos);
}

TEST_CASE("policy_rule_namespace covers the configured allocation") {
    const auto cfg = reload_config(true, false);
    const auto ns = policy_rule_namespace(cfg);
    CHECK(ns.fwmask == 0x00FF0000U);
    CHECK(ns.mark_first == 0x00010000U);
    CHECK(ns.priority_first == 401U);
    CHECK(ns.table_first == 401U);
    for (const auto& rule : expanded(planned_rules(cfg))) {
        CHECK(rule_in_namespace(rule, ns));
    }
}

TEST_CASE("probe marks: a non-strict group child gets a probe mark with lookup and guard") {
    const auto cfg = probe_config(false);
    const auto marks = allocate_outbound_marks(cfg);
    const auto rules = planned_rules(cfg);
    for (const char* tag : {"warp", "wan"}) {
        const auto probe = marks.find(probe_mark_key(tag));
        REQUIRE(probe != marks.end());
        CHECK(probe->second != marks.at(tag));
        const auto lookup = std::find_if(rules.begin(), rules.end(), [&](const RuleSpec& r) {
            return r.fwmark == probe->second && r.action == RuleAction::lookup;
        });
        const auto guard = std::find_if(rules.begin(), rules.end(), [&](const RuleSpec& r) {
            return r.fwmark == probe->second && r.action == RuleAction::unreachable;
        });
        REQUIRE(lookup != rules.end());
        REQUIRE(guard != rules.end());
        CHECK(guard->priority == lookup->priority + 1);
        CHECK(lookup->family == 0);
        // The child's own mark is still a plain, non-strict lookup.
        const auto own_guard = std::find_if(rules.begin(), rules.end(), [&](const RuleSpec& r) {
            return r.fwmark == marks.at(tag) && r.action != RuleAction::lookup;
        });
        CHECK(own_guard == rules.end());
    }
    CHECK(probe_fwmark(marks, "warp") == marks.at(probe_mark_key("warp")));
}

TEST_CASE("probe marks: a strict child or balance candidate is probed with its own mark") {
    {
        const auto cfg = probe_config(false, true);
        const auto marks = allocate_outbound_marks(cfg);
        CHECK(marks.count(probe_mark_key("warp")) == 0);
        CHECK(probe_fwmark(marks, "warp") == marks.at("warp"));
        CHECK(marks.count(probe_mark_key("wan")) == 1);
        const auto rules = planned_rules(cfg);
        CHECK(std::count_if(rules.begin(), rules.end(), [&](const RuleSpec& r) {
                  return r.fwmark == marks.at("warp");
              }) == 2);  // lookup + guard of its own mark, nothing more
    }
#ifndef KEEN_PBR_PLATFORM_KEENETIC
    // Balancing is compiled out on Keenetic, so a balance group is an ordinary
    // group there and its children keep their probe marks.
    {
        Config cfg = probe_config(false);
        auto& outbounds = *cfg.outbounds;
        outbounds[2].strategy = api::Strategy::BALANCE;
        const auto marks = allocate_outbound_marks(cfg);
        CHECK(marks.count(probe_mark_key("warp")) == 0);
        CHECK(marks.count(probe_mark_key("wan")) == 0);
    }
#endif
}

TEST_CASE("probe marks: ipv6 disabled limits the probe rules to IPv4") {
    const auto cfg = probe_config(false);
    const auto marks = allocate_outbound_marks(cfg);
    KernelRules unused;
    RouteTable routes(unused, true);
    PolicyRuleManager rules(unused, true);
    populate_routing_state(cfg, marks, routes, rules, [](const Outbound&) { return true; },
                           nullptr, false);
    const auto probe = marks.at(probe_mark_key("warp"));
    for (const auto& rule : rules.get_rules()) {
        if (rule.fwmark == probe) CHECK(rule.family == AF_INET);
    }
}

TEST_CASE("probe marks: a reload that shifts marks removes the old probe rules") {
    const auto before = probe_config(false);
    const auto after = probe_config(true);
    REQUIRE(allocate_outbound_marks(before).at(probe_mark_key("warp")) !=
            allocate_outbound_marks(after).at(probe_mark_key("warp")));
    SUBCASE("hot reload") { check_reload(before, after, true); }
    SUBCASE("restart without the previous generation") { check_reload(before, after, false); }
}

TEST_CASE("probe marks: the probe rules are in the namespace and raise no health warnings") {
    const auto cfg = probe_config(true);
    const auto rules = planned_rules(cfg);
    const auto ns = policy_rule_namespace(cfg);
    const auto kernel = expanded(rules);
    for (const auto& rule : kernel) CHECK(rule_in_namespace(rule, ns));
    CHECK(evaluate_stale_policy_rule_warnings(kernel, rules, ns).empty());
    CHECK(evaluate_fwmark_conflict_warnings({}, kernel, rules, ns.fwmask, ns).empty());
}

} // namespace keen_pbr3
