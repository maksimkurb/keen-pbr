#pragma once

// Lowering: FirewallPlan (logical rules owned by policy modules) -> ordered
// PhysicalRuleset (see firewall_physical.hpp).  This is the one place that
// knows how every logical action becomes physical rules for each backend:
// family expansion, raw vs mangle placement, OUTPUT chains, conntrack restore
// companions, iptables multi-interface fragments and protocol/address/port
// expansion, nft default-gateway companions, balance numgen/vmap and setter
// chains, and the ownership key kept as a comment.
//
// The result is exactly what the parsers of firewall_physical.hpp produce for
// the kernel state the backend creates from it: every produced rule goes
// through canonicalize_physical_rule(), marks are canonical (value, mask)
// pairs, and rule order inside a chain is plan order (stage, priority,
// insertion order), never reordered.
//
// What is covered: the rule CONTENT of the owned chains
//   iptables: the classification chains KeenPbrRaw (raw) or KeenPbrTable
//             (mangle) for PREROUTING and KeenPbrOutput (mangle) for OUTPUT,
//             one pair per enabled family;
//   nft:      the base chains `prerouting` / `output` and the setter chains.
// Builtin-chain hooks stay backend owned lifecycle; the backends expose them as
// PhysicalRules separately (Firewall::expected_hook_rules()).
//
// iptables placement: route rules (hook=prerouting) go to both the PREROUTING
// and the OUTPUT chain; hook=output rules (DNS detour) only to OUTPUT; the
// inbound-interface prefilter and its multi-interface fragments only to
// PREROUTING; restore-conntrack and DNAT skip need conntrack (not in raw).
//
// Interception (QueueAction / LogAction) never enters the classification
// chains and ignores raw mode: iptables KeenPbrDnsHold (hook postrouting) and
// KeenPbrSniff (hooks forward and output, one shared chain, equal rules not
// repeated) in mangle per enabled family; nft base chains dns_hold,
// sniff_fwd and sniff_out of the keen-pbr table, present only when non-empty.
//
// The function is pure: it never inspects the system.

#include "firewall_physical.hpp"
#include "firewall_plan.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace keen_pbr3 {

// Backend-physical facts that cannot be derived from the plan.
struct FirewallLoweringContext {
  FirewallBackend backend{FirewallBackend::iptables};
  // iptables: forwarded traffic is classified in raw PREROUTING per family
  // (OUTPUT always stays in mangle).  Ignored by nft.
  RawPreroutingMode raw_prerouting{};
  // Families that are materialized; false drops every IPv6 rule/chain.
  bool ipv6_enabled{true};
  // iptables: whether `-m comment` ownership keys can be emitted per family.
  // A rule keeps its key only when its comment will be emitted.  nft always
  // supports comments.
  bool comments_ipv4_supported{true};
  bool comments_ipv6_supported{true};
  // iptables: whether `-j NFLOG --nflog-size` is accepted (iptables >= 1.6.0).
  // Without it the rule carries no snaplen; the daemon's NFLOG group copy
  // range bounds the payload instead.  nft always supports snaplen.
  bool nflog_size_ipv4_supported{true};
  bool nflog_size_ipv6_supported{true};
  uint32_t fwmark_mask{0xFFFFFFFFu};
  // nft: marks that always get a setter chain (marks allocated to this daemon
  // instance), in addition to the marks used by the plan.
  std::vector<uint32_t> owned_marks;
  // Logical -> physical set name (as Firewall::physical_set_name); identity
  // when empty.
  std::function<std::string(const std::string &)> physical_set_name;
};

PhysicalRuleset lower_firewall_plan(const FirewallPlan &plan,
                                    const FirewallLoweringContext &context);

// ---------------------------------------------------------------------------
// Chain naming shared by lowering and the backends.
// ---------------------------------------------------------------------------

// KeenPbrRaw (raw) or KeenPbrTable (mangle): the PREROUTING classification
// chain.
const char *iptables_prerouting_chain_name(bool raw);
// KeenPbrOutput (mangle, both modes): the OUTPUT classification chain.
const char *iptables_output_chain_name();
// KeenPbrDnsHold (mangle): NFQUEUE of DNS responses, jumped from POSTROUTING
// position 1.
const char *iptables_dns_hold_chain_name();
// KeenPbrSniff (mangle): NFLOG of the first packets of new flows, jumped from
// FORWARD and OUTPUT position 1.
const char *iptables_sniff_chain_name();
// KeenPbrSniffOut (mangle): the router-originated sniff rules, jumped from
// OUTPUT only; present when the forward sniff rules need an `-i` allowlist and
// the shared KeenPbrSniff would match forwarded packets of any interface.
const char *iptables_sniff_out_chain_name();

PhysicalChainId iptables_physical_chain_id(const std::string &name,
                                           PhysicalTable table,
                                           FirewallFamily family);
// nft chain identity; `mark` is used for the setter role only.
PhysicalChainId nft_physical_chain_id(PhysicalChainRole role,
                                      uint32_t mark = 0);
// `setmark_XXXXXXXX`.
std::string nft_setter_chain_name(uint32_t mark);

// ---------------------------------------------------------------------------
// Classifier expansion (family / protocol / address / gateway companion).
// Lowering builds on it.
// ---------------------------------------------------------------------------

// Backend-neutral physical classifier form.
struct FirewallPhysicalClassifier {
  FirewallFamily family{FirewallFamily::ipv4};
  FirewallHook hook{FirewallHook::prerouting};
  FirewallRuleCriteria criteria;
  FirewallRuleAction action{MarkAction{}};
};

// Materialize the generic classifier forms emitted by either backend.  This
// is pure: it does not resolve sets or inspect backend/system state.
std::vector<FirewallPhysicalClassifier> materialize_firewall_classifiers(
    const FirewallRuleInstance &rule, FirewallBackend backend,
    uint32_t fwmark_mask);

} // namespace keen_pbr3
