#include "../firewall_rule_modules.hpp"

#include "../ip_family.hpp"
#include "../../dns/dns_router.hpp"

#include <algorithm>
#include <map>
#include <string>
#include <utility>

namespace keen_pbr3 {

namespace {
constexpr std::string_view kModuleId = "dns.detour";
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

    std::string effective_tag = outbound->tag;
    if (outbound->type != OutboundType::URLTEST &&
        outbound->type != OutboundType::ICMPTEST) {
      effective_tag = internal_detour_mark_key(outbound->tag);
    }
    const auto mark_it = context.outbound_marks->find(effective_tag);
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
}

} // namespace keen_pbr3
