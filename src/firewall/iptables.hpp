#pragma once

#include "firewall.hpp"
#include "firewall_lowering.hpp"
#include "firewall_physical.hpp"
#include "firewall_rule.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace keen_pbr3 {

class IptablesFirewall : public Firewall {
public:
  // Initialize the iptables backend; does not modify firewall state yet.
  explicit IptablesFirewall(RawPreroutingMode raw_prerouting = {});
  // Compatibility constructor: the historical bool selects IPv4 RAW only.
  explicit IptablesFirewall(bool use_raw_prerouting)
      : IptablesFirewall(RawPreroutingMode{use_raw_prerouting, false}) {}
  // Kernel firewall state is persistent and is removed only by explicit
  // cleanup(), never as a side effect of C++ object destruction.
  ~IptablesFirewall() override = default;

  void prepare_apply(FirewallApplyMode mode) override;

  // Buffer an ipset create command (hash:net family, optional timeout).
  void create_ipset(const std::string &set_name, int family,
                    uint32_t timeout = 0) override;

  // Return an IpsetRestoreVisitor that appends 'add' lines to the pending
  // element buffer for set_name; entries are flushed during apply().
  std::unique_ptr<ListEntryVisitor>
  create_batch_loader(const std::string &set_name) override;

  // Create or refresh the static sets (temp set + swap when the set already
  // exists), replace each table's KeenPbr chains (and retire legacy A/B
  // chains) with one iptables-restore transaction per table and family, then
  // destroy owned sets the new rules no longer reference.
  void apply(const FirewallPlan &plan,
             FirewallApplyMode mode = FirewallApplyMode::Destructive) override;
  // Destroy all buffered ipsets (ipset destroy) and flush/delete the
  // KeenPbrTable chain from both iptables and ip6tables mangle tables.
  void cleanup() override;
  // Returns FirewallBackend::iptables.
  FirewallBackend backend() const override;
  RawPreroutingMode raw_prerouting_mode() const override {
    return raw_prerouting_;
  }
  bool uses_raw_prerouting() const override { return raw_prerouting_.ipv4; }
  // Builtin-chain hook jumps into the PREROUTING/OUTPUT classification chains.
  PhysicalRuleset expected_hook_rules() const override;
  PhysicalRuleset expected_ruleset(const FirewallPlan &plan) const override;

  // Test/fixture-only seam: bypass the /proc capability probes (xt_comment
  // registration, raw table registration), which are absent on nft-backed
  // iptables.  Production code never calls this; probing is unchanged by
  // default.
  // `ipv6_backend` likewise replaces the ip6tables availability probe.  The
  // comment override also applies at once, so expected_ruleset() can be
  // built without prepare_apply() (which inspects the live system).
  void override_capabilities_for_fixtures(
      std::optional<bool> comments_supported,
      std::optional<RawPreroutingMode> raw_prerouting,
      std::optional<bool> ipv6_backend = std::nullopt) {
    comments_override_ = comments_supported;
    if (comments_supported.has_value()) {
      comment_v4_supported_ = comment_v6_supported_ = *comments_supported;
    }
    if (raw_prerouting.has_value()) {
      raw_prerouting_ = *raw_prerouting;
    }
    ipv6_backend_override_ = ipv6_backend;
  }

private:
  std::optional<bool> comments_override_;
  std::optional<bool> ipv6_backend_override_;
  void cleanup_live_impl(bool preserve_dynamic_sets = false,
                         bool sweep_live_state = false);
  void cleanup_impl();
  void cleanup_rules_impl(bool sweep_live_state = false);
  void cleanup_saved_sets(bool preserve_dynamic_sets);
  static void cleanup_legacy_numbered_chains(const char *command);

  // Describes a set to be created via 'ipset restore'.
  struct PendingSet {
    std::string name;
    std::string family_str; // "inet" or "inet6"
    uint32_t timeout;       // entry TTL in seconds (0 = no timeout)
    std::optional<uint32_t> hashsize;
    std::optional<uint32_t> maxelem;
  };

  void compile_plan(const FirewallPlan &plan, FirewallApplyMode mode);
  void apply_prepared(FirewallApplyMode mode);
  void clear_pending();

  // Build the 'create <name> hash:net family <f> [capacity] [timeout <t>]'
  // line. -exist remains the final token for ipset restore compatibility.
  static std::string build_ipset_create_line(const PendingSet &ps);
  static bool is_dynamic_set_name(const std::string &set_name);
  // Strict grammar of the static-set names keen-pbr owns.  Stable:
  // kpbr4_<tag>/kpbr6_<tag>; Temp: kpbr4t_<tag>/kpbr6t_<tag> (refresh
  // staging); Legacy: kpbr4s_/kpbr4S_/kpbr6s_/kpbr6S_<tag> (retired A/B
  // generations).  <tag> is a list name (lowercase letters, digits, `_`,
  // starting with a letter, at most 24 characters).
  enum class OwnedSetKind { None, Stable, Temp, Legacy };
  static OwnedSetKind classify_owned_static_set(const std::string &name);
  // Staging name of a stable static set; nullopt for any other name.
  static std::optional<std::string> temp_set_name(const std::string &stable);
  // Rewrites the `add <from> ` prefix of every restore line to `add <to> `.
  static void append_retargeted_elements(std::string &out,
                                         const std::string &elements,
                                         const std::string &from,
                                         const std::string &to);
  // `ipset destroy`; reports instead of throwing.  True when the set is gone.
  static bool destroy_set_best_effort(const std::string &name);
  // Destroys owned static sets in `live` that the new rules no longer
  // reference (everything but the stable names in `keep`).
  void destroy_unreferenced_static_sets(const std::set<std::string> &live,
                                        const std::set<std::string> &keep);
  static bool dynamic_set_schema_compatible(const std::string &saved_sets,
                                            const PendingSet &expected);
  std::optional<std::string>
  find_incompatible_dynamic_set_schema(bool effective_ipv6) const;
  void preflight_dynamic_set_schemas(bool effective_ipv6) const;
  void preflight_reused_set_schemas(bool effective_ipv6) const;
  // One owned classification chain of a table and the builtin chain that
  // jumps into it.
  struct OwnedChainSpec {
    std::string name;
    const char *hook_chain; // "PREROUTING", "OUTPUT", "POSTROUTING", ...
    // Pinned hooks are inserted at position 1 (`-I <chain> 1`) and repaired
    // when they drift; the others are appended.
    bool pinned{false};
    // Second builtin chain that also jumps into this chain (KeenPbrSniff:
    // FORWARD and OUTPUT).
    const char *extra_hook_chain{nullptr};
  };
  // Pure renderer of one table of one family into an iptables-restore
  // transaction: declares (flushes) the owned chains, appends the lowered
  // rules, ensures exactly one builtin-chain hook per chain, and flushes and
  // deletes the retired A/B chains found in `observed_dump` (the output of
  // `iptables -t <table> -S`) in the same commit.  Every rule line comes from
  // the PhysicalRuleset.
  static std::string
  build_table_script(const char *table, FirewallFamily family,
                     const std::vector<OwnedChainSpec> &chains,
                     const PhysicalRuleset &rules,
                     const std::string &observed_dump);
  FirewallLoweringContext lowering_context(uint32_t fwmark_mask) const;
  bool probe_xt_comment(bool ipv6) const;
  // Probe a caller-supplied registration file before running the restore
  // grammar check.  The path parameter is an injectable seam for tests;
  // production always supplies the corresponding /proc/net file.
  bool probe_xt_comment_from_registration(
      bool ipv6, const std::string &registration_path) const;
  static bool has_xt_comment_registration(const std::string &contents);
  bool comments_supported_for_family(bool ipv6) const {
    return ipv6 ? comment_v6_supported_ : comment_v4_supported_;
  }
  bool ipv6_backend_available() const;
  void validate_raw_prerouting_capability(bool ipv6) const;
  // `iptables -t <table> -S`; throws FirewallError on failure.
  static std::string capture_table_dump(bool ipv6, const char *table);
  // The PREROUTING classification chain of the family exists live
  // (RulesOnly reuses the live rules).
  bool live_prerouting_chain_present(bool ipv6) const;
  // Names of the live ipsets (`ipset list -n`); nullopt when the listing
  // fails.
  static std::optional<std::set<std::string>> list_live_ipset_names();
  void verify_applied_hooks(bool ipv6) const;
  // The first `-A <source>` rule of the dump is exactly the plain jump.
  static bool first_rule_is_jump(const std::string &rules,
                                 const std::string &source_chain,
                                 const std::string &target_chain);
  static size_t count_exact_jump(const std::string &rules,
                                 const std::string &source_chain,
                                 const std::string &target_chain);
  static void remove_all_hooks(const char *command, const char *table,
                               const char *builtin_chain,
                               const char *target_chain);
  const char *prerouting_table_name(bool ipv6) const;
  bool uses_raw_prerouting(bool ipv6) const {
    return raw_prerouting_.uses(ipv6);
  }
  const char *prerouting_chain_name(bool ipv6) const;
  // Sets queued for creation, flushed by apply().
  std::vector<PendingSet> pending_sets_;
  // Per-set element buffers for ipset restore lines, keyed by set name.
  std::map<std::string, std::ostringstream> pending_elements_;
  // Lowered rules of the owned classification chains, flushed by apply().
  PhysicalRuleset pending_ruleset_;

  // Track created ipsets: set_name -> family (AF_INET/AF_INET6)
  std::map<std::string, int> created_sets_;

  // Track whether chain + jump rule exist for each protocol
  bool chain_v4_created_ = false;
  bool chain_v6_created_ = false;
  FirewallApplyMode prepared_mode_{FirewallApplyMode::Destructive};
  bool apply_prepared_{false};
  bool comment_v4_supported_{true};
  bool comment_v6_supported_{true};
  RawPreroutingMode raw_prerouting_{};

#ifdef KEEN_PBR3_TESTING
  friend class IptablesBuilderTest;
#endif
};

// Renders one canonical rule as an `iptables-restore` `-A <chain> ...` line
// (newline terminated).  Pure: all policy was decided by the lowering.  Throws
// FirewallError for rules iptables cannot express in one line.
std::string render_iptables_rule(const PhysicalRule &rule,
                                 const std::string &chain);

// xt_statistic (`-m statistic --mode random`), needed by iptables load
// balancing.  Kernel checks run once, at service start: the daemon calls
// probe_iptables_statistic() (a throw-away `iptables-restore --test`; the
// kernel may autoload the module during it, no modprobe of our own) and keeps
// the answer with record_iptables_statistic_capability().  Everything after
// that only reads the stored answer.  The match is registered for IPv4 and
// IPv6 by one module, so IPv4 is probed.
bool probe_iptables_statistic();
void record_iptables_statistic_capability(bool available);
void reset_iptables_statistic_capability_for_tests();

// Pre-mutation gate: throws FirewallError naming xt_statistic when
// `uses_balance` on the iptables backend and the startup probe found the
// match unusable (or never ran, which needs a restart).  Probes nothing.
void require_iptables_balance_support(FirewallBackend backend,
                                      bool uses_balance);

// Factory function called from firewall.cpp
std::unique_ptr<Firewall>
create_iptables_firewall(RawPreroutingMode raw_prerouting = {});

} // namespace keen_pbr3
