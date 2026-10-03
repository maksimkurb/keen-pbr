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

// OUTPUT only: replies of local services to inbound connections (conntrack
// direction REPLY) are never classified by route rules.
struct SkipLocalRepliesAction {
  bool operator==(const SkipLocalRepliesAction&) const { return true; }
  bool operator!=(const SkipLocalRepliesAction& other) const {
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

// Hand the packet to a netfilter queue (NFQUEUE).  `bypass` accepts the packet
// when no userspace listener is bound.
struct QueueAction {
  uint16_t num{0};
  bool bypass{true};

  bool operator==(const QueueAction &other) const {
    return num == other.num && bypass == other.bypass;
  }
  bool operator!=(const QueueAction &other) const { return !(*this == other); }
};

// Copy the packet to an NFLOG group and keep evaluating the chain.
struct LogAction {
  uint16_t group{0};
  uint16_t snaplen{0};

  bool operator==(const LogAction &other) const {
    return group == other.group && snaplen == other.snaplen;
  }
  bool operator!=(const LogAction &other) const { return !(*this == other); }
};

using FirewallRuleAction = std::variant<MarkAction, BalanceAction, VerdictAction,
                                        RestoreConntrackMarkAction,
                                        SkipEstablishedOrDnatAction,
                                        SkipLocalRepliesAction,
                                        SkipMarkedPacketsAction,
                                        InboundInterfaceFilterAction,
                                        QueueAction, LogAction>;

enum class FirewallRuleStage : uint16_t {
  restore_conntrack = 100,
  global_bypass = 200,
  dns_detour = 300,
  route_classification = 400,
  terminal = 500,
  // Observation-only rules (DNS hold, L7 sniff) in their own chains.
  interception = 600,
};

// prerouting/output carry classification rules.  forward/postrouting exist for
// interception rules, which live in their own chains: QueueAction at
// postrouting, LogAction at forward or output.
enum class FirewallHook : uint8_t { prerouting, output, forward, postrouting };
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
  // Identifies the route rule that caused this planned action, when applicable.
  std::size_t source_rule_index{std::numeric_limits<std::size_t>::max()};
};

} // namespace keen_pbr3
