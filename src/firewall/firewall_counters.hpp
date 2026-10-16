#pragma once

#include "../config/config.hpp"
#include "firewall.hpp"
#include "firewall_plan.hpp"
#include "firewall_verifier.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace keen_pbr3 {

// Read-only packet counters of keen-pbr's own iptables rules.  Nothing here
// adds or changes a rule: counters are parsed from `iptables-save -c` and the
// rules are recognised by their `kpbr:v1:` comments.

// One rule line of `iptables-save -c`.
struct IptablesCounterRule {
  std::string table;
  std::string chain;
  uint64_t packets{0};
  uint64_t bytes{0};
  std::string comment;  // "" when the rule has no comment
  std::string target;   // -j value ("" for a plain match)
  // MARK target only (--set-xmark / --set-mark): the value that is set.
  std::optional<uint32_t> mark_value;
};

// Parses `iptables-save -c` / `ip6tables-save -c` output.  Chain declarations,
// COMMIT and unparsable lines are skipped; foreign rules are returned too.
std::vector<IptablesCounterRule>
parse_iptables_save_counters(std::string_view text);

// What the counters of the live rules mean, derived from the applied plan.
struct FirewallCounterIndex {
  // Rule comment of a balance classifier -> tag of the balance outbound.  The
  // candidate rules of a cascade carry the comment of the balance rule only;
  // the candidate itself is told apart by the mark the rule sets.
  std::map<std::string, std::string> balance_comment_outbound;
  // Mark value -> outbound tag (candidate names).
  std::map<uint32_t, std::string> mark_outbound;
};

FirewallCounterIndex build_firewall_counter_index(const FirewallPlan& plan,
                                                  const OutboundMarkMap& marks);

struct FirewallCounters {
  // New connections the statistic cascade assigned to one candidate.  Counters
  // restart from zero whenever the rules are rebuilt by an apply.
  struct Classification {
    std::string outbound;   // balance outbound
    std::string candidate;  // candidate outbound (mark_0x<hex> when unknown)
    std::string family;     // "ipv4" / "ipv6"
    uint64_t connections{0};
  };
  std::vector<Classification> balance_classifications;
  // Packets accepted early by daemon.skip_marked_packets, by family.  A family
  // is present only when its rule exists and was read.
  std::map<std::string, uint64_t> skip_marked_packets;
  bool empty() const {
    return balance_classifications.empty() && skip_marked_packets.empty();
  }
};

// Folds the parsed rules of one family into `into`.
void aggregate_firewall_counters(const std::vector<IptablesCounterRule>& rules,
                                 const std::string& family,
                                 const FirewallCounterIndex& index,
                                 FirewallCounters& into);

// Runs `<iptables|ip6tables>-save -c -t mangle` (and raw when keen-pbr uses
// raw PREROUTING for that family).  A family that cannot be read is omitted.
FirewallCounters collect_iptables_counters(const CommandRunner& runner,
                                           const FirewallCounterIndex& index,
                                           bool ipv6_enabled,
                                           RawPreroutingMode raw_prerouting);

// Briefly cached counters, so frequent scrapes do not fork iptables-save each
// time.  Thread-safe.  `generation` identifies the applied ruleset: a different
// one (a new apply reset the counters) bypasses the cache.
class FirewallCounterCache {
public:
  using Clock = std::chrono::steady_clock;
  explicit FirewallCounterCache(std::chrono::seconds lifetime = std::chrono::seconds{5})
      : lifetime_(lifetime) {}
  FirewallCounters get(const void* generation,
                       const std::function<FirewallCounters()>& read);

private:
  std::chrono::seconds lifetime_;
  std::mutex mutex_;
  const void* generation_{nullptr};
  Clock::time_point read_at_{};
  bool valid_{false};
  FirewallCounters counters_;
};

} // namespace keen_pbr3
