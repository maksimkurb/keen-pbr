#pragma once

#include "firewall.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace keen_pbr3 {

// Keep IDs compatible with iptables/nftables comment arguments.  The shared
// limit uses the safe payload size common to the supported backends; the
// iptables xt_comment array reserves one byte for its terminator.
inline constexpr std::size_t kFirewallRuleIdMaxLength = 128;
inline constexpr std::size_t kFirewallRuleCommentMaxLength = 255;
inline constexpr std::string_view kFirewallRuleCommentPrefix = "kpbr:v1:";

struct FirewallRuleKey {
  std::string module_id;
  std::string instance_id;

  std::string comment() const;

  // Build a compact key from semantic input that may not fit in a comment.
  // The returned key is canonical and its comment is fully reversible.
  static FirewallRuleKey compact(std::string_view module_id,
                                 std::string_view semantic_instance);

  // Throws std::invalid_argument for an unknown version or malformed key.
  static FirewallRuleKey from_comment(std::string_view serialized);

  bool operator==(const FirewallRuleKey &other) const {
    return module_id == other.module_id && instance_id == other.instance_id;
  }
  bool operator!=(const FirewallRuleKey &other) const {
    return !(*this == other);
  }
};

// Canonical selector comparison owned by snapshot verification.  Other
// callers use it only when comparing already-canonical observed data.
bool firewall_rule_criteria_equal(const FirewallRuleCriteria& left,
                                  const FirewallRuleCriteria& right);
std::string normalize_firewall_set_name(const std::string& name);

struct MarkAction {
  uint32_t value{0};
  uint32_t mask{0xFFFFFFFFu};

  bool operator==(const MarkAction &other) const {
    return value == other.value && mask == other.mask;
  }
  bool operator!=(const MarkAction &other) const { return !(*this == other); }
};

struct BalanceAction {
  uint32_t fallback_mark{0};
  std::vector<FirewallBalanceCandidate> candidates;

  bool operator==(const BalanceAction &other) const {
    return fallback_mark == other.fallback_mark &&
           candidates == other.candidates;
  }
  bool operator!=(const BalanceAction &other) const { return !(*this == other); }
};

struct RestoreConntrackMarkAction {
  uint32_t mask{0};

  bool operator==(const RestoreConntrackMarkAction& other) const {
    return mask == other.mask;
  }
  bool operator!=(const RestoreConntrackMarkAction& other) const {
    return !(*this == other);
  }
};

struct SkipEstablishedOrDnatAction {
  bool operator==(const SkipEstablishedOrDnatAction&) const { return true; }
  bool operator!=(const SkipEstablishedOrDnatAction& other) const {
    return !(*this == other);
  }
};

struct SkipMarkedPacketsAction {
  bool operator==(const SkipMarkedPacketsAction&) const { return true; }
  bool operator!=(const SkipMarkedPacketsAction& other) const {
    return !(*this == other);
  }
};

struct InboundInterfaceFilterAction {
  std::vector<std::string> interfaces;

  bool operator==(const InboundInterfaceFilterAction& other) const {
    return interfaces == other.interfaces;
  }
  bool operator!=(const InboundInterfaceFilterAction& other) const {
    return !(*this == other);
  }
};

enum class VerdictAction : uint8_t { drop, pass };

using FirewallRuleAction = std::variant<MarkAction, BalanceAction, VerdictAction,
                                        RestoreConntrackMarkAction,
                                        SkipEstablishedOrDnatAction,
                                        SkipMarkedPacketsAction,
                                        InboundInterfaceFilterAction>;

enum class FirewallRuleStage : uint16_t {
  restore_conntrack = 100,
  global_bypass = 200,
  dns_detour = 300,
  route_classification = 400,
  terminal = 500,
};

enum class FirewallHook : uint8_t { prerouting, output };
// `any` means the canonical rule applies to both families; it is not an
// inferred IPv4 family and is expanded physically by the selected backend.
enum class FirewallFamily : uint8_t { ipv4, ipv6, any };

struct FirewallRuleInstance {
  FirewallRuleKey key;
  FirewallRuleStage stage{FirewallRuleStage::route_classification};
  int priority{0};
  std::size_t insertion_order{0};
  FirewallHook hook{FirewallHook::prerouting};
  FirewallFamily family{FirewallFamily::any};
  FirewallRuleCriteria criteria;
  FirewallRuleAction action;
  // Set preparation still uses the historical per-route order until it becomes
  // part of the planned resource lifecycle.
  std::size_t source_rule_index{std::numeric_limits<std::size_t>::max()};
};

} // namespace keen_pbr3
