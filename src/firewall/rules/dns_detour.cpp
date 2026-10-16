#include "../firewall_rule_modules.hpp"

#include "../ip_family.hpp"
#include "../../dns/dns_router.hpp"

#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <utility>

namespace keen_pbr3 {

namespace {
constexpr std::string_view kModuleId = "dns.detour";
// Source NAT of the detoured queries; a module id of its own so the detour
// classifiers stay countable on their own.
constexpr std::string_view kSnatModuleId = "dns.detour_snat";
}

void register_dns_detour_rules(const FirewallBuildContext& context,
                               FirewallRuleRegistrar& registrar) {
  if (context.config == nullptr || context.outbound_marks == nullptr ||
      !context.config->dns.has_value()) {
    return;
  }

  const auto& dns_config = *context.config->dns;
  const auto& dns_servers =
      dns_config.servers.value_or(std::vector<DnsServer>{});
  const DnsServerRegistry dns_registry(dns_config);
  std::map<std::string, std::size_t> occurrences;
  // Outbound tag -> internal detour mark of the IPv4 detours planned.
  std::map<std::string, uint32_t> snat_marks;
  int priority = static_cast<int>(context.route_rules.size());
  for (const auto& server : dns_servers) {
    if (!server.detour.has_value()) {
      continue;
    }

    const auto outbound = std::find_if(
        context.outbounds.begin(), context.outbounds.end(),
        [&](const Outbound& candidate) {
          return candidate.tag == *server.detour;
        });
    if (outbound == context.outbounds.end()) {
      continue;
    }

    // The internal detour mark of the outbound (a urltest/icmptest one routes
    // through its selected child's table).  Only router-originated traffic
    // carries it, which the source NAT below relies on.
    const auto mark_it =
        context.outbound_marks->find(internal_detour_mark_key(outbound->tag));
    if (mark_it == context.outbound_marks->end()) {
      continue;
    }

    const auto resolved_servers = dns_registry.get_servers(server.tag);
    if (resolved_servers.empty()) {
      throw FirewallError("DNS server tag not found during detour setup: " +
                          server.tag);
    }
    for (const DnsServerConfig* resolved_server : resolved_servers) {
      const auto& address = resolved_server->resolved_ip;
      const auto port = resolved_server->port;
      const auto fwmark = mark_it->second;
      // An unresolved endpoint is skipped, not an error.
      if (address.empty() || port == 0 || fwmark == 0) {
        continue;
      }
      const auto resolved_family = ip_family_of(address);
      if (!resolved_family.has_value()) {
        throw FirewallError("invalid resolved DNS server address for tag " +
                            server.tag + ": " + address);
      }
      const FirewallFamily family = *resolved_family;
      if (family == FirewallFamily::ipv6 && !context.ipv6_enabled) {
        continue;
      }
      const char* family_label =
          family == FirewallFamily::ipv6 ? "ipv6" : "ipv4";
      if (family == FirewallFamily::ipv4) {
        snat_marks.emplace(outbound->tag, fwmark);
      }

      const std::string endpoint_id =
          "server=" + server.tag + ";route=" + outbound->tag +
          ";address=" + address + ";port=" + std::to_string(port) +
          ";family=" + family_label;
      // Preserve duplicate DNS endpoints while keeping their canonical keys unique.
      const std::size_t occurrence = occurrences[endpoint_id]++;
      for (const auto proto : {L4Proto::Tcp, L4Proto::Udp}) {
        FirewallRuleInstance rule;
        rule.key = FirewallRuleKey::compact(
            kModuleId, endpoint_id + ";occurrence=" + std::to_string(occurrence) +
                    ";proto=" + l4_proto_name(proto));
        rule.stage = FirewallRuleStage::route_classification;
        rule.priority = priority++;
        rule.hook = FirewallHook::output;
        rule.family = family;
        rule.criteria.proto = proto;
        rule.criteria.dst_port = std::to_string(port);
        rule.criteria.dst_addr = {address};
        rule.action = MarkAction{fwmark, context.fwmark_mask};
        registrar.register_rule(std::move(rule));
      }
    }
  }

  // The query socket took the source address of the main-table route before
  // the OUTPUT mark rerouted the packet; replace it with the address of the
  // interface the packet actually leaves through.  IPv4 only.
  int snat_priority = 0;
  for (const auto& [tag, mark] : snat_marks) {
    FirewallRuleInstance rule;
    rule.key = FirewallRuleKey::compact(kSnatModuleId, "route=" + tag);
    rule.stage = FirewallRuleStage::source_nat;
    rule.priority = snat_priority++;
    rule.hook = FirewallHook::postrouting;
    rule.family = FirewallFamily::ipv4;
    rule.action = MasqueradeAction{mark, context.fwmark_mask};
    registrar.register_rule(std::move(rule));
  }
}

} // namespace keen_pbr3
