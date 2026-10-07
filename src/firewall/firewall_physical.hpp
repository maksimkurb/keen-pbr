#pragma once

// Backend-neutral, typed description of the rules that keen-pbr owns in the
// kernel ("physical ruleset").  It is the common target of
//   * the lowering of a FirewallPlan (firewall_lowering.hpp), and
//   * the parsers of real backend output (iptables-save / nft -j), below.
// A verifier can then be a pure ordered per-chain diff of two PhysicalRulesets
// with no policy knowledge.
//
// Ownership is chain-scoped: every rule inside a keen-pbr chain is captured,
// whatever its comment says.  Rules in system chains (PREROUTING/OUTPUT) are
// captured only when they jump into a keen-pbr chain.  Comments are parsed into
// an optional key for diagnostics only and never take part in equality.
//
// Representation conventions (identical for the planned and the observed side):
//   * Counters are dropped.
//   * Rule order inside a chain is significant and preserved.
//   * Marks are canonical (value, mask) pairs: result = (mark & ~mask) | value.
//   * Addresses are CIDR text with an explicit prefix length.
//   * A missing chain is not the same as an empty one for keen-pbr chains
//     (existence is observable); system chains are only listed when they hold
//     at least one hook rule, so "absent" there means "no hook".
//   * Anything the parser cannot translate is kept as an explicit Unknown*
//     element carrying the original token text, which never compares equal to
//     anything the lowering produces.

#include "firewall.hpp"
#include "firewall_rule.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace keen_pbr3 {

// ---------------------------------------------------------------------------
// Chain identity
// ---------------------------------------------------------------------------

enum class PhysicalTable : uint8_t {
  mangle,   // iptables mangle
  raw,      // iptables raw
  nft_inet, // nft `table inet KeenPbrTable`
};

enum class PhysicalChainRole : uint8_t {
  // iptables: classification chain hooked from the builtin PREROUTING chain
  // (KeenPbrRaw in raw, KeenPbrTable in mangle).  Holds the rules directly.
  iptables_prerouting,
  // iptables: classification chain hooked from mangle OUTPUT
  // (KeenPbrOutput, in both raw and mangle modes).
  iptables_output,
  // iptables: dedicated interception chains in mangle.
  iptables_dns_hold,
  iptables_sniff,
  // iptables: router-originated sniff chain, only when KeenPbrSniff cannot
  // serve OUTPUT as well (it is jumped from FORWARD and needs an `-i` match).
  iptables_sniff_out,
  // nft: base chain `prerouting` / `output` of the keen-pbr table.
  nft_prerouting,
  nft_output,
  nft_dns_hold,
  nft_sniff_forward,
  nft_sniff_output,
  // nft: balance/ct-restore setter chain `setmark_XXXXXXXX`.
  nft_setter,
  // Builtin chain of a system table.  Only hook rules (jumps into keen-pbr
  // chains) are ever captured here.
  system_prerouting,
  system_output,
  // INPUT / FORWARD / POSTROUTING (identity includes `name`).
  system_other,
  // A chain whose name belongs to keen-pbr's namespace but is not one the
  // backends create.  Kept so that it is reported instead of ignored.
  other_owned,
};

struct PhysicalChainId {
  PhysicalChainRole role{PhysicalChainRole::other_owned};
  PhysicalTable table{PhysicalTable::mangle};
  // ipv4/ipv6 for iptables tables; any for the nft inet table.
  FirewallFamily family{FirewallFamily::any};
  // nft_setter only: the mark the chain installs (from the chain name).
  uint32_t setter_mark{0};
  // Diagnostics only, except for other_owned / system_other where it is part of
  // the identity.
  std::string name;

  bool operator==(const PhysicalChainId &other) const;
  bool operator!=(const PhysicalChainId &other) const {
    return !(*this == other);
  }
};

// ---------------------------------------------------------------------------
// Matches
// ---------------------------------------------------------------------------

enum class PhysicalDir : uint8_t { src, dst };
enum class PhysicalMarkKind : uint8_t { packet, conntrack };
// Transport header used by a port match.  `any` is nft's `th`; iptables always
// has an explicit protocol.
enum class PhysicalTransport : uint8_t { any, tcp, udp };

// Set membership.  `name` is the physical set name exactly as seen.
struct SetMatch {
  std::string name;
  PhysicalDir dir{PhysicalDir::dst};
  bool negate{false};
  bool operator==(const SetMatch &o) const {
    return name == o.name && dir == o.dir && negate == o.negate;
  }
};

// Address list; a packet matches when its address equals any entry.
struct AddrMatch {
  PhysicalDir dir{PhysicalDir::dst};
  bool negate{false};
  std::vector<std::string> cidrs;
  bool operator==(const AddrMatch &o) const {
    return dir == o.dir && negate == o.negate && cidrs == o.cidrs;
  }
};

// Layer-4 protocol (tcp or udp are the only ones keen-pbr emits).
struct ProtoMatch {
  L4Proto proto{L4Proto::Tcp};
  bool operator==(const ProtoMatch &o) const { return proto == o.proto; }
};

struct PortMatch {
  PhysicalTransport transport{PhysicalTransport::any};
  PhysicalDir dir{PhysicalDir::dst};
  bool negate{false};
  std::vector<PortRange> ranges; // single port: from == to
  bool operator==(const PortMatch &o) const {
    return transport == o.transport && dir == o.dir && negate == o.negate &&
           ranges == o.ranges;
  }
};

// Numeric DSCP value (0..63).
struct DscpMatch {
  uint8_t value{0};
  bool operator==(const DscpMatch &o) const { return value == o.value; }
};

// Input interface name list.
struct IifMatch {
  bool negate{false};
  std::vector<std::string> names;
  bool operator==(const IifMatch &o) const {
    return negate == o.negate && names == o.names;
  }
};

// Output interface name list.  Only meaningful where the output device is
// already known: OUTPUT (after the routing decision), FORWARD, POSTROUTING.
struct OifMatch {
  bool negate{false};
  std::vector<std::string> names;
  bool operator==(const OifMatch &o) const {
    return negate == o.negate && names == o.names;
  }
};

// Destination address class as decided by the routing tables (nft
// `fib daddr type`, iptables `-m addrtype --dst-type`).
enum PhysicalAddrType : uint8_t {
  addr_broadcast = 1U << 0,
  addr_multicast = 1U << 1,
};
struct AddrTypeMatch {
  uint8_t types{0}; // PhysicalAddrType bits; the packet matches any of them
  bool operator==(const AddrTypeMatch &o) const { return types == o.types; }
};

// (mark & mask) == value  (or != when negate).  `values` has several entries
// for nft `mark & mask == { a, b }`.
struct MarkMatch {
  PhysicalMarkKind kind{PhysicalMarkKind::packet};
  uint32_t mask{0xFFFFFFFFu};
  bool negate{false};
  std::vector<uint32_t> values;
  bool operator==(const MarkMatch &o) const {
    return kind == o.kind && mask == o.mask && negate == o.negate &&
           values == o.values;
  }
};

// Conntrack state / status bits.
enum PhysicalCtState : uint8_t {
  ct_new = 1U << 0,
  ct_established = 1U << 1,
  ct_related = 1U << 2,
  ct_invalid = 1U << 3,
  ct_untracked = 1U << 4,
  ct_snat = 1U << 5,
  ct_dnat = 1U << 6,
};
struct CtStateMatch {
  uint8_t states{0};
  bool negate{false};
  bool operator==(const CtStateMatch &o) const {
    return states == o.states && negate == o.negate;
  }
};

// Conntrack direction; keen-pbr only matches ORIGINAL.
struct CtDirMatch {
  bool original{true};
  bool operator==(const CtDirMatch &o) const { return original == o.original; }
};

enum class ConnbytesDir : uint8_t { original, reply, both };
enum class ConnbytesMode : uint8_t { packets, bytes };
struct ConnbytesMatch {
  ConnbytesDir dir{ConnbytesDir::original};
  ConnbytesMode mode{ConnbytesMode::packets};
  uint64_t from{0};
  uint64_t to{0};
  bool operator==(const ConnbytesMatch &o) const {
    return dir == o.dir && mode == o.mode && from == o.from && to == o.to;
  }
};

// iptables `-m statistic --mode random --probability p`: matches a random
// share of the packets.  The probability is kept in the kernel's fixed point
// (`p * 2^31`, what xt_statistic stores and iptables-save renders from), so
// lowering and parser compare integers, never decimal text or floats.
inline constexpr uint32_t kStatisticProbabilityOne = 0x80000000u;
struct StatisticMatch {
  uint32_t probability{0}; // 1 .. kStatisticProbabilityOne - 1
  bool operator==(const StatisticMatch &o) const {
    return probability == o.probability;
  }
};

// Anything the parser could not translate.  Never equal to a lowered match.
struct UnknownMatch {
  std::string text;
  bool operator==(const UnknownMatch &) const { return false; }
};

using PhysicalMatch =
    std::variant<SetMatch, AddrMatch, ProtoMatch, PortMatch, DscpMatch,
                 IifMatch, OifMatch, AddrTypeMatch, MarkMatch, CtStateMatch, CtDirMatch, ConnbytesMatch,
                 StatisticMatch, UnknownMatch>;

// ---------------------------------------------------------------------------
// Statements
// ---------------------------------------------------------------------------

// mark = (mark & ~mask) | value, on the packet or conntrack mark.
struct SetMarkStmt {
  PhysicalMarkKind kind{PhysicalMarkKind::packet};
  uint32_t value{0};
  uint32_t mask{0xFFFFFFFFu};
  bool operator==(const SetMarkStmt &o) const {
    return kind == o.kind && value == o.value && mask == o.mask;
  }
};

// iptables CONNMARK --save-mark / --restore-mark, with both masks explicit.
struct CopyMarkStmt {
  bool to_conntrack{true}; // true: save-mark, false: restore-mark
  uint32_t nfmask{0xFFFFFFFFu};
  uint32_t ctmask{0xFFFFFFFFu};
  bool operator==(const CopyMarkStmt &o) const {
    return to_conntrack == o.to_conntrack && nfmask == o.nfmask &&
           ctmask == o.ctmask;
  }
};

struct JumpStmt {
  PhysicalChainId target;
  bool is_goto{false};
  bool operator==(const JumpStmt &o) const {
    return is_goto == o.is_goto && target == o.target;
  }
};

enum class PhysicalVerdict : uint8_t { accept, drop, return_ };
struct VerdictStmt {
  PhysicalVerdict verdict{PhysicalVerdict::accept};
  bool operator==(const VerdictStmt &o) const { return verdict == o.verdict; }
};

struct QueueStmt {
  uint16_t num{0};
  bool bypass{false};
  bool operator==(const QueueStmt &o) const {
    return num == o.num && bypass == o.bypass;
  }
};

struct LogStmt {
  uint16_t group{0};
  uint16_t snaplen{0};
  uint32_t threshold{1};
  bool operator==(const LogStmt &o) const {
    return group == o.group && snaplen == o.snaplen && threshold == o.threshold;
  }
};

// nft `<key> vmap { k : jump chain, from-to : jump chain, ... }`.
enum class PhysicalVmapKey : uint8_t {
  numgen_inc,         // numgen inc mod `param`
  conntrack_mark_and, // ct mark & `param`
};
// One vmap element: the key interval [from, to] (from == to for a single key).
struct VmapEntry {
  uint32_t from{0};
  uint32_t to{0};
  PhysicalChainId chain;
  bool operator==(const VmapEntry &o) const {
    return from == o.from && to == o.to && chain == o.chain;
  }
};
struct VmapStmt {
  PhysicalVmapKey key{PhysicalVmapKey::numgen_inc};
  uint32_t param{0};
  std::vector<VmapEntry> entries;
  bool operator==(const VmapStmt &o) const {
    return key == o.key && param == o.param && entries == o.entries;
  }
};

struct UnknownStmt {
  std::string text;
  bool operator==(const UnknownStmt &) const { return false; }
};

// A match that nft evaluates *after* a statement of the same rule (the
// ct-mark restore rule tests the mark again after its vmap).  Kept in position
// so the evaluation order is not lost.
struct LateMatchStmt {
  PhysicalMatch match;
  bool operator==(const LateMatchStmt &o) const { return match == o.match; }
};

using PhysicalStatement =
    std::variant<SetMarkStmt, CopyMarkStmt, JumpStmt, VerdictStmt, QueueStmt,
                 LogStmt, VmapStmt, UnknownStmt, LateMatchStmt>;

// ---------------------------------------------------------------------------
// Rules, chains, ruleset
// ---------------------------------------------------------------------------

// `PhysicalRule::plan_rule` value of rules that no plan rule produced (hook
// jumps, setter chain bodies, every parsed rule).
inline constexpr uint32_t kNoPlanRule = 0xFFFFFFFFu;

struct PhysicalRule {
  // L3 family the rule applies to.  iptables: the table family.  nft: the
  // `meta nfproto` guard, else the ip/ip6 payload family used by the matches,
  // else any.  The guard itself is not kept as a match.
  FirewallFamily family{FirewallFamily::any};
  std::vector<PhysicalMatch> matches;
  std::vector<PhysicalStatement> statements;
  // Parsed from the rule comment; diagnostics only, ignored by operator==.
  std::optional<FirewallRuleKey> key;
  // Lowering only: index into FirewallPlan::rules of the plan rule this
  // physical rule was produced from, so a verifier can attribute a difference
  // without comments.  Diagnostics only, ignored by operator==.
  uint32_t plan_rule{kNoPlanRule};
  // System-chain jumps into the interception chains only (KeenPbrDnsHold,
  // KeenPbrSniff): 0-based index of the rule among ALL rules of the builtin
  // chain, foreign rules included.  Those jumps must be the first rule of the
  // builtin chain (`-I <CHAIN> 1`), so the expected side carries 0 and the
  // parser records the observed index.  nullopt for every other rule (the
  // classification hooks are appended and their position is not tracked).
  std::optional<uint32_t> hook_position;

  // Ignores `key` and `plan_rule`.
  bool operator==(const PhysicalRule &other) const {
    return family == other.family && matches == other.matches &&
           statements == other.statements &&
           hook_position == other.hook_position;
  }
  bool operator!=(const PhysicalRule &other) const { return !(*this == other); }
};

// nft base-chain attributes (the nft equivalent of an iptables hook rule).
struct PhysicalBaseChain {
  enum class Type : uint8_t { filter, route, other };
  enum class Hook : uint8_t { prerouting, output, forward, postrouting, other };
  Type type{Type::filter};
  Hook hook{Hook::prerouting};
  int32_t priority{0};
  bool policy_accept{true};
  bool operator==(const PhysicalBaseChain &o) const {
    return type == o.type && hook == o.hook && priority == o.priority &&
           policy_accept == o.policy_accept;
  }
  bool operator!=(const PhysicalBaseChain &o) const { return !(*this == o); }
};

struct PhysicalChain {
  PhysicalChainId id;
  std::optional<PhysicalBaseChain> base; // nft base chains only
  std::vector<PhysicalRule> rules;       // in kernel evaluation order
  // iptables sniff chain only: besides FORWARD it is jumped from OUTPUT (the
  // plan sniffs router-originated traffic).  A lowering fact used to derive
  // the expected builtin-chain jumps; never parsed, so ignored by ==.
  bool output_hook{false};

  bool operator==(const PhysicalChain &o) const {
    return id == o.id && base == o.base && rules == o.rules;
  }
};

struct PhysicalRuleset {
  std::vector<PhysicalChain> chains;

  const PhysicalChain *find(const PhysicalChainId &id) const;

  // Chains compare by identity and content, regardless of listing order.
  bool operator==(const PhysicalRuleset &o) const;
  bool operator!=(const PhysicalRuleset &o) const { return !(*this == o); }
};

// ---------------------------------------------------------------------------
// Parsers
// ---------------------------------------------------------------------------

// Parse `iptables-save` / `ip6tables-save` output (optionally `-t <table>`),
// or `iptables -t <table> -S` output (which has `-N`/`-P` instead of `:chain`
// lines and no `*table` header; `default_table` is used then).  Only the
// mangle and raw tables are examined.
PhysicalRuleset parse_iptables_save(
    std::string_view output, FirewallFamily family,
    PhysicalTable default_table = PhysicalTable::mangle);

// Parse `nft -j list table inet KeenPbrTable` (or a whole-ruleset listing; only
// that table is examined).  An absent table yields an empty ruleset.  Throws
// FirewallError when the text is not an nftables JSON document.
#ifndef KEEN_PBR_PLATFORM_KEENETIC
PhysicalRuleset parse_nft_json(std::string_view json);
#endif

// Canonical form shared by parsers and lowering.  A rule is canonical when
//   * set-valued matches are sorted and deduplicated: port ranges are merged
//     when overlapping or adjacent, CIDR lists are reduced to the minimal CIDR
//     decomposition of the address set they cover (nft merges adjacent and
//     overlapping elements of anonymous sets and prints ranges), interface and
//     mark values are sorted (the kernel stores them as sets: nft lists them sorted/merged);
//   * a positive AddrMatch covering its whole family (`-d 0.0.0.0/0`, or the
//     halves nft merges into it) is dropped: it matches everything, and
//     iptables-save omits it.  The rule keeps the family it implied (`family`
//     is set from the dropped prefixes when it was `any`, so nft still gets a
//     `meta nfproto` guard).  A negated one is never dropped (lowering rejects
//     it: it would match nothing);
//   * a tcp/udp PortMatch is accompanied by the matching ProtoMatch (nft drops
//     the redundant `meta l4proto`; iptables always prints `-p`);
//   * `matches` are ordered by kind (iptables-save groups -i/-s/-d/-p before
//     extension matches, whatever order they were added in).
// Statements are never reordered.  Idempotent.
void canonicalize_physical_rule(PhysicalRule &rule);

// Canonical text of one CIDR ("a.b.c.d/len", RFC 5952 IPv6); nullopt when the
// text is not an address.  A bare address gets /32 or /128 and host bits are
// cleared, as both backends do.
std::optional<std::string> canonical_cidr(std::string_view text);
// Reduces the list to the minimal CIDR decomposition of the union of its
// addresses (overlapping and adjacent entries merge), sorted by address.
void canonicalize_cidr_list(std::vector<std::string> &cidrs);
// Minimal CIDR decomposition of the inclusive range [first, last]; empty when
// either end is not a bare address, the families differ or first > last.
std::vector<std::string> cidrs_from_range(std::string_view first,
                                          std::string_view last);
// True when the union of the prefixes is the whole address space of their
// (single) family, e.g. `0.0.0.0/0` or `0.0.0.0/1` + `128.0.0.0/1`.
bool cidrs_cover_address_family(const std::vector<std::string> &cidrs);
void canonicalize_port_ranges(std::vector<PortRange> &ranges);

// Merge `from` into `into` (used to combine per-table iptables-save outputs).
void append_physical_ruleset(PhysicalRuleset &into, PhysicalRuleset &&from);

// Classify an iptables chain name found in `table`; nullopt when the name is a
// foreign chain.  Builtin PREROUTING/OUTPUT yield the system_* roles.
std::optional<PhysicalChainId> classify_iptables_chain(std::string_view name,
                                                       PhysicalTable table,
                                                       FirewallFamily family);

} // namespace keen_pbr3
