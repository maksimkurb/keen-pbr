#pragma once

// Shared by the physical-ruleset tests: the representative plan whose real
// kernel dumps live in tests/firewall_it/fixtures/physical/ (see the README
// there) and the fixture reader.

#include <doctest/doctest.h>

#include "../src/firewall/firewall_plan.hpp"
#include "../src/firewall/firewall_rule_modules.hpp"
#include "../src/firewall/firewall_runtime.hpp"

#include <filesystem>
#include <map>
#include <optional>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace keen_pbr3 {

// Backend-builder test vocabulary for the prefilter portion of a plan. It is
// only a fixture shorthand mapped onto plan rules by the builder tests; the
// production code derives these decisions solely from the plan modules.
struct PrefilterFixture {
  std::optional<std::vector<std::string>> inbound_interfaces;
  bool skip_established_or_dnat{false};
  bool skip_marked_packets{false};
  bool restore_conntrack_mark{false};
  uint32_t conntrack_mark_mask{0};
  std::string restore_conntrack_mark_comment;
  std::string skip_established_or_dnat_comment;
  std::string skip_marked_packets_comment;
  std::string inbound_interface_filter_comment;
  bool comments_ipv4_supported{true};
  bool comments_ipv6_supported{true};

  bool has_inbound_interfaces() const {
    return inbound_interfaces.has_value() && !inbound_interfaces->empty();
  }
  bool comments_supported(bool ipv6) const {
    return ipv6 ? comments_ipv6_supported : comments_ipv4_supported;
  }
};

// Prefilter fixture derived from a config through the real plan modules
// (bypass and interface guard only; conntrack restore stays off).
inline PrefilterFixture prefilter_fixture_from_config(
    const Config& config,
    FirewallBackend backend = FirewallBackend::iptables) {
  const OutboundMarkMap marks;
  const std::map<std::string, ListSetUsage> usage;
  const std::vector<DumpedRoute> routes;
  const std::vector<DumpedInterface> interfaces;
  const FirewallPlan plan = build_firewall_plan(
      {config, marks, usage, routes, interfaces, nullptr, true, 0xFFFFFFFFu,
       backend});
  PrefilterFixture fixture;
  fixture.skip_established_or_dnat =
      plan_has_action<SkipEstablishedOrDnatAction>(plan);
  fixture.skip_marked_packets = plan_has_action<SkipMarkedPacketsAction>(plan);
  for (const auto& rule : plan.rules) {
    if (const auto* inbound =
            std::get_if<InboundInterfaceFilterAction>(&rule.action)) {
      fixture.inbound_interfaces = inbound->interfaces;
    }
  }
  return fixture;
}

inline constexpr uint32_t kCaptureMask = 0x00FF0000u;

inline FirewallRuleInstance capture_rule(std::string_view module,
                                  std::string_view semantic,
                                  FirewallRuleStage stage, int priority,
                                  FirewallFamily family,
                                  FirewallRuleCriteria criteria,
                                  FirewallRuleAction action,
                                  FirewallHook hook = FirewallHook::prerouting) {
  FirewallRuleInstance rule;
  rule.key = FirewallRuleKey::compact(module, semantic);
  rule.stage = stage;
  rule.priority = priority;
  rule.family = family;
  rule.criteria = std::move(criteria);
  rule.hook = hook;
  rule.action = std::move(action);
  return rule;
}

inline FirewallPlan capture_plan(bool nft, bool two_interfaces, bool conntrack) {
  FirewallPlan plan;
  plan.fwmark_mask = kCaptureMask;
  FirewallRuleRegistrar r(plan);
  r.register_set({"kpbr4_hybrid", FirewallFamily::ipv4, 0});
  r.register_set({"kpbr6_hybrid", FirewallFamily::ipv6, 0});
  r.register_set({"kpbr4d_routed", FirewallFamily::ipv4, 300});
  using S = FirewallRuleStage;
  if (conntrack) {
    r.register_rule(capture_rule("prefilter.restore_conntrack_mark", "mask",
                                 S::restore_conntrack, 0, FirewallFamily::any,
                                 {}, RestoreConntrackMarkAction{kCaptureMask}));
    r.register_rule(capture_rule("prefilter.skip_local_replies", "reply",
                                 S::global_bypass, -1, FirewallFamily::any, {},
                                 SkipLocalRepliesAction{}));
    r.register_rule(capture_rule("prefilter.skip_established_or_dnat", "dnat",
                                 S::global_bypass, 0, FirewallFamily::any, {},
                                 SkipEstablishedOrDnatAction{}));
  }
  r.register_rule(capture_rule("prefilter.skip_marked_packets", "all",
                               S::global_bypass, 1, FirewallFamily::any, {},
                               SkipMarkedPacketsAction{}));
  InboundInterfaceFilterAction inbound;
  inbound.interfaces = two_interfaces
      ? std::vector<std::string>{"lan0", "br-guest"}
      : std::vector<std::string>{"lan0"};
  r.register_rule(capture_rule("prefilter.inbound_interface_filter", "lan",
                               S::global_bypass, 2, FirewallFamily::any, {},
                               inbound));
  int index = 0;
  const auto route = [&](std::string_view module, FirewallFamily family,
                         FirewallRuleCriteria criteria,
                         FirewallRuleAction action) {
    r.register_rule(capture_rule(module, "r" + std::to_string(index),
                                 S::route_classification, index, family,
                                 std::move(criteria), std::move(action)));
    ++index;
  };
  const MarkAction mark1{0x10000u, kCaptureMask};
  const MarkAction mark2{0x20000u, kCaptureMask};
  {
    FirewallRuleCriteria c;
    c.dst_set_name = "kpbr4_hybrid";
    c.proto = L4Proto::Tcp;
    c.dst_port = "443";
    route("route.mark", FirewallFamily::ipv4, c, mark1);
  }
  {
    FirewallRuleCriteria c;
    c.dst_set_name = "kpbr6_hybrid";
    c.proto = L4Proto::Tcp;
    c.dst_port = "443";
    route("route.mark", FirewallFamily::ipv6, c, mark1);
  }
  {
    FirewallRuleCriteria c;
    c.dst_set_name = "kpbr4_hybrid";
    c.proto = L4Proto::Udp;
    c.src_addr = {"10.0.0.0/8"};
    c.negate_src_addr = true;
    c.dst_port = "53";
    route("route.drop", FirewallFamily::ipv4, c, VerdictAction::drop);
  }
  {
    FirewallRuleCriteria c;
    c.proto = L4Proto::Udp;
    c.dst_addr = {"8.8.8.8", "9.9.9.9/32"};
    c.dst_port = "53";
    route("route.pass", FirewallFamily::ipv4, c, VerdictAction::pass);
  }
  {
    FirewallRuleCriteria c;
    c.src_port = "1111";
    route("route.mark", FirewallFamily::ipv4, c, mark2);
  }
  {
    FirewallRuleCriteria c;
    c.proto = L4Proto::Tcp;
    c.src_addr = {"192.168.1.0/24"};
    c.dst_port = "443";
    c.negate_dst_port = true;
    route("route.drop", FirewallFamily::ipv4, c, VerdictAction::drop);
  }
  {
    FirewallRuleCriteria c;
    c.proto = L4Proto::Udp;
    c.dst_addr = {"2001:db8:53::53"};
    c.dst_port = "53";
    route("route.mark", FirewallFamily::ipv6, c, mark2);
  }
  {
    FirewallRuleCriteria c;
    c.proto = L4Proto::Tcp;
    c.dscp = 46;
    c.dst_port = "80,443,8000-9000";
    route("route.mark", FirewallFamily::ipv4, c, mark1);
  }
  {
    FirewallRuleCriteria c;
    c.dscp = 10;
    c.src_addr = {"192.0.2.0/24"};
    route("route.mark", FirewallFamily::ipv4, c, mark1);
  }
  {
    FirewallRuleCriteria c;
    c.proto = L4Proto::Tcp;
    c.src_port = "1024-65535";
    c.dst_port = "22";
    route("route.mark", FirewallFamily::ipv4, c, mark2);
  }
  {
    FirewallRuleCriteria c;
    c.dst_set_name = "kpbr4d_routed";
    r.register_rule(capture_rule("route.mark", "r" + std::to_string(index),
                                 S::route_classification, index, FirewallFamily::ipv4,
                                 c, mark1, FirewallHook::output));
    ++index;
  }
  {
    FirewallRuleCriteria c;
    c.dst_set_name = "kpbr4_hybrid";
    r.register_rule(capture_rule("route.drop", "r" + std::to_string(index),
                                 S::route_classification, index, FirewallFamily::ipv4,
                                 c, VerdictAction::drop, FirewallHook::output));
    ++index;
  }
  if (nft) {
    {
      FirewallRuleCriteria c;
      c.default_gateway = DefaultGatewayFamily::Ipv4;
      c.default_gateway_bypass = {"10.0.0.0/8", "192.168.0.0/16"};
      route("route.mark", FirewallFamily::ipv4, c, mark1);
    }
    {
      FirewallRuleCriteria c;
      c.default_gateway = DefaultGatewayFamily::Ipv6;
      c.default_gateway_bypass = {"fd00::/8"};
      route("route.mark", FirewallFamily::ipv6, c, mark2);
    }
    {
      FirewallRuleCriteria c;
      c.dst_set_name = "kpbr4_hybrid";
      c.proto = L4Proto::Udp;
      route("route.balance", FirewallFamily::ipv4, c,
            BalanceAction{0x30000u, {{0x10000u, true, false},
                                     {0x20000u, true, false}}});
    }
    {
      FirewallRuleCriteria c;
      c.dst_port = "8443";
      c.proto = L4Proto::Tcp;
      route("route.balance", FirewallFamily::ipv6, c,
            BalanceAction{0x30000u, {{0x10000u, false, true},
                                     {0x20000u, false, true},
                                     {0x40000u, false, true}}});
    }
    {
      // Weighted: 70/20/10 reduce to 7/2/1 -> numgen mod 10, ranges 0-6, 7-8
      // and the single key 9.
      FirewallRuleCriteria c;
      c.dst_port = "9443";
      c.proto = L4Proto::Tcp;
      route("route.balance", FirewallFamily::ipv4, c,
            BalanceAction{0x30000u, {{0x10000u, true, false, 70},
                                     {0x20000u, true, false, 20},
                                     {0x40000u, true, false, 10}}});
    }
  }
  r.finish();
  return plan;
}

// iptables balance cascades: a two-way IPv4 split behind a set, a three-way
// IPv6 split on a port, and a family-agnostic rule whose candidates differ per
// family (so both tables get a cascade of a different length), behind the
// restore prefilter that keeps established flows on their candidate.
inline FirewallPlan capture_plan_iptables_balance() {
  FirewallPlan plan;
  plan.fwmark_mask = kCaptureMask;
  FirewallRuleRegistrar r(plan);
  r.register_set({"kpbr4_hybrid", FirewallFamily::ipv4, 0});
  using S = FirewallRuleStage;
  r.register_rule(capture_rule("prefilter.restore_conntrack_mark", "mask",
                               S::restore_conntrack, 0, FirewallFamily::any, {},
                               RestoreConntrackMarkAction{kCaptureMask}));
  r.register_rule(capture_rule("prefilter.skip_marked_packets", "all",
                               S::global_bypass, 1, FirewallFamily::any, {},
                               SkipMarkedPacketsAction{}));
  int index = 0;
  const auto route = [&](FirewallFamily family, FirewallRuleCriteria criteria,
                         FirewallRuleAction action) {
    r.register_rule(capture_rule("route.balance", "r" + std::to_string(index),
                                 S::route_classification, index, family,
                                 std::move(criteria), std::move(action)));
    ++index;
  };
  {
    FirewallRuleCriteria c;
    c.dst_set_name = "kpbr4_hybrid";
    c.proto = L4Proto::Udp;
    route(FirewallFamily::ipv4, c,
          BalanceAction{0x30000u, {{0x10000u, true, false},
                                   {0x20000u, true, false}}});
  }
  {
    FirewallRuleCriteria c;
    c.dst_port = "8443";
    c.proto = L4Proto::Tcp;
    route(FirewallFamily::ipv6, c,
          BalanceAction{0x30000u, {{0x10000u, false, true},
                                   {0x20000u, false, true},
                                   {0x40000u, false, true}}});
  }
  {
    FirewallRuleCriteria c;
    c.dscp = 46;
    route(FirewallFamily::any, c,
          BalanceAction{0x30000u, {{0x10000u, true, true},
                                   {0x20000u, true, true},
                                   {0x40000u, true, false},
                                   {0x50000u, true, false},
                                   {0x60000u, true, false},
                                   {0x70000u, true, false},
                                   {0x80000u, false, true}}});
  }
  {
    // Weighted 70/20/10: picks with 7/10, then 2/3, then unconditionally.
    FirewallRuleCriteria c;
    c.dst_port = "9443";
    c.proto = L4Proto::Tcp;
    route(FirewallFamily::ipv4, c,
          BalanceAction{0x30000u, {{0x10000u, true, false, 70},
                                   {0x20000u, true, false, 20},
                                   {0x40000u, true, false, 10}}});
  }
  r.finish();
  return plan;
}

// Catch-all address spellings (`0.0.0.0/0`, `::/0`, and the halves nft merges
// into them) on route rules, behind the restore/reply-skip prefilters.
inline FirewallPlan capture_plan_catch_all() {
  FirewallPlan plan;
  plan.fwmark_mask = kCaptureMask;
  FirewallRuleRegistrar r(plan);
  using S = FirewallRuleStage;
  r.register_rule(capture_rule("prefilter.restore_conntrack_mark", "mask",
                               S::restore_conntrack, 0, FirewallFamily::any,
                               {}, RestoreConntrackMarkAction{kCaptureMask}));
  r.register_rule(capture_rule("prefilter.skip_local_replies", "reply",
                               S::global_bypass, -1, FirewallFamily::any, {},
                               SkipLocalRepliesAction{}));
  int index = 0;
  const auto route = [&](FirewallFamily family, FirewallRuleCriteria criteria) {
    r.register_rule(capture_rule("route.mark", "r" + std::to_string(index),
                                 S::route_classification, index, family,
                                 std::move(criteria),
                                 MarkAction{0x10000u, kCaptureMask}));
    ++index;
  };
  const std::vector<std::vector<std::string>> v4 = {
      {"0.0.0.0/0"}, {"0.0.0.0/1", "128.0.0.0/1"}, {"10.0.0.0/8"}};
  for (const auto& dst : v4) {
    FirewallRuleCriteria c;
    c.proto = L4Proto::Udp;
    c.dst_addr = dst;
    route(FirewallFamily::ipv4, c);
  }
  {
    FirewallRuleCriteria c;
    c.proto = L4Proto::Tcp;
    c.src_addr = {"0.0.0.0/0"};
    c.dst_port = "443";
    route(FirewallFamily::ipv4, c);
  }
  {
    FirewallRuleCriteria c;
    c.proto = L4Proto::Udp;
    c.dst_addr = {"::/0"};
    route(FirewallFamily::ipv6, c);
  }
  {
    FirewallRuleCriteria c;
    c.dst_addr = {"::/1", "8000::/1"};
    route(FirewallFamily::ipv6, c);
  }
  r.finish();
  return plan;
}

// PLAN O: inbound interfaces plus a positive UDP catch-all route rule, behind
// the restore/reply-skip prefilters and the skip_lan_output rules (OUTPUT
// only: LAN oif, broadcast, multicast).
inline FirewallPlan capture_plan_lan_output() {
  FirewallPlan plan;
  plan.fwmark_mask = kCaptureMask;
  FirewallRuleRegistrar r(plan);
  using S = FirewallRuleStage;
  r.register_rule(capture_rule("prefilter.restore_conntrack_mark", "mask",
                               S::restore_conntrack, 0, FirewallFamily::any,
                               {}, RestoreConntrackMarkAction{kCaptureMask}));
  r.register_rule(capture_rule("prefilter.skip_local_replies", "reply",
                               S::global_bypass, -1, FirewallFamily::any, {},
                               SkipLocalRepliesAction{}));
  const auto skip = [&](std::string_view instance,
                        SkipLanOutputAction action) {
    r.register_rule(capture_rule("prefilter.skip_lan_output", instance,
                                 S::global_bypass, -1, FirewallFamily::any, {},
                                 std::move(action), FirewallHook::output));
  };
  skip("lan_oif", SkipLanOutputAction{SkipLanOutputAction::Kind::lan_oif,
                                      {"lan0", "br-guest"}});
  skip("bcast",
       SkipLanOutputAction{SkipLanOutputAction::Kind::broadcast, {}});
  skip("mcast",
       SkipLanOutputAction{SkipLanOutputAction::Kind::multicast, {}});
  InboundInterfaceFilterAction inbound;
  inbound.interfaces = {"lan0", "br-guest"};
  r.register_rule(capture_rule("prefilter.inbound_interface_filter", "lan",
                               S::global_bypass, 2, FirewallFamily::any, {},
                               inbound));
  FirewallRuleCriteria c;
  c.proto = L4Proto::Udp;
  c.dst_addr = {"0.0.0.0/0"};
  r.register_rule(capture_rule("route.mark", "r0", S::route_classification, 0,
                               FirewallFamily::ipv4, std::move(c),
                               MarkAction{0x10000u, kCaptureMask}));
  r.finish();
  return plan;
}

// The rules of the interception policy modules (DNS hold, L7 sniff) for
// `settings`, exactly as the production modules plan them.  `inbound` is
// route.inbound_interfaces, `wan_interfaces` the interfaces of interface
// outbounds; `router_traffic` is iproute.process_router_traffic (the default
// keeps the historic behaviour of hand-built contexts).
inline std::vector<FirewallRuleInstance> intercept_module_rules(
    const InterceptFirewallSettings& settings,
    const std::vector<std::string>& inbound = {},
    const std::vector<std::string>& wan_interfaces = {},
    bool router_traffic = true) {
  static const std::vector<RouteRule> route_rules;
  std::vector<Outbound> outbounds;
  for (const auto& name : wan_interfaces) {
    Outbound outbound;
    outbound.tag = name;
    outbound.type = OutboundType::INTERFACE;
    outbound.interface = name;
    outbounds.push_back(std::move(outbound));
  }
  Config config;
  if (!inbound.empty()) {
    config.route = RouteConfig{};
    config.route->inbound_interfaces = inbound;
  }
  static const std::map<std::string, ListConfig> lists;
  static const std::map<std::string, ListSetUsage> usage;
  static const std::vector<DumpedRoute> main_routes;
  static const std::vector<DumpedInterface> interfaces;
  FirewallBuildContext context{route_rules, outbounds, lists, usage,
                               main_routes, interfaces};
  context.config = &config;
  context.intercept = settings;
  context.process_router_traffic = router_traffic;
  FirewallPlan plan;
  plan.fwmark_mask = kCaptureMask;
  FirewallRuleRegistrar registrar(plan);
  register_intercept_dns_hold_rules(context, registrar);
  register_intercept_l7_sniff_rules(context, registrar);
  registrar.finish();
  return plan.rules;
}

// The representative capture plan plus the interception rules (all of them
// enabled with the default settings).  Their stage sorts after every other
// rule, so appending keeps the plan order.  Without `inbound` the learning
// scope excludes two WAN interfaces; with it, learning is limited to them.
inline FirewallPlan capture_plan_with_intercept(
    bool nft, bool two_interfaces, bool conntrack,
    const std::vector<std::string>& inbound = {}) {
  FirewallPlan plan = capture_plan(nft, two_interfaces, conntrack);
  InterceptFirewallSettings settings;
  settings.dns_hold = true;
  settings.l7_sniff = true;
  for (auto& rule : intercept_module_rules(
           settings, inbound, inbound.empty()
                                  ? std::vector<std::string>{"wan0", "wan1"}
                                  : std::vector<std::string>{})) {
    rule.insertion_order = plan.rules.size();
    plan.rules.push_back(std::move(rule));
  }
  return plan;
}

inline std::string read_fixture(const std::string &name) {
  const std::filesystem::path path =
      std::filesystem::path(__FILE__).parent_path() / "firewall_it" /
      "fixtures" / "physical" / name;
  std::ifstream input(path, std::ios::binary);
  REQUIRE_MESSAGE(input.good(), "missing fixture " << path.string());
  std::ostringstream contents;
  contents << input.rdbuf();
  return contents.str();
}

} // namespace keen_pbr3
