#pragma once

#include "port_spec_util.hpp"

#include <cstdint>
#include <memory>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <vector>

namespace keen_pbr3 {

class ListEntryVisitor;
struct FirewallPlan;
struct PhysicalRuleset;
enum class DefaultGatewayFamily : uint8_t { None, Ipv4, Ipv6 };

struct FirewallBalanceCandidate {
  uint32_t fwmark;
  bool ipv4{true};
  bool ipv6{true};

  bool operator==(const FirewallBalanceCandidate& other) const {
    return fwmark == other.fwmark && ipv4 == other.ipv4 && ipv6 == other.ipv6;
  }

  bool operator!=(const FirewallBalanceCandidate& other) const {
    return !(*this == other);
  }
};

using FirewallBalanceCandidates =
    std::map<std::string, std::vector<FirewallBalanceCandidate>>;
enum class L4Proto : uint8_t {
  Any,
  Tcp,
  Udp,
  TcpUdp,
};

inline const char *l4_proto_name(L4Proto proto) {
  switch (proto) {
  case L4Proto::Any:
    return "";
  case L4Proto::Tcp:
    return "tcp";
  case L4Proto::Udp:
    return "udp";
  case L4Proto::TcpUdp:
    return "tcp/udp";
  }
  return "";
}

// Inclusive packet-count window of the conntrack entry (connbytes).
struct PacketCountRange {
  uint32_t from{0};
  uint32_t to{0};
};

// Match criteria for firewall mark/drop/pass rules.
// All fields default to empty meaning "any".
struct FirewallRuleCriteria {
  std::optional<std::string>
      dst_set_name;            // named destination set matcher, if any
  std::optional<uint8_t> dscp; // DSCP tag selector, empty = any
  L4Proto proto = L4Proto::Any;
  PortSpec src_port;                 // parsed source port selector
  PortSpec dst_port;                 // parsed destination port selector
  std::vector<std::string> src_addr; // CIDR list, empty = any source address
  std::vector<std::string>
      dst_addr;                 // CIDR list, empty = any destination address
  bool negate_src_port = false; // if true, match packets NOT from src_port
  bool negate_dst_port = false; // if true, match packets NOT to dst_port
  bool negate_src_addr = false; // if true, match packets NOT from src_addr
  bool negate_dst_addr = false; // if true, match packets NOT to dst_addr
  DefaultGatewayFamily default_gateway = DefaultGatewayFamily::None;
  std::vector<std::string> default_gateway_bypass;
  // Reply-direction packets of an established connection (interception only).
  bool ct_established_reply = false;
  // Output interfaces the packet must NOT leave through (interception only);
  // empty = any.  Used to keep loopback replies to router-local processes out
  // of the DNS hold.
  std::vector<std::string> exclude_oif;
  // Original-direction packet count window of the connection (interception
  // only); needs conntrack.
  std::optional<PacketCountRange> connbytes_original_packets;
  bool empty() const {
    return !dst_set_name.has_value() && !dscp.has_value() &&
           !ct_established_reply && exclude_oif.empty() &&
           !connbytes_original_packets.has_value() &&
           proto == L4Proto::Any && src_port.empty() && dst_port.empty() &&
           src_addr.empty() && dst_addr.empty() &&
           default_gateway == DefaultGatewayFamily::None;
  }

  bool has_rule_selector() const {
    return dst_set_name.has_value() || dscp.has_value() || !src_port.empty() ||
           !dst_port.empty() || !src_addr.empty() || !dst_addr.empty() ||
           default_gateway != DefaultGatewayFamily::None;
  }
};

using ProtoPortFilter = FirewallRuleCriteria;

class FirewallError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

// Raised when an inspection-only RulesOnly apply cannot safely reuse the
// currently live firewall sets. Callers may retry exactly once with the
// conservative PreserveSets mode.
class FirewallRulesOnlyError : public FirewallError {
public:
  using FirewallError::FirewallError;
};

// Concrete firewall backend selected for runtime use.
enum class FirewallBackend : uint8_t { iptables, nftables };

// User-facing backend preference from config.
enum class FirewallBackendPreference : uint8_t {
  auto_detect,
  iptables,
  nftables
};

// Independent placement of forwarded traffic classification.  OUTPUT always
// remains in mangle; these flags select only the PREROUTING table per family.
struct RawPreroutingMode {
  bool ipv4{false};
  bool ipv6{false};

  bool uses(bool is_ipv6) const { return is_ipv6 ? ipv6 : ipv4; }
};

// How pending firewall changes should be applied.
enum class FirewallApplyMode : uint8_t {
  // Recreate backend-owned firewall state from scratch, including sets.
  Destructive,
  // Refresh chains/rules while preserving existing set contents when possible.
  PreserveSets,
  // Refresh static list-backed elements while preserving dynamic DNS sets.
  // Backends may rebuild equivalent chains to publish the refreshed sets.
  StaticSetsOnly,
  // Rebuild packet-classification rules while reusing the currently live
  // static and dynamic sets. This mode must not mutate any set or stream list
  // contents; callers fall back to PreserveSets when preflight fails.
  RulesOnly
};

// What the backend actually realized for one successful apply. Kept apart from
// FirewallPlan, which is desired intent only and never carries apply results.
struct FirewallApplyResult {
  FirewallApplyMode mode{FirewallApplyMode::Destructive};
  // Sorted, unique physical names of the sets declared by the applied plan.
  // RulesOnly checks that every set its plan needs was realized by the
  // previous apply (set names are stable across applies).
  std::vector<std::string> physical_set_names;
  // The complete ruleset keen-pbr expects in the kernel after this apply
  // (Firewall::expected_ruleset), lowered once at apply time so verification
  // never re-lowers the plan.  Null only for a default-constructed result.
  std::shared_ptr<const PhysicalRuleset> expected_ruleset;

  bool has_physical_set(const std::string &name) const;
};

// Return the kernel-normalized initial hashsize for an ipset declaration.
// The result is absent when the normalized value cannot be represented by
// ipset's uint32 command representation.
std::optional<uint32_t> normalize_ipset_hashsize(uint32_t requested);

// Abstract firewall interface for managing IP sets and packet marking rules.
// Both iptables and nftables backends implement this interface.
//
// Usage pattern (transactional rebuild):
//   prepare_apply() → stage plan.sets with create_ipset() →
//   create_batch_loader() streams entries → apply(plan) commits everything
//   using the requested apply mode
class Firewall {
public:
  virtual ~Firewall() = default;

  // Start a new buffered apply attempt. Backends use this to probe
  // capabilities and discard buffers before rules and set contents are queued.
  virtual void prepare_apply(FirewallApplyMode mode) { (void)mode; }

  // Physical set names are stable and equal to the logical names:
  // kpbr4_<list> / kpbr6_<list> (static), kpbr4d_<list> / kpbr6d_<list>
  // (dynamic).  Both backends use them verbatim.
  static std::string static_set_name(const std::string &list_name, int family) {
    return std::string(family == AF_INET6 ? "kpbr6_" : "kpbr4_") + list_name;
  }

  static std::string dynamic_set_name(const std::string &list_name,
                                      int family) {
    return std::string(family == AF_INET6 ? "kpbr6d_" : "kpbr4d_") + list_name;
  }

  // Resolve a canonical logical set reference to its physical name.
  static std::string physical_set_name(const std::string &logical_name) {
    return logical_name;
  }

  // Create a named IP set for storing IP addresses and/or CIDR subnets.
  // set_name: unique name for the set
  // family: AF_INET or AF_INET6
  // timeout: TTL in seconds for entries (0 = no timeout)
  virtual void create_ipset(const std::string &set_name, int family,
                            uint32_t timeout = 0) = 0;

  // Create a batch loader visitor for streaming IP/CIDR entries into a set.
  // Returns a ListEntryVisitor that buffers entries for atomic application.
  // Caller must call finish() on the returned visitor after streaming is
  // complete.
  virtual std::unique_ptr<ListEntryVisitor>
  create_batch_loader(const std::string &set_name) = 0;

  // Compile and apply the complete canonical desired state atomically (where
  // supported by the backend).  prepare_apply() remains separate because the
  // runtime needs attempt-specific set names before loading list contents.
  virtual void apply(const FirewallPlan &plan,
                     FirewallApplyMode mode = FirewallApplyMode::Destructive) = 0;

  void set_ipv6_enabled(bool enabled) { ipv6_enabled_ = enabled; }

  bool ipv6_enabled() const { return ipv6_enabled_; }

  void set_fwmark_mask(uint32_t fwmark_mask) { fwmark_mask_ = fwmark_mask; }

  uint32_t fwmark_mask() const { return fwmark_mask_; }

  // Marks allocated to this daemon instance. Backends that restore conntrack
  // marks can retain healthy established flows even when a classifier no
  // longer selects that child for new connections.
  virtual void set_owned_marks(const std::vector<uint32_t>& marks) {
    (void)marks;
  }

  void set_clear_dynamic_sets_on_apply(bool clear) {
    clear_dynamic_sets_on_apply_ = clear;
  }

  bool clear_dynamic_sets_on_apply() const {
    return clear_dynamic_sets_on_apply_;
  }

  // Optional ipset capacity hints. The iptables backend includes these in
  // hash:net declarations; nftables deliberately ignores them.
  void set_ipset_hashsize(std::optional<uint32_t> hashsize) {
    ipset_hashsize_ = std::move(hashsize);
  }

  const std::optional<uint32_t>& ipset_hashsize() const {
    return ipset_hashsize_;
  }

  void set_ipset_maxelem(std::optional<uint32_t> maxelem) {
    ipset_maxelem_ = std::move(maxelem);
  }

  const std::optional<uint32_t>& ipset_maxelem() const {
    return ipset_maxelem_;
  }

  // Remove all firewall rules and IP sets created by this instance.
  // Should be called on daemon shutdown.
  virtual void cleanup() = 0;

  // Return the backend type for this firewall instance.
    virtual FirewallBackend backend() const = 0;

    // Resolved per-family placement of forwarded traffic.  OUTPUT remains
    // mangle for both families.
    virtual RawPreroutingMode raw_prerouting_mode() const { return {}; }
    // Compatibility accessor: the historical flag means IPv4 RAW.
    virtual bool uses_raw_prerouting() const {
      return raw_prerouting_mode().ipv4;
    }

  // Everything keen-pbr expects in the kernel once `plan` has been applied by
  // this backend, as the backend realized it: the lowered owned chains
  // (raw/mangle placement, comment support, resolved set names,
  // owned marks) merged with expected_hook_rules().  Call after apply() while
  // the backend still holds the facts of that apply.  Rules carry the index of
  // the plan rule that produced them (PhysicalRule::plan_rule).
  virtual PhysicalRuleset expected_ruleset(const FirewallPlan &plan) const;

  // Builtin-chain hook jump rules as physical rules (iptables
  // PREROUTING/OUTPUT jumps into the KeenPbr chains).  Empty for backends whose
  // hooks are chain attributes carried by the lowered ruleset itself.
  virtual PhysicalRuleset expected_hook_rules() const;

  // Non-copyable
  Firewall(const Firewall &) = delete;
  Firewall &operator=(const Firewall &) = delete;

protected:
  Firewall() = default;

  uint32_t fwmark_mask_{0xFFFFFFFFu};
  bool ipv6_enabled_{true};
  bool clear_dynamic_sets_on_apply_{false};
  std::optional<uint32_t> ipset_hashsize_;
  std::optional<uint32_t> ipset_maxelem_;
};

// Return the stable config/CLI label for a concrete backend.
const char *firewall_backend_name(FirewallBackend backend);

// Factory function to create the appropriate firewall backend.
// backend_pref: auto-detect, iptables, or nftables.
// raw_prerouting: independent IPv4/IPv6 RAW PREROUTING placement.
// Throws FirewallError if requested backend is not available.
std::unique_ptr<Firewall>
create_firewall(FirewallBackendPreference backend_pref =
                    FirewallBackendPreference::auto_detect,
                RawPreroutingMode raw_prerouting = {});

// Compatibility overload for callers using the historical IPv4-only flag.
inline std::unique_ptr<Firewall>
create_firewall(FirewallBackendPreference backend_pref, bool use_raw_prerouting) {
  return create_firewall(backend_pref,
                         RawPreroutingMode{use_raw_prerouting, false});
}

} // namespace keen_pbr3
