#pragma once

// Shared by the physical-ruleset tests: the representative plan whose real
// kernel dumps live in tests/firewall_it/fixtures/physical/ (see the README
// there) and the fixture reader.

#include <doctest/doctest.h>

#include "../src/firewall/firewall_plan.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace keen_pbr3 {

inline constexpr uint32_t kCaptureMask = 0x00FF0000u;

inline FirewallRuleInstance capture_rule(std::string_view module,
                                  std::string_view semantic,
                                  FirewallRuleStage stage, int priority,
                                  FirewallFamily family,
                                  FirewallRuleCriteria criteria,
                                  FirewallRuleAction action) {
  FirewallRuleInstance rule;
  rule.key = FirewallRuleKey::compact(module, semantic);
  rule.stage = stage;
  rule.priority = priority;
  rule.family = family;
  rule.criteria = std::move(criteria);
  rule.hook = rule.criteria.apply_output ? FirewallHook::output
                                         : FirewallHook::prerouting;
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
    c.apply_output = true;
    c.dst_set_name = "kpbr4d_routed";
    route("route.mark", FirewallFamily::ipv4, c, mark1);
  }
  {
    FirewallRuleCriteria c;
    c.apply_output = true;
    c.dst_set_name = "kpbr4_hybrid";
    route("route.drop", FirewallFamily::ipv4, c, VerdictAction::drop);
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
      c.apply_output = true;
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
  }
  r.finish();
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
