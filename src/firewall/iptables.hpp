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
  std::string static_set_name(const std::string &list_name,
                              int family) const override;
  std::vector<std::string>
  static_set_names(const std::string &list_name, int family) const override;

  // Buffer an ipset create command (hash:net family, optional timeout).
  void create_ipset(const std::string &set_name, int family,
                    uint32_t timeout = 0) override;

  // Return an IpsetRestoreVisitor that appends 'add' lines to the pending
  // element buffer for set_name; entries are flushed during apply().
  std::unique_ptr<ListEntryVisitor>
  create_batch_loader(const std::string &set_name) override;

  // Populate the inactive A/B static-set generation, then atomically rebuild
  // and retarget the stable PREROUTING and OUTPUT dispatchers.
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
  // Dispatcher and builtin-chain hook jumps of the active generation.
  PhysicalRuleset expected_hook_rules() const override;

  // Test/fixture-only seam: bypass the /proc capability probes (xt_comment
  // registration, raw table registration), which are absent on nft-backed
  // iptables.  Production code never calls this; probing is unchanged by
  // default.
  void override_capabilities_for_fixtures(
      std::optional<bool> comments_supported,
      std::optional<RawPreroutingMode> raw_prerouting) {
    comments_override_ = comments_supported;
    if (raw_prerouting.has_value()) {
      raw_prerouting_ = *raw_prerouting;
    }
  }

private:
  std::optional<bool> comments_override_;
  static constexpr const char *CHAIN_NAME = "KeenPbrTable";
  static constexpr const char *RAW_CHAIN_NAME = "KeenPbrRaw";
  static constexpr const char *OUTPUT_CHAIN_NAME = "KeenPbrOutput";
  void cleanup_live_impl(bool preserve_dynamic_sets = false,
                         bool sweep_live_state = false);
  void cleanup_impl();
  void cleanup_rules_impl(bool sweep_live_state = false);
  void cleanup_saved_sets(bool preserve_dynamic_sets);
  static void cleanup_legacy_generation_chains(const char *command);

  // Describes a set to be created via 'ipset restore'.
  struct PendingSet {
    std::string name;
    std::string family_str; // "inet" or "inet6"
    uint32_t timeout;       // entry TTL in seconds (0 = no timeout)
    std::optional<uint32_t> hashsize;
    std::optional<uint32_t> maxelem;
  };

  enum class LiveGenerationState { A, B, Missing, Invalid };

  struct DispatcherInspection {
    LiveGenerationState state{LiveGenerationState::Missing};
    bool references_a{false};
    bool references_b{false};
  };

  struct GenerationInspection {
    LiveGenerationState primary{LiveGenerationState::Missing};
    LiveGenerationState secondary{LiveGenerationState::Missing};
    bool primary_references_a{false};
    bool primary_references_b{false};
    bool secondary_references_a{false};
    bool secondary_references_b{false};
  };

  struct GenerationPlan {
    FirewallSetGeneration target{FirewallSetGeneration::A};
    bool repair_output{false};
  };

  struct StaticSetInspection {
    LiveGenerationState generation{LiveGenerationState::Missing};
    std::set<std::string> names;
  };

  void compile_plan(const FirewallPlan &plan, FirewallApplyMode mode);
  void apply_prepared(FirewallApplyMode mode);
  void clear_pending();

  // Build the 'create <name> hash:net family <f> [capacity] [timeout <t>]'
  // line. -exist remains the final token for ipset restore compatibility.
  static std::string build_ipset_create_line(const PendingSet &ps);
  static bool is_dynamic_set_name(const std::string &set_name);
  static bool dynamic_set_schema_compatible(const std::string &saved_sets,
                                            const PendingSet &expected);
  std::optional<std::string>
  find_incompatible_dynamic_set_schema(bool effective_ipv6) const;
  void preflight_dynamic_set_schemas(bool effective_ipv6) const;
  void preflight_reused_set_schemas(bool effective_ipv6) const;
  // Pure renderers of the lowered ruleset of one family into an
  // iptables-restore transaction.  They add only the stable dispatcher/chain
  // scaffolding; every rule line comes from the PhysicalRuleset.
  static std::string
  build_raw_prerouting_script(bool ipv6,
                              FirewallSetGeneration target_generation,
                              const PhysicalRuleset &rules);
  static std::string build_output_script(bool ipv6,
                                         FirewallSetGeneration target_generation,
                                         const PhysicalRuleset &rules);
  static std::string build_ipt_script(bool ipv6,
                                      FirewallSetGeneration target_generation,
                                      const PhysicalRuleset &rules);
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
  DispatcherInspection inspect_live_generation(bool ipv6) const;
  GenerationInspection inspect_generation(bool ipv6,
                                          bool allow_invalid = false) const;
  StaticSetInspection inspect_static_sets(
      bool ipv6, const GenerationInspection &inspection) const;
  static StaticSetInspection parse_static_set_references(
      const std::string &rules, bool ipv6);
  static GenerationPlan generation_plan_for_states(
      LiveGenerationState primary, LiveGenerationState secondary);
  static FirewallSetGeneration static_target_for_mode(
      FirewallApplyMode mode, LiveGenerationState live_static,
      FirewallSetGeneration rule_target);
  static void validate_target_generation(const GenerationPlan &plan,
                                         FirewallSetGeneration expected);
  DispatcherInspection inspect_dispatcher(
      const char *command, const char *table, const std::string &dispatcher,
      const std::string &generation_a,
      const std::string &generation_b) const;
  FirewallSetGeneration select_target_generation(bool ipv6,
                                                 bool repair_output) const;
  void ensure_target_generation_inactive(
      bool ipv6, FirewallSetGeneration target, bool repair_output) const;
  void publish_dispatcher(bool ipv6, bool output,
                          FirewallSetGeneration generation) const;
  static LiveGenerationState
  parse_live_generation(const std::string &rules, const std::string &dispatcher,
                        const std::string &generation_a,
                        const std::string &generation_b);
  static DispatcherInspection parse_live_generation_details(
      const std::string &rules, const std::string &dispatcher,
      const std::string &generation_a, const std::string &generation_b);
  static FirewallSetGeneration
  target_generation_for_states(LiveGenerationState primary,
                               LiveGenerationState secondary);
  void reconcile_hooks(bool ipv6) const;
  void verify_applied_generation(bool ipv6, FirewallSetGeneration target) const;
  static size_t count_exact_jump(const std::string &rules,
                                 const std::string &source_chain,
                                 const std::string &target_chain);
  static void reconcile_hook(const char *command, const char *table,
                             const char *builtin_chain,
                             const char *target_chain);
  static void remove_all_hooks(const char *command, const char *table,
                               const char *builtin_chain,
                               const char *target_chain);
  const char *prerouting_table_name(bool ipv6) const;
  bool uses_raw_prerouting(bool ipv6) const {
    return raw_prerouting_.uses(ipv6);
  }
  const char *prerouting_dispatcher_chain_name(bool ipv6) const;
  const char *prerouting_generation_chain(FirewallSetGeneration generation,
                                          bool ipv6) const;
  static const char *output_generation_chain(FirewallSetGeneration generation);
  // Sets queued for creation, flushed by apply().
  std::vector<PendingSet> pending_sets_;
  // Per-set element buffers for ipset restore lines, keyed by set name.
  std::map<std::string, std::ostringstream> pending_elements_;
  // Lowered rules of the owned generation chains, flushed by apply().
  PhysicalRuleset pending_ruleset_;

  // Track created ipsets: set_name -> family (AF_INET/AF_INET6)
  std::map<std::string, int> created_sets_;

  // Track whether chain + jump rule exist for each protocol
  bool chain_v4_created_ = false;
  bool chain_v6_created_ = false;
  static const char *generation_chain(FirewallSetGeneration generation);
  static std::string static_set_name_for_generation(
      const std::string &list_name, int family,
      FirewallSetGeneration generation);
  FirewallSetGeneration target_v4_generation_{FirewallSetGeneration::A};
  FirewallSetGeneration target_v6_generation_{FirewallSetGeneration::A};
  FirewallSetGeneration target_static_v4_generation_{FirewallSetGeneration::A};
  FirewallSetGeneration target_static_v6_generation_{FirewallSetGeneration::A};
  bool static_generations_prepared_{false};
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

// Factory function called from firewall.cpp
std::unique_ptr<Firewall>
create_iptables_firewall(RawPreroutingMode raw_prerouting = {});

} // namespace keen_pbr3
