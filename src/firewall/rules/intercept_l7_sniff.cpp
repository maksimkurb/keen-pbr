#include "../firewall_rule_modules.hpp"

#include <string>
#include <utility>

namespace keen_pbr3 {

namespace {
constexpr std::string_view kModuleId = "l7.sniff";

void register_sniff(FirewallRuleRegistrar& registrar,
                    const InterceptFirewallSettings& settings,
                    std::string_view instance, FirewallHook hook, L4Proto proto,
                    const char* ports, const InterceptClientScope& scope) {
  FirewallRuleInstance rule;
  rule.key = FirewallRuleKey{std::string(kModuleId), std::string(instance)};
  rule.stage = FirewallRuleStage::interception;
  rule.hook = hook;
  rule.family = FirewallFamily::any;
  rule.criteria.proto = proto;
  rule.criteria.dst_port = PortSpec(ports);
  rule.criteria.connbytes_original_packets =
      PacketCountRange{1, settings.max_packets};
  // Flows are learned from their request direction only (replies carry no
  // client request), and forwarded ones only for clients on allowed
  // interfaces: a packet arriving from the internet (port forward, IPv6 LAN
  // host) or from an unlisted segment is not parsed.  Router-originated
  // traffic has no input interface and follows process_router_traffic alone.
  rule.criteria.ct_original = true;
  if (hook == FirewallHook::forward) {
    rule.criteria.include_iif = scope.include;
    rule.criteria.exclude_iif = scope.exclude;
  }
  rule.action = LogAction{settings.nflog_group, settings.snaplen};
  registrar.register_rule(std::move(rule));
}
} // namespace

// Copy the first packets of new flows (TCP 80/443, UDP 443) to an NFLOG group
// for TLS SNI / HTTP Host / QUIC Initial parsing.  Forwarded traffic is always
// sniffed (original direction, client interface in the allowed scope);
// router-originated traffic only with process_router_traffic.
// Disabled unless interception settings enable it explicitly.
void register_intercept_l7_sniff_rules(const FirewallBuildContext& context,
                                       FirewallRuleRegistrar& registrar) {
  if (!context.intercept.has_value() || !context.intercept->l7_sniff) {
    return;
  }
  const auto& settings = *context.intercept;
  const char* tcp_ports = settings.http && settings.tls ? "80,443"
                          : settings.http               ? "80"
                          : settings.tls                ? "443"
                                                        : nullptr;
  const auto scope = intercept_client_scope(context);
  for (const auto hook : {FirewallHook::forward, FirewallHook::output}) {
    if (hook == FirewallHook::output && !context.process_router_traffic) {
      continue;
    }
    const std::string suffix =
        hook == FirewallHook::forward ? ".forward" : ".output";
    if (tcp_ports != nullptr) {
      register_sniff(registrar, settings, "tcp" + suffix, hook, L4Proto::Tcp,
                     tcp_ports, scope);
    }
    if (settings.quic) {
      register_sniff(registrar, settings, "udp" + suffix, hook, L4Proto::Udp,
                     "443", scope);
    }
  }
}

} // namespace keen_pbr3
