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
//   iptables: the A/B generation chains (KeenPbrTable_A/B, KeenPbrRaw_A/B,
//             KeenPbrOutput_A/B), one set per enabled family;
//   nft:      the base chains `prerouting` / `output` and the setter chains.
// Dispatcher chains, builtin-chain hooks and A/B publication stay backend
// owned lifecycle; the backends expose them as PhysicalRules separately
// (Firewall::expected_hook_rules()).
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
  // iptables: A/B generation that receives the rules, per family.
  FirewallSetGeneration generation_ipv4{FirewallSetGeneration::A};
  FirewallSetGeneration generation_ipv6{FirewallSetGeneration::A};
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

// KeenPbrTable_A/B (mangle) or KeenPbrRaw_A/B (raw) holding PREROUTING rules.
const char *iptables_prerouting_generation_chain_name(
    bool raw, FirewallSetGeneration generation);
// KeenPbrOutput_A/B (raw mode OUTPUT rules).
const char *
iptables_output_generation_chain_name(FirewallSetGeneration generation);

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
