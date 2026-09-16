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
struct FirewallRuleKey;
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
  bool apply_output = false;    // classify locally generated packets instead of prerouting
  DefaultGatewayFamily default_gateway = DefaultGatewayFamily::None;
  std::vector<std::string> default_gateway_bypass;
  bool empty() const {
    return !dst_set_name.has_value() && !dscp.has_value() &&
           proto == L4Proto::Any && src_port.empty() && dst_port.empty() &&
           src_addr.empty() && dst_addr.empty() && !apply_output &&
           default_gateway == DefaultGatewayFamily::None;
  }

  bool has_rule_selector() const {
    return dst_set_name.has_value() || dscp.has_value() || !src_port.empty() ||
           !dst_port.empty() || !src_addr.empty() || !dst_addr.empty() ||
           default_gateway != DefaultGatewayFamily::None;
  }
};

using ProtoPortFilter = FirewallRuleCriteria;

// Backend compatibility state populated by canonical prefilter actions.
// This is not part of FirewallPlan or the public Firewall API.
struct FirewallPrefilter {
  std::optional<std::vector<std::string>> inbound_interfaces;
  bool skip_established_or_dnat{false};
  bool skip_marked_packets{false};
  // Restore only the daemon-owned portion of ctmark for original-direction
  // packets before normal classification. Disabled by default for builder
  // callers; runtime enables it with the configured fwmark mask.
  bool restore_conntrack_mark{false};
  uint32_t conntrack_mark_mask{0};
  // The compatibility adapters preserve canonical ownership while the
  // backend still expands these operations into physical rules.
  std::string restore_conntrack_mark_comment;
  std::string skip_established_or_dnat_comment;
  std::string skip_marked_packets_comment;
  std::string inbound_interface_filter_comment;
  bool comments_ipv4_supported{true};
  bool comments_ipv6_supported{true};

  bool has_inbound_interfaces() const {
    return inbound_interfaces.has_value() && !inbound_interfaces->empty();
  }

  bool empty() const {
    return !restore_conntrack_mark && !skip_established_or_dnat &&
           !skip_marked_packets && !has_inbound_interfaces();
  }

  bool comments_supported(bool ipv6) const {
    return ipv6 ? comments_ipv6_supported : comments_ipv4_supported;
  }
};

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

enum class FirewallSetGeneration : uint8_t { A, B };

// Return the kernel-normalized initial hashsize for an ipset declaration.
// The result is absent when the normalized value cannot be represented by
// ipset's uint32 command representation.
std::optional<uint32_t> normalize_ipset_hashsize(uint32_t requested);

// Abstract firewall interface for managing IP sets and packet marking rules.
// Both iptables and nftables backends implement this interface.
//
// Usage pattern (transactional rebuild):
//   create_ipset() / create_mark_rule() / create_drop_rule() /
//   create_pass_rule() — buffer operations create_batch_loader() → stream
//   entries → finish() apply()    — atomically commit everything using the
//   requested apply mode
class Firewall {
public:
  virtual ~Firewall() = default;

  // Start a new buffered apply attempt. Backends use this to select any
  // attempt-scoped physical names before rules and set contents are queued.
  virtual void prepare_apply(FirewallApplyMode mode) { (void)mode; }

  virtual std::string static_set_name(const std::string &list_name,
                                      int family) const {
    return std::string(family == AF_INET6 ? "kpbr6_" : "kpbr4_") + list_name;
  }

  // Return every physical static-set name that may represent this logical
  // list for the backend. Most backends have one stable name; A/B backends
  // expose both generations so RulesOnly can reject stale realized state
  // instead of treating the list as empty.
  virtual std::vector<std::string>
  static_set_names(const std::string &list_name, int family) const {
    return {static_set_name(list_name, family)};
  }

  virtual std::string dynamic_set_name(const std::string &list_name,
                                       int family) const {
    return std::string(family == AF_INET6 ? "kpbr6d_" : "kpbr4d_") + list_name;
  }

  // Create a named IP set for storing IP addresses and/or CIDR subnets.
  // set_name: unique name for the set
  // family: AF_INET or AF_INET6
  // timeout: TTL in seconds for entries (0 = no timeout)
  virtual void create_ipset(const std::string &set_name, int family,
                            uint32_t timeout = 0) = 0;

  // Create a firewall rule that marks packets matching the given criteria
  // with the specified firewall mark (fwmark).
  // fwmark: mark value to apply to matching packets
  // criteria: optional match criteria (default = any packet)
  virtual void create_mark_rule(uint32_t fwmark,
                                const FirewallRuleCriteria &criteria = {}) = 0;
  // Keyed compatibility entry point used by the canonical plan adapter.
  // Legacy callers may continue using the unkeyed API.
  virtual void create_mark_rule(const FirewallRuleKey &key, uint32_t fwmark,
                                const FirewallRuleCriteria &criteria = {}) {
    (void)key;
    create_mark_rule(fwmark, criteria);
  }
  virtual void create_balance_rule(
      uint32_t fallback_fwmark,
      const std::vector<FirewallBalanceCandidate>& candidates,
      const FirewallRuleCriteria& criteria = {}) {
    (void)fallback_fwmark;
    (void)candidates;
    (void)criteria;
    throw FirewallError("connection balancing requires the nftables firewall backend");
  }
  virtual void create_balance_rule(
      const FirewallRuleKey &key, uint32_t fallback_fwmark,
      const std::vector<FirewallBalanceCandidate> &candidates,
      const FirewallRuleCriteria &criteria = {}) {
    (void)key;
    create_balance_rule(fallback_fwmark, candidates, criteria);
  }

  // Create a firewall rule that drops packets matching the given criteria.
  // Used for blackhole outbounds that don't need routing tables or fwmarks.
  virtual void create_drop_rule(const FirewallRuleCriteria &criteria = {}) = 0;
  virtual void create_drop_rule(const FirewallRuleKey &key,
                                const FirewallRuleCriteria &criteria = {}) {
    (void)key;
    create_drop_rule(criteria);
  }

  // Create a firewall rule that stops keen-pbr processing for matching packets
  // and leaves them unmodified for normal system routing.
  virtual void create_pass_rule(const FirewallRuleCriteria &criteria = {}) = 0;
  virtual void create_pass_rule(const FirewallRuleKey &key,
                                const FirewallRuleCriteria &criteria = {}) {
    (void)key;
    create_pass_rule(criteria);
  }

  virtual void create_restore_conntrack_mark_rule(const FirewallRuleKey&,
                                                   uint32_t) {
  }
  virtual void create_skip_established_or_dnat_rule(const FirewallRuleKey&) {
  }
  virtual void create_skip_marked_packets_rule(const FirewallRuleKey&) {
  }
  virtual void create_inbound_interface_filter_rule(
      const FirewallRuleKey&, const std::vector<std::string>&) {
  }

  // Create a batch loader visitor for streaming IP/CIDR entries into a set.
  // Returns a ListEntryVisitor that buffers entries for atomic application.
  // Caller must call finish() on the returned visitor after streaming is
  // complete.
  virtual std::unique_ptr<ListEntryVisitor>
  create_batch_loader(const std::string &set_name) = 0;

  // Apply all pending changes atomically (where supported by the backend).
  virtual void
  apply(FirewallApplyMode mode = FirewallApplyMode::Destructive) = 0;

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

  // Non-copyable
  Firewall(const Firewall &) = delete;
  Firewall &operator=(const Firewall &) = delete;

protected:
  Firewall() = default;

  uint32_t fwmark_mask_{0xFFFFFFFFu};
  bool ipv6_enabled_{true};
  bool clear_dynamic_sets_on_apply_{true};
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
