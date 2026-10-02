#include <doctest/doctest.h>

#include "../src/config/config.hpp"
#include "../src/config/routing_state.hpp"
#include "../src/firewall/ipset_restore_pipe.hpp"
#include "../src/firewall/iptables.hpp"
#include "firewall_fixtures.hpp"
#include "../src/firewall/firewall_lowering.hpp"
#include "../src/firewall/firewall_plan.hpp"
#include "../src/lists/list_entry_visitor.hpp"

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace keen_pbr3 {

namespace {

L4Proto parse_test_proto(const std::string &proto) {
  if (proto.empty())
    return L4Proto::Any;
  if (proto == "tcp")
    return L4Proto::Tcp;
  if (proto == "udp")
    return L4Proto::Udp;
  if (proto == "tcp/udp")
    return L4Proto::TcpUdp;
  throw std::invalid_argument("unexpected proto in test: " + proto);
}

class PathGuard {
public:
  PathGuard() : previous_(std::getenv("PATH") ? std::getenv("PATH") : "") {}
  ~PathGuard() { (void)setenv("PATH", previous_.c_str(), 1); }

  PathGuard(const PathGuard &) = delete;
  PathGuard &operator=(const PathGuard &) = delete;

private:
  std::string previous_;
};

void write_executable(const std::filesystem::path &path,
                      const std::string &contents) {
  std::ofstream output(path);
  REQUIRE(output.good());
  output << contents;
  output.close();
  REQUIRE(chmod(path.c_str(), 0755) == 0);
}

} // namespace

// Friend class with test access to IptablesFirewall private methods.
class IptablesBuilderTest {
public:
  static FirewallPlan mark_plan(uint32_t fwmark, FirewallRuleCriteria criteria,
                                FirewallRuleKey key = {"test", "rule"}) {
    FirewallPlan plan;
    FirewallRuleInstance rule;
    rule.key = std::move(key);
    rule.criteria = std::move(criteria);
    rule.action = MarkAction{fwmark, 0xFFFFFFFFu};
    plan.rules.push_back(std::move(rule));
    return plan;
  }

  // Public mirror of PendingRule for use in test functions.
  struct RuleDesc {
    std::string set_name;
    bool ipv6;
    bool direct = false;
    enum Action { Mark, Drop, Pass } action;
    uint32_t fwmark;
    ProtoPortFilter filter;
    FirewallRuleKey key;
  };

  static std::string build_ipset_create_line(const std::string &name,
                                             const std::string &family_str,
                                             uint32_t timeout,
                                             std::optional<uint32_t> hashsize =
                                                 std::nullopt,
                                             std::optional<uint32_t> maxelem =
                                                 std::nullopt) {
    IptablesFirewall::PendingSet ps;
    ps.name = name;
    ps.family_str = family_str;
    ps.timeout = timeout;
    ps.hashsize = hashsize;
    ps.maxelem = maxelem;
    return IptablesFirewall::build_ipset_create_line(ps);
  }

  static std::optional<uint32_t> normalize_ipset_hashsize(uint32_t requested) {
    return keen_pbr3::normalize_ipset_hashsize(requested);
  }

  static std::string create_ipset_line_from_config(
      const std::string &name, int family, uint32_t timeout,
      std::optional<uint32_t> hashsize, std::optional<uint32_t> maxelem) {
    IptablesFirewall firewall;
    firewall.set_ipset_hashsize(hashsize);
    firewall.set_ipset_maxelem(maxelem);
    firewall.create_ipset(name, family, timeout);
    REQUIRE(firewall.pending_sets_.size() == 1);
    return IptablesFirewall::build_ipset_create_line(
        firewall.pending_sets_.front());
  }

  static bool is_dynamic_set_name(const std::string &name) {
    return IptablesFirewall::is_dynamic_set_name(name);
  }

  static bool dynamic_set_schema_compatible(const std::string &saved,
                                            const std::string &name,
                                            const std::string &family,
                                            uint32_t timeout,
                                            std::optional<uint32_t> hashsize =
                                                std::nullopt,
                                            std::optional<uint32_t> maxelem =
                                                std::nullopt) {
    IptablesFirewall::PendingSet set{name, family, timeout, hashsize, maxelem};
    return IptablesFirewall::dynamic_set_schema_compatible(saved, set);
  }

  static std::string static_set_name(FirewallSetGeneration generation,
                                     const std::string &name, int family) {
    IptablesFirewall firewall;
    firewall.target_static_v4_generation_ = generation;
    firewall.target_static_v6_generation_ = generation;
    return firewall.static_set_name(name, family);
  }

  // The static-set name selected for a PreserveSets apply over `dump` (the
  // output of `iptables -t mangle -S`).  Throws like prepare_apply when the
  // live rules reference both generations.
  static std::string static_set_name_for_live_rules(const std::string &dump) {
    IptablesFirewall firewall;
    const auto live = IptablesFirewall::parse_static_set_references(
        dump, {"KeenPbrTable", "KeenPbrOutput", "KeenPbrTable_OUTPUT"}, false);
    if (live.generation == IptablesFirewall::LiveGenerationState::Invalid) {
      throw FirewallError("static ipsets from multiple generations");
    }
    firewall.target_static_v4_generation_ =
        IptablesFirewall::static_target_for_mode(FirewallApplyMode::PreserveSets,
                                                 live.generation);
    return firewall.static_set_name("sample", AF_INET);
  }

  static std::string prerouting_table(RawPreroutingMode mode, bool ipv6) {
    IptablesFirewall firewall;
    firewall.raw_prerouting_ = mode;
    return firewall.prerouting_table_name(ipv6);
  }

  static std::string prerouting_chain(RawPreroutingMode mode, bool ipv6) {
    IptablesFirewall firewall;
    firewall.raw_prerouting_ = mode;
    return firewall.prerouting_chain_name(ipv6);
  }

  static std::string cleanup_sweep_log(RawPreroutingMode mode) {
    const auto directory = std::filesystem::temp_directory_path() /
                           ("keen-pbr-raw-cleanup-" +
                            std::to_string(static_cast<long long>(getpid())));
    std::filesystem::create_directories(directory);
    const auto log_path = directory / "commands.log";
    const std::string script =
        "#!/bin/sh\nprintf '%s %s\\n' \"$0\" \"$*\" >> \"$KEEN_TEST_LOG\"\n";
    write_executable(directory / "iptables", script);
    write_executable(directory / "ip6tables", script);
    PathGuard path_guard;
    const auto path = directory.string() + ":/usr/bin:/bin";
    REQUIRE(setenv("PATH", path.c_str(), 1) == 0);
    REQUIRE(setenv("KEEN_TEST_LOG", log_path.c_str(), 1) == 0);
    IptablesFirewall firewall;
    firewall.raw_prerouting_ = mode;
    firewall.cleanup_rules_impl(true);
    unsetenv("KEEN_TEST_LOG");
    std::ifstream input(log_path);
    std::ostringstream contents;
    contents << input.rdbuf();
    std::filesystem::remove_all(directory);
    return contents.str();
  }

  // Legacy fixture vocabulary (rule descriptors + a prefilter struct) mapped
  // onto a FirewallPlan; the backend itself only sees the plan.
  static FirewallRuleKey key_from_comment(const std::string &comment) {
    return comment.empty() ? FirewallRuleKey{}
                           : FirewallRuleKey::from_comment(comment);
  }

  static FirewallPlan plan_from(const std::vector<RuleDesc> &descs,
                                const PrefilterFixture &prefilter,
                                uint32_t fwmark_mask = 0xFFFFFFFFu) {
    FirewallPlan plan;
    plan.fwmark_mask = fwmark_mask;
    const auto add = [&plan](FirewallRuleKey key, FirewallRuleAction action,
                             FirewallRuleStage stage) {
      FirewallRuleInstance rule;
      rule.key = std::move(key);
      rule.stage = stage;
      rule.family = FirewallFamily::any;
      rule.action = std::move(action);
      plan.rules.push_back(std::move(rule));
    };
    if (prefilter.restore_conntrack_mark) {
      add(key_from_comment(prefilter.restore_conntrack_mark_comment),
          RestoreConntrackMarkAction{prefilter.conntrack_mark_mask},
          FirewallRuleStage::restore_conntrack);
    }
    if (prefilter.skip_established_or_dnat) {
      add(key_from_comment(prefilter.skip_established_or_dnat_comment),
          SkipEstablishedOrDnatAction{}, FirewallRuleStage::global_bypass);
    }
    if (prefilter.skip_marked_packets) {
      add(key_from_comment(prefilter.skip_marked_packets_comment),
          SkipMarkedPacketsAction{}, FirewallRuleStage::global_bypass);
    }
    if (prefilter.has_inbound_interfaces()) {
      add(key_from_comment(prefilter.inbound_interface_filter_comment),
          InboundInterfaceFilterAction{*prefilter.inbound_interfaces},
          FirewallRuleStage::global_bypass);
    }
    for (const auto &d : descs) {
      FirewallRuleInstance rule;
      rule.key = d.key;
      rule.family = d.ipv6 ? FirewallFamily::ipv6 : FirewallFamily::ipv4;
      rule.criteria = d.filter;
      if (!d.set_name.empty()) {
        rule.criteria.dst_set_name = d.set_name;
      }
      rule.hook = FirewallHook::prerouting;
      if (d.action == RuleDesc::Mark) {
        rule.action = MarkAction{d.fwmark, fwmark_mask};
      } else {
        rule.action = d.action == RuleDesc::Drop ? VerdictAction::drop
                                                 : VerdictAction::pass;
      }
      plan.rules.push_back(std::move(rule));
    }
    return plan;
  }

  static PhysicalRuleset lower(const FirewallPlan &plan,
                               RawPreroutingMode raw = {},
                               bool comments = true) {
    FirewallLoweringContext context;
    context.backend = FirewallBackend::iptables;
    context.raw_prerouting = raw;
    context.comments_ipv4_supported = comments;
    context.comments_ipv6_supported = comments;
    context.fwmark_mask = plan.fwmark_mask;
    return lower_firewall_plan(plan, context);
  }

  using ChainSpec = IptablesFirewall::OwnedChainSpec;

  static std::string table_script(const char *table, bool ipv6,
                                  const std::vector<ChainSpec> &chains,
                                  const PhysicalRuleset &ruleset,
                                  const std::string &observed = {}) {
    return IptablesFirewall::build_table_script(
        table, ipv6 ? FirewallFamily::ipv6 : FirewallFamily::ipv4, chains,
        ruleset, observed);
  }

  // The PREROUTING chain (KeenPbrTable) of the mangle layout only.
  static std::string build_ipt_script(bool ipv6,
                                      const std::vector<RuleDesc> &descs,
                                      PrefilterFixture prefilter = {}) {
    const auto ruleset = lower(plan_from(descs, prefilter), {},
                               prefilter.comments_supported(ipv6));
    return table_script("mangle", ipv6, {{"KeenPbrTable", "PREROUTING"}},
                        ruleset);
  }

  // The whole mangle restore (PREROUTING + OUTPUT chains) over an observed
  // `iptables -t mangle -S` dump.
  static std::string
  build_mangle_script(bool ipv6, const std::vector<RuleDesc> &descs,
                      PrefilterFixture prefilter = {},
                      const std::string &observed = {}) {
    const auto ruleset = lower(plan_from(descs, prefilter), {},
                               prefilter.comments_supported(ipv6));
    return table_script(
        "mangle", ipv6,
        {{"KeenPbrTable", "PREROUTING"}, {"KeenPbrOutput", "OUTPUT"}}, ruleset,
        observed);
  }

  static std::string build_raw_script(const std::vector<RuleDesc> &descs,
                                      PrefilterFixture prefilter = {}) {
    return build_raw_script_for_family(false, descs, prefilter);
  }

  static std::string
  build_raw_script_for_family(bool ipv6, const std::vector<RuleDesc> &descs,
                              PrefilterFixture prefilter = {},
                              const std::string &observed = {}) {
    const auto ruleset = lower(plan_from(descs, prefilter),
                               RawPreroutingMode{true, true},
                               prefilter.comments_supported(ipv6));
    return table_script("raw", ipv6, {{"KeenPbrRaw", "PREROUTING"}}, ruleset,
                        observed);
  }

  static std::string build_output_script_for_family(
      bool ipv6, const std::vector<RuleDesc> &descs,
      PrefilterFixture prefilter = {}, const std::string &observed = {}) {
    const auto ruleset = lower(plan_from(descs, prefilter),
                               RawPreroutingMode{true, true},
                               prefilter.comments_supported(ipv6));
    return table_script("mangle", ipv6, {{"KeenPbrOutput", "OUTPUT"}}, ruleset,
                        observed);
  }

  static PhysicalRuleset expected_hooks(RawPreroutingMode mode) {
    IptablesFirewall firewall;
    firewall.raw_prerouting_ = mode;
    firewall.set_ipv6_enabled(false);
    return firewall.expected_hook_rules();
  }

  static int state_a() {
    return static_cast<int>(IptablesFirewall::LiveGenerationState::A);
  }
  static int state_b() {
    return static_cast<int>(IptablesFirewall::LiveGenerationState::B);
  }
  static int state_missing() {
    return static_cast<int>(IptablesFirewall::LiveGenerationState::Missing);
  }
  static int state_invalid() {
    return static_cast<int>(IptablesFirewall::LiveGenerationState::Invalid);
  }

  // Static-set references reachable from the owned chain `KeenPbrTable` of a
  // `-S` dump fragment (the chain declaration is added here).
  static IptablesFirewall::StaticSetInspection
  inspect_dump(const std::string &rules, bool ipv6,
               const std::vector<std::string> &roots) {
    return IptablesFirewall::parse_static_set_references(rules, roots, ipv6);
  }

  static int static_set_generation(const std::string &rules, bool ipv6 = false) {
    return static_cast<int>(
        inspect_dump("-N KeenPbrTable\n" + rules, ipv6, {"KeenPbrTable"})
            .generation);
  }

  static std::set<std::string> static_set_names(const std::string &rules,
                                                bool ipv6 = false) {
    return inspect_dump("-N KeenPbrTable\n" + rules, ipv6, {"KeenPbrTable"})
        .names;
  }

  static FirewallSetGeneration static_target_for_mode(FirewallApplyMode mode,
                                                      int live_static) {
    return IptablesFirewall::static_target_for_mode(
        mode,
        static_cast<IptablesFirewall::LiveGenerationState>(live_static));
  }

  static std::string static_name_for_generation(FirewallSetGeneration generation,
                                                const std::string &name,
                                                int family) {
    return IptablesFirewall::static_set_name_for_generation(name, family,
                                                             generation);
  }

  static std::string build_rules_for_set_slot(FirewallSetGeneration set_generation,
                                              bool ipv6) {
    IptablesFirewall firewall;
    firewall.target_static_v4_generation_ = set_generation;
    firewall.target_static_v6_generation_ = set_generation;
    RuleDesc rule{firewall.static_set_name("sample", ipv6 ? AF_INET6 : AF_INET),
                  ipv6, false, RuleDesc::Mark, 42, {}, {}};
    return build_ipt_script(ipv6, {rule});
  }

  static size_t count_exact_jump(const std::string &rules,
                                 const std::string &source,
                                 const std::string &target) {
    return IptablesFirewall::count_exact_jump(rules, source, target);
  }

  static bool has_xt_comment_registration(const std::string &contents) {
    return IptablesFirewall::has_xt_comment_registration(contents);
  }

  static bool probe_xt_comment_from_registration(
      const IptablesFirewall &firewall, bool ipv6,
      const std::string &registration_path) {
    return firewall.probe_xt_comment_from_registration(ipv6,
                                                        registration_path);
  }

  static std::string
  build_ipt_script_for_rule(bool ipv6, RuleDesc::Action action, uint32_t fwmark,
                            FirewallRuleCriteria criteria, bool list_backed,
                            uint32_t fwmark_mask = 0xFFFFFFFFu,
                            PrefilterFixture prefilter = {},
                            FirewallRuleKey key = {}) {
    RuleDesc desc;
    desc.ipv6 = ipv6;
    desc.action = action;
    desc.fwmark = fwmark;
    desc.filter = std::move(criteria);
    desc.key = std::move(key);
    if (list_backed) {
      desc.set_name = "pairwise_set";
    }
    const auto ruleset = lower(plan_from({desc}, prefilter, fwmark_mask), {},
                               prefilter.comments_supported(ipv6));
    return table_script("mangle", ipv6, {{"KeenPbrTable", "PREROUTING"}},
                        ruleset);
  }

  static std::string build_route_mark_script(const FirewallPlan& plan) {
    IptablesFirewall fw;
    fw.compile_plan(plan, FirewallApplyMode::Destructive);
    return table_script("mangle", false, {{"KeenPbrTable", "PREROUTING"}},
                        fw.pending_ruleset_);
  }

  // The `-p ... --sport/--dport ...` part of the single MARK rule that the
  // given selectors lower to.
  static std::string build_proto_port_fragment(const std::string &proto,
                                               const std::string &src_port,
                                               const std::string &dst_port,
                                               bool negate_src = false,
                                               bool negate_dst = false) {
    RuleDesc desc;
    desc.ipv6 = false;
    desc.action = RuleDesc::Mark;
    desc.fwmark = 1;
    desc.filter.proto = parse_test_proto(proto);
    desc.filter.src_port = PortSpec(src_port);
    desc.filter.dst_port = PortSpec(dst_port);
    desc.filter.negate_src_port = negate_src;
    desc.filter.negate_dst_port = negate_dst;
    const auto ruleset = lower(plan_from({desc}, {}));
    const auto &rules = ruleset.chains.front().rules;
    // MARK + RETURN per fragment, no conntrack save without a restore action.
    if (rules.size() != 2) {
      throw std::invalid_argument(
          "Port specification requires multiple iptables rules");
    }
    std::string line = render_iptables_rule(rules.front(), "");
    line = line.substr(3, line.find(" -j MARK") - 3);
    return line;
  }

  static size_t pending_set_count_after_duplicate_create() {
    IptablesFirewall firewall;
    firewall.create_ipset("kpbr4_shared", AF_INET);
    firewall.create_ipset("kpbr4_shared", AF_INET);
    return firewall.pending_sets_.size();
  }

  static bool conflicting_duplicate_create_throws() {
    IptablesFirewall firewall;
    firewall.create_ipset("kpbr4_shared", AF_INET);
    try {
      firewall.create_ipset("kpbr4_shared", AF_INET6);
    } catch (const FirewallError &) {
      return true;
    }
    return false;
  }
};

} // namespace keen_pbr3

using namespace keen_pbr3;
using T = IptablesBuilderTest;
using Rule = IptablesBuilderTest::RuleDesc;

namespace {

// The jump target name of the only rule of `chain` in the expected hooks.
std::string hook_target(const PhysicalRuleset &hooks, const std::string &name,
                        PhysicalTable table) {
  const auto *chain =
      hooks.find(iptables_physical_chain_id(name, table, FirewallFamily::ipv4));
  REQUIRE(chain != nullptr);
  REQUIRE(chain->rules.size() == 1);
  return std::get<JumpStmt>(chain->rules[0].statements.at(0)).target.name;
}

} // namespace

TEST_CASE("iptables expected hook rules point at the classification chains") {
  const auto mangle = T::expected_hooks(RawPreroutingMode{});
  CHECK(mangle.chains.size() == 2);
  CHECK(hook_target(mangle, "PREROUTING", PhysicalTable::mangle) ==
        "KeenPbrTable");
  CHECK(hook_target(mangle, "OUTPUT", PhysicalTable::mangle) ==
        "KeenPbrOutput");

  const auto raw = T::expected_hooks(RawPreroutingMode{true, false});
  CHECK(raw.chains.size() == 2);
  CHECK(hook_target(raw, "PREROUTING", PhysicalTable::raw) == "KeenPbrRaw");
  CHECK(hook_target(raw, "OUTPUT", PhysicalTable::mangle) == "KeenPbrOutput");
  // Identity matches what the iptables-save parser reports.
  CHECK(raw.find(iptables_physical_chain_id("PREROUTING", PhysicalTable::raw,
                                            FirewallFamily::ipv4))
            ->id.role == PhysicalChainRole::system_prerouting);
}

namespace {

PhysicalRuleset iptables_expected_ruleset(const FirewallPlan &plan,
                                          RawPreroutingMode raw, bool ipv6) {
  IptablesFirewall firewall;
  firewall.override_capabilities_for_fixtures(true, raw, ipv6);
  firewall.set_ipv6_enabled(true);
  firewall.set_fwmark_mask(plan.fwmark_mask);
  return firewall.expected_ruleset(plan);
}

PhysicalRuleset kernel_dump(const std::string &v4, const std::string &v6) {
  PhysicalRuleset result = parse_iptables_save(v4, FirewallFamily::ipv4);
  if (!v6.empty()) {
    append_physical_ruleset(result,
                            parse_iptables_save(v6, FirewallFamily::ipv6));
  }
  return result;
}

} // namespace

TEST_CASE("iptables expected ruleset equals the real kernel dump in both layouts") {
  // Lowered chains plus dispatcher/builtin hooks are exactly what iptables
  // reports after applying the capture plan (fixtures are real dumps).
  CHECK(iptables_expected_ruleset(capture_plan(false, false, true), {}, true) ==
        kernel_dump(read_fixture("iptables_mangle_v4.save"),
                    read_fixture("iptables_mangle_v6.save")));
  CHECK(iptables_expected_ruleset(capture_plan(false, true, true),
                                  RawPreroutingMode{true, true}, true) ==
        kernel_dump(read_fixture("iptables_raw_v4.save"),
                    read_fixture("iptables_raw_v6.save")));
}

TEST_CASE("iptables expected ruleset follows the active generation and IPv6 availability") {
  const auto plan = capture_plan(false, false, true);
  const auto without_v6 = iptables_expected_ruleset(plan, {}, false);
  CHECK(std::none_of(without_v6.chains.begin(), without_v6.chains.end(),
                     [](const PhysicalChain &chain) {
                       return chain.id.family == FirewallFamily::ipv6;
                     }));
  CHECK(without_v6 ==
        kernel_dump(read_fixture("iptables_mangle_v4.save"), ""));
}

TEST_CASE("IptablesFirewall deduplicates repeated static ipset declarations") {
  CHECK(T::pending_set_count_after_duplicate_create() == 1);
  CHECK(T::conflicting_duplicate_create_throws());
}

TEST_CASE("family layout helpers cover all four RAW/mangle combinations") {
  const std::array<RawPreroutingMode, 4> modes = {
      RawPreroutingMode{false, false}, RawPreroutingMode{true, false},
      RawPreroutingMode{false, true}, RawPreroutingMode{true, true}};
  for (const auto mode : modes) {
    CHECK(T::prerouting_table(mode, false) ==
          (mode.ipv4 ? "raw" : "mangle"));
    CHECK(T::prerouting_table(mode, true) ==
          (mode.ipv6 ? "raw" : "mangle"));
    CHECK(T::prerouting_chain(mode, false) ==
          (mode.ipv4 ? "KeenPbrRaw" : "KeenPbrTable"));
    CHECK(T::prerouting_chain(mode, true) ==
          (mode.ipv6 ? "KeenPbrRaw" : "KeenPbrTable"));
  }
}

TEST_CASE("live-state cleanup sweeps RAW and mangle layouts per family") {
  const auto log = T::cleanup_sweep_log(RawPreroutingMode{true, false});
  CHECK(log.find("-t raw -S PREROUTING") != std::string::npos);
  CHECK(log.find("-t raw -F KeenPbrRaw") != std::string::npos);
  CHECK(log.find("-t mangle -F KeenPbrTable") != std::string::npos);
  CHECK(log.find("-t mangle -F KeenPbrOutput") != std::string::npos);
  CHECK(log.find("ip6tables -t raw -S PREROUTING") != std::string::npos);
  CHECK(log.find("-t mangle -S PREROUTING") != std::string::npos);
  CHECK(log.find("ip6tables -t mangle -F KeenPbrTable") != std::string::npos);
}

TEST_CASE("IptablesFirewall cleanup propagates command failure") {
  const auto directory = std::filesystem::temp_directory_path() /
                         ("keen-pbr-iptables-cleanup-" +
                          std::to_string(static_cast<long long>(getpid())));
  std::filesystem::remove_all(directory);
  std::filesystem::create_directories(directory);
  write_executable(directory / "iptables",
                   "#!/bin/sh\n"
                   "echo 'permission denied' >&2\n"
                   "exit 7\n");

  {
    PathGuard path_guard;
    const auto old_path = std::getenv("PATH");
    const std::string path = directory.string() + ":" +
                             (old_path == nullptr ? std::string{} : old_path);
    REQUIRE(setenv("PATH", path.c_str(), 1) == 0);
    IptablesFirewall firewall;
    CHECK_THROWS_AS(firewall.cleanup(), FirewallError);
  }
  std::filesystem::remove_all(directory);
}

TEST_CASE("IPv6 RAW RulesOnly references select one static-set generation") {
  const auto rules =
      "-N KeenPbrRaw\n"
      "-A KeenPbrRaw -m set --match-set kpbr6s_remote dst -j RETURN\n";
  const auto live = T::inspect_dump(rules, true, {"KeenPbrRaw"});
  CHECK(static_cast<int>(live.generation) == T::state_a());
  CHECK(live.names == std::set<std::string>{"kpbr6s_remote"});
  CHECK(live.prerouting_chain_present);
}

TEST_CASE("static-set inspection follows a retired dispatcher to its active generation") {
  // A router upgraded from the A/B layout: only the chain the dispatcher
  // reaches counts, a stale sibling referencing the other generation does not.
  const std::string legacy =
      "-N KeenPbrRaw\n-N KeenPbrRaw_A\n-N KeenPbrRaw_B\n"
      "-A KeenPbrRaw -j KeenPbrRaw_B\n"
      "-A KeenPbrRaw_A -m set --match-set kpbr4s_stale dst -j RETURN\n"
      "-A KeenPbrRaw_B -m set --match-set kpbr4S_live dst -j RETURN\n";
  const auto live = T::inspect_dump(legacy, false, {"KeenPbrRaw"});
  CHECK(static_cast<int>(live.generation) == T::state_b());
  CHECK(live.names == std::set<std::string>{"kpbr4S_live"});
  // Unreachable-only references are not seen.
  CHECK(static_cast<int>(
            T::inspect_dump(legacy, false, {"KeenPbrOutput"}).generation) ==
        T::state_missing());
  // Both generations reachable is reported as invalid.
  CHECK(static_cast<int>(
            T::inspect_dump("-N KeenPbrRaw\n-N KeenPbrRaw_A\n"
                            "-A KeenPbrRaw -j KeenPbrRaw_A\n"
                            "-A KeenPbrRaw -m set --match-set kpbr4S_x dst -j RETURN\n"
                            "-A KeenPbrRaw_A -m set --match-set kpbr4s_y dst -j RETURN\n",
                            false, {"KeenPbrRaw"})
                .generation) == T::state_invalid());
}

TEST_CASE("raw prerouting rules use an isolated raw chain without conntrack") {
  Rule rule{"kpbr4s_minecraft", false, false, Rule::Mark, 0x100, {}};
  PrefilterFixture prefilter;
  prefilter.restore_conntrack_mark = true;
  prefilter.conntrack_mark_mask = 0xff00;
  prefilter.skip_established_or_dnat = true;
  const std::string script = T::build_raw_script({rule}, prefilter);
  CHECK(script.find("*raw\n") != std::string::npos);
  CHECK(script.find(":KeenPbrRaw - [0:0]") != std::string::npos);
  CHECK(script.find("KeenPbrRaw_") == std::string::npos);
  CHECK(script.find("-A PREROUTING -j KeenPbrRaw\n") != std::string::npos);
  CHECK(script.find("--set-xmark 0x100/0xffffffff") != std::string::npos);
  CHECK(script.find("CONNMARK") == std::string::npos);
  CHECK(script.find("-m conntrack") == std::string::npos);
  CHECK(script.find("-m connmark") == std::string::npos);
  CHECK(script.find("--ctstate") == std::string::npos);
  CHECK(script.find("--ctdir") == std::string::npos);
}

TEST_CASE("IPv6 raw prerouting filters families and keeps no-conntrack semantics") {
  Rule v4{"kpbr4s_v4", false, false, Rule::Mark, 0x100, {}};
  Rule v6{"kpbr6s_v6", true, false, Rule::Mark, 0x200, {}};
  PrefilterFixture prefilter;
  prefilter.restore_conntrack_mark = true;
  prefilter.conntrack_mark_mask = 0xff00;
  const auto script = T::build_raw_script_for_family(true, {v4, v6}, prefilter);
  CHECK(script.find("*raw\n") != std::string::npos);
  CHECK(script.find("kpbr6s_v6") != std::string::npos);
  CHECK(script.find("kpbr4s_v4") == std::string::npos);
  CHECK(script.find("--set-xmark 0x200/0xffffffff") != std::string::npos);
  CHECK(script.find("CONNMARK") == std::string::npos);
  CHECK(script.find("-m conntrack") == std::string::npos);
}

TEST_CASE("IPv6 raw mode keeps OUTPUT in mangle with connmark optimization") {
  Rule v4{"kpbr4s_v4", false, false, Rule::Mark, 0x100, {}};
  Rule v6{"kpbr6s_v6", true, false, Rule::Mark, 0x200, {}};
  PrefilterFixture prefilter;
  prefilter.restore_conntrack_mark = true;
  prefilter.conntrack_mark_mask = 0xff00;
  const auto script = T::build_output_script_for_family(true, {v4, v6}, prefilter);
  CHECK(script.find("*mangle\n") != std::string::npos);
  CHECK(script.find(":KeenPbrOutput - [0:0]") != std::string::npos);
  CHECK(script.find("kpbr6s_v6") != std::string::npos);
  CHECK(script.find("kpbr4s_v4") == std::string::npos);
  CHECK(script.find("CONNMARK") != std::string::npos);
  CHECK(script.find("--set-xmark 0x200/0xffffffff") != std::string::npos);
}

namespace {

Config parse_valid_config(const std::string &json) {
  Config cfg = parse_config(json);
  if (!cfg.dns.has_value()) {
    cfg.dns = DnsConfig{};
  }
  if (!cfg.dns->servers.has_value()) {
    DnsServer fallback_server;
    fallback_server.tag = "default_dns";
    fallback_server.address = "127.0.0.1";
    cfg.dns->servers = std::vector<DnsServer>{fallback_server};
  }
  if (!cfg.dns->fallback.has_value()) {
    cfg.dns->fallback = std::vector<std::string>{"default_dns"};
  }
  if (!cfg.dns->system_resolver.has_value()) {
    api::SystemResolver resolver;
    resolver.address = "127.0.0.1";
    cfg.dns->system_resolver = resolver;
  }
  validate_config(cfg);
  return cfg;
}

} // namespace

static Rule mark_rule(const std::string &set_name, bool ipv6, uint32_t fwmark,
                      ProtoPortFilter filter = {}) {
  Rule r;
  r.set_name = set_name;
  r.ipv6 = ipv6;
  r.action = Rule::Mark;
  r.fwmark = fwmark;
  r.filter = filter;
  return r;
}

static Rule drop_rule(const std::string &set_name, bool ipv6,
                      ProtoPortFilter filter = {}) {
  Rule r;
  r.set_name = set_name;
  r.ipv6 = ipv6;
  r.action = Rule::Drop;
  r.fwmark = 0;
  r.filter = filter;
  return r;
}

static Rule pass_rule(const std::string &set_name, bool ipv6,
                      ProtoPortFilter filter = {}) {
  Rule r;
  r.set_name = set_name;
  r.ipv6 = ipv6;
  r.action = Rule::Pass;
  r.fwmark = 0;
  r.filter = filter;
  return r;
}

static PrefilterFixture
prefilter_with_interfaces(std::vector<std::string> interfaces,
                          bool skip_established_or_dnat = true) {
  PrefilterFixture prefilter;
  prefilter.skip_established_or_dnat = skip_established_or_dnat;
  prefilter.skip_marked_packets = true;
  prefilter.inbound_interfaces = std::move(interfaces);
  return prefilter;
}

// =============================================================================
// IpsetRestoreVisitor::on_entry tests
// =============================================================================

TEST_CASE("IpsetRestoreVisitor: IP entry without timeout") {
  std::ostringstream buf;
  IpsetRestoreVisitor v(buf, "myset");
  v.on_entry(EntryType::Ip, "10.0.0.1");
  CHECK(buf.str() == "add myset 10.0.0.1 -exist\n");
  CHECK(v.count() == 1);
}

TEST_CASE("IpsetRestoreVisitor: CIDR entry") {
  std::ostringstream buf;
  IpsetRestoreVisitor v(buf, "myset");
  v.on_entry(EntryType::Cidr, "192.168.0.0/24");
  CHECK(buf.str() == "add myset 192.168.0.0/24 -exist\n");
  CHECK(v.count() == 1);
}

TEST_CASE("IpsetRestoreVisitor: IPv4 zero prefix expands for hash:net") {
  std::ostringstream buf;
  IpsetRestoreVisitor v(buf, "myset");
  v.on_entry(EntryType::Cidr, "0.0.0.0/0");
  CHECK(buf.str() == "add myset 0.0.0.0/1 -exist\n"
                     "add myset 128.0.0.0/1 -exist\n");
  CHECK(v.count() == 2);
}

TEST_CASE("IpsetRestoreVisitor: IPv6 zero prefix expands for hash:net") {
  std::ostringstream buf;
  IpsetRestoreVisitor v(buf, "myset");
  v.on_entry(EntryType::Cidr, "::/0");
  CHECK(buf.str() == "add myset ::/1 -exist\n"
                     "add myset 8000::/1 -exist\n");
  CHECK(v.count() == 2);
}

TEST_CASE("IpsetRestoreVisitor: Domain entry is ignored") {
  std::ostringstream buf;
  IpsetRestoreVisitor v(buf, "myset");
  v.on_entry(EntryType::Domain, "example.com");
  CHECK(buf.str().empty());
  CHECK(v.count() == 0);
}

TEST_CASE("IpsetRestoreVisitor: count increments only for IP/CIDR") {
  std::ostringstream buf;
  IpsetRestoreVisitor v(buf, "myset");
  v.on_entry(EntryType::Ip, "1.2.3.4");
  v.on_entry(EntryType::Domain, "example.com");
  v.on_entry(EntryType::Cidr, "10.0.0.0/8");
  CHECK(v.count() == 2);
}

// =============================================================================
// build_ipset_create_line tests
// =============================================================================

TEST_CASE("build_ipset_create_line: IPv4 without timeout") {
  auto line = T::build_ipset_create_line("myset", "inet", 0);
  CHECK(line == "create myset hash:net family inet -exist\n");
}

TEST_CASE("build_ipset_create_line: IPv4 with timeout 60") {
  auto line = T::build_ipset_create_line("myset", "inet", 60);
  CHECK(line == "create myset hash:net family inet timeout 60 -exist\n");
}

TEST_CASE("build_ipset_create_line: IPv6 without timeout") {
  auto line = T::build_ipset_create_line("myset", "inet6", 0);
  CHECK(line == "create myset hash:net family inet6 -exist\n");
}

TEST_CASE("build_ipset_create_line: capacity options precede timeout and -exist") {
  auto line = T::build_ipset_create_line("myset", "inet", 60, 2048, 65536);
  CHECK(line ==
        "create myset hash:net family inet hashsize 2048 maxelem 65536 "
        "timeout 60 -exist\n");
}

TEST_CASE("build_ipset_create_line: each capacity option is independently optional") {
  CHECK(T::build_ipset_create_line("myset", "inet", 0, 4096) ==
        "create myset hash:net family inet hashsize 4096 -exist\n");
  CHECK(T::build_ipset_create_line("myset", "inet", 0, std::nullopt, 131072) ==
        "create myset hash:net family inet maxelem 131072 -exist\n");
}

TEST_CASE("create_ipset: configured capacity options apply to static and dynamic sets") {
  CHECK(T::create_ipset_line_from_config("kpbr4s_static", AF_INET, 0, 2048,
                                         65536) ==
        "create kpbr4s_static hash:net family inet hashsize 2048 maxelem "
        "65536 -exist\n");
  CHECK(T::create_ipset_line_from_config("kpbr4d_dynamic", AF_INET, 300,
                                         2048, 65536) ==
        "create kpbr4d_dynamic hash:net family inet hashsize 2048 maxelem "
        "65536 timeout 300 -exist\n");
}

TEST_CASE("ipset reconcile: only dnsmasq names are dynamic") {
  CHECK(T::is_dynamic_set_name("kpbr4d_domains"));
  CHECK(T::is_dynamic_set_name("kpbr6d_domains"));
  CHECK_FALSE(T::is_dynamic_set_name("kpbr4_static"));
  CHECK_FALSE(T::is_dynamic_set_name("kpbr6_static"));
  CHECK_FALSE(T::is_dynamic_set_name("foreign_kpbr4d_domains"));
}

TEST_CASE("ipset reconcile: dynamic schema accepts terse ipset XML") {
  CHECK(T::dynamic_set_schema_compatible(
      R"(<?xml version="1.0" encoding="utf-8"?>
<ipsets>
  <ipset name="kpbr4d_domains">
    <type>hash:net</type>
    <revision>7</revision>
    <header>
      <family>inet</family>
      <hashsize>1024</hashsize>
      <maxelem>65536</maxelem>
      <timeout>300</timeout>
      <memsize>440</memsize>
      <references>1</references>
      <numentries>1000000</numentries>
    </header>
  </ipset>
</ipsets>)",
      "kpbr4d_domains", "inet", 300));
  CHECK(T::dynamic_set_schema_compatible(
      R"(<ipsets><ipset name="kpbr6d_domains"><type>hash:net</type><header><family>inet6</family><hashsize>1024</hashsize><maxelem>65536</maxelem></header></ipset></ipsets>)",
      "kpbr6d_domains", "inet6", 0));
}

TEST_CASE("ipset schema capacities: maxelem is exact and hashsize is grown") {
  const auto xml = [](uint32_t hashsize, uint32_t maxelem) {
    return "<ipsets><ipset name=\"kpbr4d_domains\"><type>hash:net</type>"
           "<header><family>inet</family><hashsize>" +
           std::to_string(hashsize) + "</hashsize><maxelem>" +
           std::to_string(maxelem) + "</maxelem></header></ipset></ipsets>";
  };

  CHECK(T::dynamic_set_schema_compatible(xml(128, 65536), "kpbr4d_domains",
                                         "inet", 0, 100, 65536));
  CHECK_FALSE(T::dynamic_set_schema_compatible(xml(128, 131072),
                                               "kpbr4d_domains", "inet", 0,
                                               100, 65536));
  CHECK(T::dynamic_set_schema_compatible(xml(256, 65536), "kpbr4d_domains",
                                         "inet", 0, 129, 65536));
  CHECK_FALSE(T::dynamic_set_schema_compatible(xml(128, 65536),
                                               "kpbr4d_domains", "inet", 0,
                                               129, 65536));
}

TEST_CASE("ipset schema capacities: missing and duplicate nodes are rejected") {
  CHECK_FALSE(T::dynamic_set_schema_compatible(
      R"(<ipsets><ipset name="kpbr4d_domains"><type>hash:net</type><header><family>inet</family><maxelem>65536</maxelem></header></ipset></ipsets>)",
      "kpbr4d_domains", "inet", 0));
  CHECK_FALSE(T::dynamic_set_schema_compatible(
      R"(<ipsets><ipset name="kpbr4d_domains"><type>hash:net</type><header><family>inet</family><hashsize>1024</hashsize><hashsize>2048</hashsize><maxelem>65536</maxelem></header></ipset></ipsets>)",
      "kpbr4d_domains", "inet", 0));
  CHECK_FALSE(T::dynamic_set_schema_compatible(
      R"(<ipsets><ipset name="kpbr4d_domains"><type>hash:net</type><header><family>inet</family><hashsize>1024</hashsize><maxelem>65536</maxelem><maxelem>131072</maxelem></header></ipset></ipsets>)",
      "kpbr4d_domains", "inet", 0));
}

TEST_CASE("ipset hashsize normalization uses minimum and power-of-two growth") {
  CHECK(*T::normalize_ipset_hashsize(1) == 64);
  CHECK(*T::normalize_ipset_hashsize(1024) == 1024);
  CHECK(*T::normalize_ipset_hashsize(1025) == 2048);
  CHECK(*T::normalize_ipset_hashsize(2147483648U) == 2147483648U);
  CHECK_FALSE(T::normalize_ipset_hashsize(4294967295U).has_value());
}

TEST_CASE("ipset reconcile: dynamic schema rejects incompatible live sets") {
  CHECK_FALSE(T::dynamic_set_schema_compatible(
      R"(<ipsets><ipset name="kpbr4d_domains"><type>hash:ip</type><header><family>inet</family><timeout>300</timeout></header></ipset></ipsets>)",
      "kpbr4d_domains", "inet", 300));
  CHECK_FALSE(T::dynamic_set_schema_compatible(
      R"(<ipsets><ipset name="kpbr4d_domains"><type>hash:net</type><header><family>inet6</family><timeout>300</timeout></header></ipset></ipsets>)",
      "kpbr4d_domains", "inet", 300));
  CHECK_FALSE(T::dynamic_set_schema_compatible(
      R"(<ipsets><ipset name="kpbr4d_domains"><type>hash:net</type><header><family>inet</family><timeout>60</timeout></header></ipset></ipsets>)",
      "kpbr4d_domains", "inet", 300));
}

TEST_CASE("ipset reconcile: dynamic schema rejects malformed or ambiguous XML") {
  CHECK_FALSE(T::dynamic_set_schema_compatible(
      R"(<ipsets><ipset name="kpbr4d_domains"><type>hash:net</type><header><family>inet</family></ipset></ipsets>)",
      "kpbr4d_domains", "inet", 0));
  CHECK_FALSE(T::dynamic_set_schema_compatible(
      R"(<ipsets><ipset name="kpbr4d_domains"><type>hash:net</type><type>hash:ip</type><header><family>inet</family></header></ipset></ipsets>)",
      "kpbr4d_domains", "inet", 0));
  CHECK_FALSE(T::dynamic_set_schema_compatible(
      R"(<ipsets><ipset name="kpbr4d_domains"><type>hash:net</type><header><family>inet</family><timeout>-1</timeout></header></ipset></ipsets>)",
      "kpbr4d_domains", "inet", 0));
  CHECK_FALSE(T::dynamic_set_schema_compatible(
      R"(<ipsets><ipset name="kpbr4d_domains"><type>hash:net</type><header><family>inet</family></header></ipset><ipset name="foreign"><type>hash:net</type><header><family>inet</family></header></ipset></ipsets>)",
      "kpbr4d_domains", "inet", 0));
}

TEST_CASE("RulesOnly schema helper accepts empty compatible static sets") {
  CHECK(T::dynamic_set_schema_compatible(
      R"(<ipsets><ipset name="kpbr4s_empty"><type>hash:net</type><header><family>inet</family><hashsize>1024</hashsize><maxelem>65536</maxelem></header></ipset></ipsets>)",
      "kpbr4s_empty", "inet", 0));
}

TEST_CASE("RulesOnly schema helper rejects incompatible static set schemas") {
  CHECK_FALSE(T::dynamic_set_schema_compatible(
      R"(<ipsets><ipset name="kpbr4s_empty"><type>hash:ip</type><header><family>inet</family></header></ipset></ipsets>)",
      "kpbr4s_empty", "inet", 0));
  CHECK_FALSE(T::dynamic_set_schema_compatible(
      R"(<ipsets><ipset name="kpbr4s_empty"><type>hash:net</type><header><family>inet6</family></header></ipset></ipsets>)",
      "kpbr4s_empty", "inet", 0));
  CHECK_FALSE(T::dynamic_set_schema_compatible(
      R"(<ipsets><ipset name="kpbr4s_empty"><type>hash:net</type><header><family>inet</family><timeout>300</timeout></header></ipset></ipsets>)",
      "kpbr4s_empty", "inet", 0));
}

TEST_CASE("ipset reconcile: static A/B names fit the ipset limit") {
  const std::string longest_name(24, 'a');
  const auto v4a =
      T::static_set_name(FirewallSetGeneration::A, longest_name, AF_INET);
  const auto v4b =
      T::static_set_name(FirewallSetGeneration::B, longest_name, AF_INET);
  const auto v6a =
      T::static_set_name(FirewallSetGeneration::A, longest_name, AF_INET6);
  const auto v6b =
      T::static_set_name(FirewallSetGeneration::B, longest_name, AF_INET6);
  CHECK(v4a == "kpbr4s_" + longest_name);
  CHECK(v4b == "kpbr4S_" + longest_name);
  CHECK(v6a == "kpbr6s_" + longest_name);
  CHECK(v6b == "kpbr6S_" + longest_name);
  CHECK(v4a.size() == 31);
  CHECK(v4b.size() == 31);
  CHECK(v6a.size() == 31);
  CHECK(v6b.size() == 31);
  CHECK(("kpbr4d_" + longest_name).size() == 31);
  CHECK(("kpbr6d_" + longest_name).size() == 31);
}

TEST_CASE("ipset reconcile: live rules select the inactive static slot") {
  CHECK(T::static_set_name_for_live_rules("") == "kpbr4s_sample");
  CHECK(T::static_set_name_for_live_rules(
            "-N KeenPbrTable\n-A KeenPbrTable -m set --match-set kpbr4s_x dst "
            "-j MARK\n") == "kpbr4S_sample");
  CHECK(T::static_set_name_for_live_rules(
            "-N KeenPbrTable\n-A KeenPbrTable -m set --match-set kpbr4S_x dst "
            "-j MARK\n") == "kpbr4s_sample");
  // Legacy dispatcher layout reached through KeenPbrTable_OUTPUT too.
  CHECK(T::static_set_name_for_live_rules(
            "-N KeenPbrTable_OUTPUT\n-N KeenPbrTable_B\n"
            "-A KeenPbrTable_OUTPUT -j KeenPbrTable_B\n"
            "-A KeenPbrTable -m set --match-set kpbr4S_x dst -j MARK\n") ==
        "kpbr4s_sample");
  CHECK_THROWS(T::static_set_name_for_live_rules(
      "-N KeenPbrTable\n"
      "-A KeenPbrTable -m set --match-set kpbr4s_x dst -j MARK\n"
      "-A KeenPbrTable -m set --match-set kpbr4S_y dst -j MARK\n"));
}

TEST_CASE("RulesOnly derives static slot from live match-set references") {
  CHECK(T::static_set_generation(
            "-A KeenPbrTable -m set --match-set kpbr4s_remote dst -j MARK\n") ==
        T::state_a());
  CHECK(T::static_set_generation(
            "-A KeenPbrTable -m set --match-set kpbr4S_remote dst -j MARK\n") ==
        T::state_b());
  CHECK(T::static_set_generation(
            "-A KeenPbrTable -m set --match-set kpbr4s_remote dst -j MARK\n"
            "-A KeenPbrTable -m set --match-set kpbr4s_other dst -j RETURN\n") ==
        T::state_a());
  CHECK(T::static_set_generation(
            "-A KeenPbrTable -m set --match-set kpbr4s_remote dst -j MARK\n"
            "-A KeenPbrTable -m set --match-set kpbr4S_other dst -j RETURN\n") ==
        T::state_invalid());
  CHECK(T::static_set_generation(
            "-A KeenPbrTable -m set --match-set kpbr4d_remote dst -j MARK\n") ==
        T::state_missing());
  CHECK(T::static_set_generation(
            "-A KeenPbrTable -m set --match-set kpbr6S_remote dst -j MARK\n",
            true) == T::state_b());
  CHECK(T::static_set_names(
            "-A KeenPbrTable -m set --match-set kpbr4S_remote dst -j MARK\n") ==
        std::set<std::string>{"kpbr4S_remote"});
}

TEST_CASE("static generation transition helper covers RulesOnly and refreshes") {
  CHECK(T::static_target_for_mode(FirewallApplyMode::RulesOnly, T::state_a()) ==
        FirewallSetGeneration::A);
  CHECK(T::static_target_for_mode(FirewallApplyMode::RulesOnly, T::state_b()) ==
        FirewallSetGeneration::B);
  CHECK(T::static_target_for_mode(FirewallApplyMode::RulesOnly,
                                  T::state_missing()) ==
        FirewallSetGeneration::A);
  CHECK(T::static_target_for_mode(FirewallApplyMode::PreserveSets,
                                  T::state_a()) == FirewallSetGeneration::B);
  CHECK(T::static_target_for_mode(FirewallApplyMode::PreserveSets,
                                  T::state_b()) == FirewallSetGeneration::A);
  CHECK(T::static_target_for_mode(FirewallApplyMode::StaticSetsOnly,
                                  T::state_a()) == FirewallSetGeneration::B);
  CHECK(T::static_target_for_mode(FirewallApplyMode::PreserveSets,
                                  T::state_missing()) ==
        FirewallSetGeneration::A);

  CHECK(T::static_name_for_generation(FirewallSetGeneration::A, "remote",
                                      AF_INET) == "kpbr4s_remote");
  CHECK(T::static_name_for_generation(FirewallSetGeneration::B, "remote",
                                      AF_INET) == "kpbr4S_remote");
  CHECK(T::static_name_for_generation(FirewallSetGeneration::A, "remote",
                                      AF_INET6) == "kpbr6s_remote");
  CHECK(T::static_name_for_generation(FirewallSetGeneration::B, "remote",
                                      AF_INET6) == "kpbr6S_remote");
}

namespace {

// Fake iptables for apply tests: `-S` of a table prints `table_dump`, the
// builtin chains print their single hook.  Every mutation through the
// iptables binary (not through iptables-restore) is appended to `mutation_log`.
std::string fake_iptables_script(const std::string &table_dump,
                                 const std::string &mutation_log) {
  return "#!/bin/sh\n"
         "mutation_log='" + mutation_log + "'\n"
         "for arg in \"$@\"; do\n"
         "  case \"$arg\" in\n"
         "    -A|-D|-I|-F|-X) /bin/printf '%s\\n' \"$*\" >> \"$mutation_log\" ;;\n"
         "  esac\n"
         "done\n"
         "last=''\n"
         "for arg in \"$@\"; do last=\"$arg\"; done\n"
         "case \"$last\" in\n"
         "  -S) /bin/printf '%s\\n' '" + table_dump + "' ;;\n"
         "  PREROUTING) /bin/printf '%s\\n' '-A PREROUTING -j KeenPbrTable' ;;\n"
         "  OUTPUT) /bin/printf '%s\\n' '-A OUTPUT -j KeenPbrOutput' ;;\n"
         "esac\n"
         "exit 0\n";
}

} // namespace

TEST_CASE("Destructive apply preserves compatible dynamic schemas") {
  struct ApplyResult {
    bool threw = false;
    std::string mutations;
  };

  const auto run_apply = [](const std::string &schema_xml,
                            int names_status = 0,
                            int schema_status = 0,
                            std::optional<uint32_t> maxelem = std::nullopt) {
    const auto sandbox = std::filesystem::temp_directory_path() /
                         ("keen-pbr-iptables-destructive-" +
                          std::to_string(static_cast<long long>(getpid())) +
                          "-" + std::to_string(names_status) + "-" +
                          std::to_string(schema_status) + "-" +
                          std::to_string(maxelem.value_or(0)));
    std::filesystem::remove_all(sandbox);
    std::filesystem::create_directories(sandbox);
    const auto mutation_log = sandbox / "ipset-mutations.log";

    const auto write_iptables = [](const std::filesystem::path &path) {
      write_executable(
          path,
          "#!/bin/sh\n"
          "last=''\n"
          "for arg in \"$@\"; do last=\"$arg\"; done\n"
          "if [ \"$last\" = \"PREROUTING\" ]; then\n"
          "  /bin/printf '%s\\n' '-A PREROUTING -j KeenPbrTable'\n"
          "elif [ \"$last\" = \"OUTPUT\" ]; then\n"
          "  /bin/printf '%s\\n' '-A OUTPUT -j KeenPbrOutput'\n"
          "fi\n"
          "exit 0\n");
    };
    write_iptables(sandbox / "iptables");
    write_iptables(sandbox / "ip6tables");
    write_executable(
        sandbox / "iptables-restore",
        "#!/bin/sh\n"
        "if [ \"$1\" = \"--test\" ]; then /bin/cat >/dev/null; exit 0; fi\n"
        "/bin/cat >/dev/null\n"
        "exit 0\n");
    write_executable(
        sandbox / "ipset",
        "#!/bin/sh\n"
        "mutation_log='" + mutation_log.string() + "'\n"
        "if [ \"$1\" = \"list\" ] && [ \"$2\" = \"-n\" ]; then\n"
        "  /bin/printf '%s\\n' kpbr4d_domains\n"
        "  exit " + std::to_string(names_status) + "\n"
        "fi\n"
        "if [ \"$1\" = \"list\" ] && [ \"$2\" = \"-t\" ]; then\n"
        "  /bin/printf '%s\\n' '" + schema_xml + "'\n"
        "  exit " + std::to_string(schema_status) + "\n"
        "fi\n"
        "if [ \"$1\" = \"save\" ]; then\n"
        "  /bin/printf '%s\\n' 'create kpbr4d_domains hash:net family inet hashsize 1024 maxelem 65536'\n"
        "  exit 0\n"
        "fi\n"
        "if [ \"$1\" = \"flush\" ] || [ \"$1\" = \"destroy\" ]; then\n"
        "  /bin/printf '%s\\n' \"$*\" >> \"$mutation_log\"\n"
        "  exit 0\n"
        "fi\n"
        "if [ \"$1\" = \"restore\" ]; then\n"
        "  /bin/cat >/dev/null\n"
        "  exit 0\n"
        "fi\n"
        "exit 0\n");

    PathGuard path_guard;
    const char *old_path = std::getenv("PATH");
    const std::string path = sandbox.string() + ":" +
                             (old_path == nullptr ? std::string{} : old_path);
    REQUIRE(setenv("PATH", path.c_str(), 1) == 0);

    IptablesFirewall firewall;
    firewall.set_ipv6_enabled(false);
    firewall.set_clear_dynamic_sets_on_apply(false);
    firewall.set_ipset_maxelem(maxelem);
    firewall.prepare_apply(FirewallApplyMode::Destructive);
    firewall.create_ipset("kpbr4d_domains", AF_INET);

    ApplyResult result;
    try {
      firewall.apply(FirewallPlan{}, FirewallApplyMode::Destructive);
    } catch (const FirewallError &) {
      result.threw = true;
    }
    std::ifstream input(mutation_log);
    if (input.good()) {
      std::ostringstream contents;
      contents << input.rdbuf();
      result.mutations = contents.str();
    }
    std::filesystem::remove_all(sandbox);
    return result;
  };

  const auto compatible_xml =
      R"(<ipsets><ipset name="kpbr4d_domains"><type>hash:net</type><header><family>inet</family><hashsize>1024</hashsize><maxelem>65536</maxelem></header></ipset></ipsets>)";
  const auto compatible = run_apply(compatible_xml);
  CHECK_FALSE(compatible.threw);
  CHECK(compatible.mutations.empty());

  const auto incompatible_xml =
      R"(<ipsets><ipset name="kpbr4d_domains"><type>hash:net</type><header><family>inet</family><hashsize>1024</hashsize><maxelem>65536</maxelem></header></ipset></ipsets>)";
  const auto incompatible = run_apply(incompatible_xml, 0, 0, 131072);
  CHECK_FALSE(incompatible.threw);
  CHECK(incompatible.mutations.find("flush kpbr4d_domains") !=
        std::string::npos);
  CHECK(incompatible.mutations.find("destroy kpbr4d_domains") !=
        std::string::npos);

  const auto inspection_failed = run_apply(compatible_xml, 1);
  CHECK(inspection_failed.threw);
  CHECK(inspection_failed.mutations.empty());
}

TEST_CASE("an invalid plan fails before any firewall mutation, a valid one commits once") {
  const auto sandbox = std::filesystem::temp_directory_path() /
                       ("keen-pbr-iptables-plan-validation-" +
                        std::to_string(static_cast<long long>(getpid())));
  std::filesystem::remove_all(sandbox);
  std::filesystem::create_directories(sandbox);
  const auto restore_log = sandbox / "restore.log";
  const auto mutation_log = sandbox / "mutations.log";

  write_executable(sandbox / "iptables",
                   fake_iptables_script("", mutation_log.string()));
  write_executable(
      sandbox / "iptables-restore",
      "#!/bin/sh\n"
      "if [ \"$1\" = \"--test\" ]; then /bin/cat >/dev/null; exit 0; fi\n"
      "/bin/printf '%s\\n' \"$*\" >> '" + restore_log.string() + "'\n"
      "/bin/cat >> '" + restore_log.string() + "'\n"
      "exit 0\n");
  write_executable(sandbox / "ipset", "#!/bin/sh\nexit 0\n");

  PathGuard path_guard;
  const char *old_path = std::getenv("PATH");
  const std::string path = sandbox.string() + ":" +
                           (old_path == nullptr ? std::string{} : old_path);
  REQUIRE(setenv("PATH", path.c_str(), 1) == 0);

  IptablesFirewall firewall;
  firewall.set_ipv6_enabled(false);
  firewall.prepare_apply(FirewallApplyMode::PreserveSets);

  FirewallPlan invalid;
  FirewallRuleInstance balance;
  balance.key = {"route.balance", "invalid"};
  balance.action = BalanceAction{1U, {{2U, true, true}}};
  invalid.rules.push_back(std::move(balance));
  CHECK_THROWS_AS(firewall.apply(invalid, FirewallApplyMode::PreserveSets),
                  FirewallError);

  CHECK_FALSE(std::filesystem::exists(restore_log));
  CHECK_FALSE(std::filesystem::exists(mutation_log));

  firewall.prepare_apply(FirewallApplyMode::PreserveSets);
  FirewallPlan valid;
  FirewallRuleInstance mark;
  mark.key = {"route.mark", "valid"};
  mark.action = MarkAction{42U};
  valid.rules.push_back(std::move(mark));
  CHECK_NOTHROW(firewall.apply(valid, FirewallApplyMode::PreserveSets));

  std::ifstream restores(restore_log);
  std::ostringstream restored;
  restored << restores.rdbuf();
  const std::string text = restored.str();
  // One transaction for the mangle table: both chains, both hooks, no A/B.
  CHECK(text.find("--noflush") != std::string::npos);
  CHECK(text.find("*mangle\n:KeenPbrTable - [0:0]\n:KeenPbrOutput - [0:0]\n") !=
        std::string::npos);
  CHECK(text.find("-A PREROUTING -j KeenPbrTable\n") != std::string::npos);
  CHECK(text.find("-A OUTPUT -j KeenPbrOutput\n") != std::string::npos);
  CHECK(text.find("_A") == std::string::npos);
  CHECK(text.find("_B") == std::string::npos);
  CHECK(text.find("*mangle", text.find("*mangle") + 1) == std::string::npos);
  CHECK_FALSE(std::filesystem::exists(mutation_log));
  std::filesystem::remove_all(sandbox);
}

TEST_CASE("RulesOnly reports a static-set generation change as typed and does not restore") {
  const auto sandbox = std::filesystem::temp_directory_path() /
                       ("keen-pbr-iptables-rules-only-" +
                        std::to_string(static_cast<long long>(getpid())));
  std::filesystem::remove_all(sandbox);
  std::filesystem::create_directories(sandbox);
  const auto count_file = sandbox / "iptables-count";
  const auto restore_log = sandbox / "restore.log";

  // The first table dump (prepare) references static A; every later dump
  // references static B, as if another process rebuilt the sets in between.
  write_executable(
      sandbox / "iptables",
      "#!/bin/sh\n"
      "count_file='" + count_file.string() + "'\n"
      "count=0\n"
      "if [ -f \"$count_file\" ]; then count=$(/bin/cat \"$count_file\"); fi\n"
      "last=''\n"
      "for arg in \"$@\"; do last=\"$arg\"; done\n"
      "if [ \"$last\" = \"-S\" ]; then\n"
      "  count=$((count + 1))\n"
      "  /bin/printf '%s\\n' \"$count\" > \"$count_file\"\n"
      "  slot=s\n"
      "  if [ \"$count\" -gt 1 ]; then slot=S; fi\n"
      "  /bin/printf '%s\\n' '-N KeenPbrTable' \"-A KeenPbrTable -m set --match-set kpbr4${slot}_remote dst -j MARK\"\n"
      "fi\n");
  write_executable(
      sandbox / "ipset",
      "#!/bin/sh\n"
      "if [ \"$1\" = \"list\" ] && [ \"$2\" = \"-n\" ]; then\n"
      "  /bin/printf '%s\\n' kpbr4s_remote\n"
      "  exit 0\n"
      "fi\n"
      "if [ \"$1\" = \"list\" ] && [ \"$2\" = \"-t\" ]; then\n"
      "  /bin/printf '%s\\n' '<ipsets><ipset name=\"kpbr4s_remote\"><type>hash:net</type><header><family>inet</family><hashsize>1024</hashsize><maxelem>65536</maxelem></header></ipset></ipsets>'\n"
      "  exit 0\n"
      "fi\n"
      "exit 1\n");
  write_executable(
      sandbox / "iptables-restore",
      "#!/bin/sh\n"
      "if [ \"$1\" = \"--test\" ]; then /bin/cat >/dev/null; exit 0; fi\n"
      "/bin/printf '%s\\n' invoked >> '" + restore_log.string() + "'\n"
      "exit 0\n");

  PathGuard path_guard;
  const char *old_path = std::getenv("PATH");
  const std::string path = sandbox.string() + ":" +
                           (old_path == nullptr ? std::string{} : old_path);
  REQUIRE(setenv("PATH", path.c_str(), 1) == 0);

  IptablesFirewall firewall;
  firewall.set_ipv6_enabled(false);
  firewall.prepare_apply(FirewallApplyMode::RulesOnly);
  firewall.create_ipset("kpbr4s_remote", AF_INET);
  FirewallRuleCriteria criteria;
  criteria.dst_set_name = "kpbr4s_remote";
  const auto plan = IptablesBuilderTest::mark_plan(1, criteria);

  CHECK_THROWS_AS(firewall.apply(plan, FirewallApplyMode::RulesOnly),
                  FirewallRulesOnlyError);
  CHECK_FALSE(std::filesystem::exists(restore_log));
  std::filesystem::remove_all(sandbox);
}

TEST_CASE("RulesOnly without a live PREROUTING chain is a typed fallback") {
  const auto sandbox = std::filesystem::temp_directory_path() /
                       ("keen-pbr-iptables-rules-only-missing-" +
                        std::to_string(static_cast<long long>(getpid())));
  std::filesystem::remove_all(sandbox);
  std::filesystem::create_directories(sandbox);
  write_executable(sandbox / "iptables",
                   fake_iptables_script("-P PREROUTING ACCEPT",
                                        (sandbox / "m.log").string()));
  PathGuard path_guard;
  const char *old_path = std::getenv("PATH");
  const std::string path = sandbox.string() + ":" +
                           (old_path == nullptr ? std::string{} : old_path);
  REQUIRE(setenv("PATH", path.c_str(), 1) == 0);
  IptablesFirewall firewall;
  firewall.set_ipv6_enabled(false);
  CHECK_THROWS_AS(firewall.prepare_apply(FirewallApplyMode::RulesOnly),
                  FirewallRulesOnlyError);
  std::filesystem::remove_all(sandbox);
}

TEST_CASE("consecutive RulesOnly applies rewrite the same chains while reusing static A") {
  const auto sandbox = std::filesystem::temp_directory_path() /
                       ("keen-pbr-iptables-rules-only-consecutive-" +
                        std::to_string(static_cast<long long>(getpid())));
  std::filesystem::remove_all(sandbox);
  std::filesystem::create_directories(sandbox);
  const auto restore_count_file = sandbox / "restore-count";
  const auto mutation_log = sandbox / "ipset-mutations.log";
  const auto iptables_mutations = sandbox / "iptables-mutations.log";
  const auto restore_one = sandbox / "restore-1.rules";
  const auto restore_two = sandbox / "restore-2.rules";

  // The live state: our chain with static A references and both hooks.
  write_executable(
      sandbox / "iptables",
      fake_iptables_script(
          "-P PREROUTING ACCEPT\n-P OUTPUT ACCEPT\n-N KeenPbrTable\n"
          "-N KeenPbrOutput\n-A PREROUTING -j KeenPbrTable\n"
          "-A OUTPUT -j KeenPbrOutput\n"
          "-A KeenPbrTable -m set --match-set kpbr4s_remote dst -j MARK\n"
          "-A KeenPbrOutput -m set --match-set kpbr4s_remote dst -j MARK",
          iptables_mutations.string()));
  write_executable(
      sandbox / "ipset",
      "#!/bin/sh\n"
      "mutation_log='" + mutation_log.string() + "'\n"
      "if [ \"$1\" = \"restore\" ] || [ \"$1\" = \"flush\" ]; then\n"
      "  /bin/printf '%s\\n' \"$*\" >> \"$mutation_log\"\n"
      "  exit 42\n"
      "fi\n"
      "if [ \"$1\" = \"list\" ] && [ \"$2\" = \"-n\" ]; then\n"
      "  /bin/printf '%s\\n' kpbr4s_remote\n"
      "  exit 0\n"
      "fi\n"
      "if [ \"$1\" = \"list\" ] && [ \"$2\" = \"-t\" ]; then\n"
      "  /bin/printf '%s\\n' '<ipsets><ipset name=\"kpbr4s_remote\"><type>hash:net</type><header><family>inet</family><hashsize>1024</hashsize><maxelem>65536</maxelem></header></ipset></ipsets>'\n"
      "  exit 0\n"
      "fi\n"
      "exit 1\n");
  write_executable(
      sandbox / "iptables-restore",
      "#!/bin/sh\n"
      "count_file='" + restore_count_file.string() + "'\n"
      "count=0\n"
      "if [ \"$1\" = \"--test\" ]; then /bin/cat >/dev/null; exit 0; fi\n"
      "if [ -f \"$count_file\" ]; then count=$(/bin/cat \"$count_file\"); fi\n"
      "count=$((count + 1))\n"
      "/bin/printf '%s\\n' \"$count\" > \"$count_file\"\n"
      "/bin/cat > '" + sandbox.string() + "/restore-'\"$count\"'.rules'\n"
      "exit 0\n");

  PathGuard path_guard;
  const char *old_path = std::getenv("PATH");
  const std::string path = sandbox.string() + ":" +
                           (old_path == nullptr ? std::string{} : old_path);
  REQUIRE(setenv("PATH", path.c_str(), 1) == 0);

  IptablesFirewall firewall;
  firewall.set_ipv6_enabled(false);
  const auto apply_rules_only = [&] {
    firewall.prepare_apply(FirewallApplyMode::RulesOnly);
    CHECK(firewall.static_set_name("remote", AF_INET) == "kpbr4s_remote");
    firewall.create_ipset("kpbr4s_remote", AF_INET);
    FirewallRuleCriteria criteria;
    criteria.dst_set_name = "kpbr4s_remote";
    const auto plan = IptablesBuilderTest::mark_plan(1, criteria);
    firewall.apply(plan, FirewallApplyMode::RulesOnly);
  };

  apply_rules_only();
  apply_rules_only();
  CHECK_FALSE(std::filesystem::exists(mutation_log));

  const auto read_rules = [](const std::filesystem::path &path) {
    std::ifstream input(path);
    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
  };
  const auto first_rules = read_rules(restore_one);
  const auto second_rules = read_rules(restore_two);
  // Same chains and static sets every time; hooks already present are not
  // added again.
  CHECK(first_rules == second_rules);
  CHECK(first_rules.find("-A KeenPbrTable -m set --match-set kpbr4s_remote") !=
        std::string::npos);
  CHECK(first_rules.find("-A KeenPbrOutput -m set --match-set kpbr4s_remote") !=
        std::string::npos);
  CHECK(first_rules.find("kpbr4S_remote") == std::string::npos);
  CHECK(first_rules.find("-A PREROUTING") == std::string::npos);
  CHECK(first_rules.find("-A OUTPUT") == std::string::npos);
  CHECK(first_rules.find("-X ") == std::string::npos);
  CHECK_FALSE(std::filesystem::exists(iptables_mutations));

  std::filesystem::remove_all(sandbox);
}

TEST_CASE("static slot is independent of the chains the rules are written to") {
  const auto a_sets = T::build_rules_for_set_slot(FirewallSetGeneration::A, false);
  CHECK(a_sets.find("-A KeenPbrTable -m set --match-set kpbr4s_sample") !=
        std::string::npos);
  const auto b_sets = T::build_rules_for_set_slot(FirewallSetGeneration::B, true);
  CHECK(b_sets.find("-A KeenPbrTable -m set --match-set kpbr6S_sample") !=
        std::string::npos);
  CHECK(b_sets.find("kpbr6s_sample") == std::string::npos);
}

TEST_CASE("xt_comment registration parser requires an exact token") {
  CHECK(T::has_xt_comment_registration("comment\n"));
  CHECK(T::has_xt_comment_registration("state\tcomment\t\n"));
  CHECK_FALSE(T::has_xt_comment_registration("xt_comment\n"));
  CHECK_FALSE(T::has_xt_comment_registration("comment_extra\n"));
  CHECK_FALSE(T::has_xt_comment_registration("mycomment\n"));
  CHECK_FALSE(T::has_xt_comment_registration("commentary\n"));
}

TEST_CASE("xt_comment preflight fails closed for missing or unreadable registration") {
  const auto sandbox = std::filesystem::temp_directory_path() /
                       ("keen-pbr-iptables-comment-proc-" +
                        std::to_string(static_cast<long long>(getpid())));
  std::filesystem::remove_all(sandbox);
  std::filesystem::create_directories(sandbox / "directory");

  IptablesFirewall firewall;
  CHECK_FALSE(T::probe_xt_comment_from_registration(
      firewall, false, (sandbox / "missing").string()));
  CHECK_FALSE(T::probe_xt_comment_from_registration(
      firewall, false, (sandbox / "directory").string()));

  std::filesystem::remove_all(sandbox);
}

TEST_CASE("xt_comment preflight keeps family registration independent") {
  const auto sandbox = std::filesystem::temp_directory_path() /
                       ("keen-pbr-iptables-comment-family-" +
                        std::to_string(static_cast<long long>(getpid())));
  std::filesystem::remove_all(sandbox);
  std::filesystem::create_directories(sandbox);
  const auto command_log = sandbox / "commands.log";
  {
    std::ofstream v4(sandbox / "ip_tables_matches");
    REQUIRE(v4.good());
    v4 << "comment\n";
  }
  {
    std::ofstream v6(sandbox / "ip6_tables_matches");
    REQUIRE(v6.good());
    v6 << "comment_extra\n";
  }
  write_executable(
      sandbox / "iptables-restore",
      "#!/bin/sh\n"
      "if [ \"$1\" = \"--test\" ]; then\n"
      "  /bin/printf '%s\\n' \"$0\" >> '" + command_log.string() + "'\n"
      "  /bin/cat >/dev/null\n"
      "  exit 1\n"
      "fi\n"
      "exit 0\n");
  write_executable(sandbox / "ip6tables-restore",
                   "#!/bin/sh\n"
                   "/bin/printf '%s\\n' \"$0\" >> '" +
                       command_log.string() +
                       "'\n"
                       "exit 0\n");

  PathGuard path_guard;
  const char *old_path = std::getenv("PATH");
  const std::string path = sandbox.string() + ":" +
                           (old_path == nullptr ? std::string{} : old_path);
  REQUIRE(setenv("PATH", path.c_str(), 1) == 0);

  IptablesFirewall firewall;
  CHECK_FALSE(T::probe_xt_comment_from_registration(
      firewall, false, (sandbox / "ip_tables_matches").string()));
  CHECK_FALSE(T::probe_xt_comment_from_registration(
      firewall, true, (sandbox / "ip6_tables_matches").string()));

  std::ifstream log_input(command_log);
  std::ostringstream log;
  log << log_input.rdbuf();
  CHECK(log.str().find("iptables-restore") != std::string::npos);
  CHECK(log.str().find("ip6tables-restore") == std::string::npos);
  std::filesystem::remove_all(sandbox);
}

TEST_CASE("xt_comment restore grammar failure is a safe fallback") {
  const auto sandbox = std::filesystem::temp_directory_path() /
                       ("keen-pbr-iptables-comment-restore-" +
                        std::to_string(static_cast<long long>(getpid())));
  std::filesystem::remove_all(sandbox);
  std::filesystem::create_directories(sandbox);
  const auto command_log = sandbox / "commands.log";
  {
    std::ofstream registration(sandbox / "ip_tables_matches");
    REQUIRE(registration.good());
    registration << "comment\n";
  }
  write_executable(
      sandbox / "iptables-restore",
      "#!/bin/sh\n"
      "if [ \"$1\" = \"--test\" ]; then\n"
      "  /bin/printf '%s\\n' probe-restore >> '" + command_log.string() + "'\n"
      "  /bin/cat >/dev/null\n"
      "  exit 1\n"
      "fi\n"
      "exit 0\n");

  PathGuard path_guard;
  const char *old_path = std::getenv("PATH");
  const std::string path = sandbox.string() + ":" +
                           (old_path == nullptr ? std::string{} : old_path);
  REQUIRE(setenv("PATH", path.c_str(), 1) == 0);

  IptablesFirewall firewall;
  CHECK_FALSE(T::probe_xt_comment_from_registration(
      firewall, false, (sandbox / "ip_tables_matches").string()));
  std::ifstream log_input(command_log);
  std::ostringstream log;
  log << log_input.rdbuf();
  CHECK(log.str().find("probe-restore") != std::string::npos);
  std::filesystem::remove_all(sandbox);
}

TEST_CASE("unsupported xt_comment omits comments without changing apply") {
  const auto sandbox = std::filesystem::temp_directory_path() /
                       ("keen-pbr-iptables-no-comment-" +
                        std::to_string(static_cast<long long>(getpid())));
  std::filesystem::remove_all(sandbox);
  std::filesystem::create_directories(sandbox);
  const auto command_log = sandbox / "commands.log";
  const auto restore_log = sandbox / "restore.rules";

  const auto write_iptables = [&](const std::filesystem::path &path) {
    write_executable(
        path,
        "#!/bin/sh\n"
        "log='" + command_log.string() + "'\n"
        "last=''\n"
        "for arg in \"$@\"; do last=\"$arg\"; done\n"
        "case \"$last\" in\n"
        "  PREROUTING) /bin/printf '%s\\n' '-A PREROUTING -j KeenPbrTable' ;;\n"
        "  OUTPUT) /bin/printf '%s\\n' '-A OUTPUT -j KeenPbrOutput' ;;\n"
        "esac\n"
        "exit 0\n");
  };
  write_iptables(sandbox / "iptables");
  write_iptables(sandbox / "ip6tables");
  write_executable(
      sandbox / "ipset",
      "#!/bin/sh\n"
      "log='" + command_log.string() + "'\n"
      "if [ \"$1\" = \"save\" ]; then exit 0; fi\n"
      "if [ \"$1\" = \"restore\" ]; then\n"
      "  /bin/printf '%s\\n' ipset-restore >> \"$log\"\n"
      "  /bin/cat >/dev/null\n"
      "  exit 0\n"
      "fi\n"
      "if [ \"$1\" = \"flush\" ] || [ \"$1\" = \"destroy\" ]; then\n"
      "  /bin/printf '%s\\n' ipset-mutation >> \"$log\"\n"
      "fi\n"
      "exit 0\n");
  write_executable(
      sandbox / "iptables-restore",
      "#!/bin/sh\n"
      "if [ \"$1\" = \"--test\" ]; then\n"
      "  /bin/printf '%s\\n' probe-restore >> '" + command_log.string() + "'\n"
      "  /bin/cat >/dev/null\n"
      "  exit 1\n"
      "fi\n"
      "/bin/printf '%s\\n' restore >> '" + command_log.string() + "'\n"
      "/bin/cat > '" + restore_log.string() + "'\n"
      "exit 0\n");

  PathGuard path_guard;
  const char *old_path = std::getenv("PATH");
  const std::string path = sandbox.string() + ":" +
                           (old_path == nullptr ? std::string{} : old_path);
  REQUIRE(setenv("PATH", path.c_str(), 1) == 0);

  IptablesFirewall firewall;
  firewall.set_ipv6_enabled(false);
  firewall.set_clear_dynamic_sets_on_apply(false);
  firewall.prepare_apply(FirewallApplyMode::Destructive);
  firewall.create_ipset("kpbr4s_comment_test", AF_INET);
  FirewallRuleCriteria criteria;
  criteria.dst_set_name = "kpbr4s_comment_test";
  const auto plan = IptablesBuilderTest::mark_plan(
      7, criteria, FirewallRuleKey{"route.mark", "outbound"});
  CHECK_NOTHROW(firewall.apply(plan, FirewallApplyMode::Destructive));

  std::ifstream commands_input(command_log);
  std::ostringstream commands;
  commands << commands_input.rdbuf();
  std::ifstream restore_input(restore_log);
  std::ostringstream restore;
  restore << restore_input.rdbuf();
  const auto command_text = commands.str();
  CHECK(command_text.find("restore") != std::string::npos);
  if (command_text.find("probe") != std::string::npos) {
    CHECK(command_text.find("probe") < command_text.find("restore"));
  }
  if (command_text.find("probe-restore") != std::string::npos) {
    CHECK(command_text.find("probe-restore") <
          command_text.find("ipset-restore"));
  }
  CHECK(restore.str().find("-m comment") == std::string::npos);
  std::filesystem::remove_all(sandbox);
}

TEST_CASE("live rules referencing both static generations fail before PreserveSets mutation") {
  const auto sandbox = std::filesystem::temp_directory_path() /
                       ("keen-pbr-iptables-ambiguous-" +
                        std::to_string(static_cast<long long>(getpid())));
  std::filesystem::remove_all(sandbox);
  std::filesystem::create_directories(sandbox);
  const auto mutation_log = sandbox / "mutation.log";
  write_executable(
      sandbox / "iptables",
      fake_iptables_script(
          "-N KeenPbrTable\n"
          "-A KeenPbrTable -m set --match-set kpbr4s_x dst -j MARK\n"
          "-A KeenPbrTable -m set --match-set kpbr4S_y dst -j MARK",
          (sandbox / "iptables-mutation.log").string()));
  write_executable(
      sandbox / "ipset",
      "#!/bin/sh\n"
      "/bin/printf '%s\\n' mutation >> '" + mutation_log.string() + "'\n"
      "exit 0\n");

  PathGuard path_guard;
  const char *old_path = std::getenv("PATH");
  const std::string path = sandbox.string() + ":" +
                           (old_path == nullptr ? std::string{} : old_path);
  REQUIRE(setenv("PATH", path.c_str(), 1) == 0);

  IptablesFirewall firewall;
  firewall.set_ipv6_enabled(false);
  CHECK_THROWS_AS(firewall.prepare_apply(FirewallApplyMode::PreserveSets),
                  FirewallError);
  CHECK_FALSE(std::filesystem::exists(mutation_log));
  std::filesystem::remove_all(sandbox);
}

TEST_CASE("hook parser counts only exact daemon-owned jumps") {
  const std::string rules = "-P PREROUTING ACCEPT\n"
                            "-A PREROUTING -j KeenPbrTable\n"
                            "-A PREROUTING -p tcp -j KeenPbrTable\n"
                            "-A PREROUTING -j ForeignTable\n"
                            "-A PREROUTING -j KeenPbrTable\n";
  CHECK(T::count_exact_jump(rules, "PREROUTING", "KeenPbrTable") == 2);
  CHECK(T::count_exact_jump(rules, "PREROUTING", "ForeignTable") == 1);
  CHECK(T::count_exact_jump(rules, "OUTPUT", "KeenPbrTable") == 0);
}

// =============================================================================
// build_ipt_script tests
// =============================================================================

TEST_CASE("build_ipt_script: IPv4 mark rule") {
  auto s = T::build_ipt_script(false, {mark_rule("myset", false, 0x100)});
  CHECK(s.find("*mangle") != std::string::npos);
  CHECK(s.find(":KeenPbrTable") != std::string::npos);
  CHECK(s.find("-A KeenPbrOutput") == std::string::npos);
  CHECK(s.find("-A KeenPbrTable -m set --match-set myset dst -j MARK "
               "--set-xmark 0x100/0xffffffff") != std::string::npos);
  CHECK(s.find("-A KeenPbrTable -m set --match-set myset dst -j RETURN") !=
        std::string::npos);
  CHECK(s.size() >= 7);
  CHECK(s.substr(s.size() - 7) == "COMMIT\n");
}

TEST_CASE("build_ipt_script: keyed policy rules carry validated ownership comments") {
  const FirewallRuleKey key{"route.mark", "outbound"};
  const auto script = T::build_ipt_script_for_rule(
      false, Rule::Mark, 0x100, {}, false, 0xFFFFFFFFu, {}, key);
  CHECK(script.find(
            "-m comment --comment kpbr:v1:route.mark:outbound -j MARK") !=
        std::string::npos);
  CHECK(script.find(
            "-m comment --comment kpbr:v1:route.mark:outbound -j RETURN") !=
        std::string::npos);

  const FirewallRuleKey max_key{std::string(128, 'm'), std::string(118, 'i')};
  const auto max_script = T::build_ipt_script_for_rule(
      true, Rule::Drop, 0, {}, false, 0xFFFFFFFFu, {}, max_key);
  CHECK(max_script.find(max_key.comment()) != std::string::npos);

  const Rule keyed_rule{"kpbr4_policy", false, false, Rule::Mark, 0x100, {}, key};
  const auto raw_script = T::build_raw_script({keyed_rule});
  CHECK(raw_script.find(
            "-m comment --comment kpbr:v1:route.mark:outbound -j MARK") !=
        std::string::npos);
  const auto output_script = T::build_output_script_for_family(
      false, {keyed_rule});
  CHECK(output_script.find(
            "-m comment --comment kpbr:v1:route.mark:outbound -j MARK") !=
        std::string::npos);

  FirewallRuleCriteria bundle_filter;
  bundle_filter.proto = L4Proto::TcpUdp;
  PrefilterFixture bundle_prefilter;
  bundle_prefilter.restore_conntrack_mark = true;
  bundle_prefilter.conntrack_mark_mask = 0xFF00U;
  const auto bundle_script = T::build_ipt_script_for_rule(
      false, Rule::Mark, 0x100, bundle_filter, false, 0xFFFFFFFFu,
      bundle_prefilter, key);
  const std::string comment = "-m comment --comment " + key.comment();
  std::size_t comment_count = 0;
  for (std::size_t position = bundle_script.find(comment);
       position != std::string::npos;
       position = bundle_script.find(comment, position + comment.size())) {
    ++comment_count;
  }
  CHECK(comment_count == 6);

  CHECK_THROWS(T::build_ipt_script_for_rule(
      false, Rule::Pass, 0, {}, false, 0xFFFFFFFFu, {},
      FirewallRuleKey{"route\"mark", "outbound"}));
}

TEST_CASE("iptables emitted prefilter bundle is ordered before classifiers") {
  const FirewallRuleKey route_key{"route.mark", "one"};
  PrefilterFixture prefilter;
  prefilter.restore_conntrack_mark = true;
  prefilter.conntrack_mark_mask = 0xFFFFFFFFu;
  prefilter.restore_conntrack_mark_comment =
      FirewallRuleKey{"prefilter.restore_conntrack_mark", "one"}.comment();
  const auto v4 = IptablesBuilderTest::build_ipt_script_for_rule(
      false, IptablesBuilderTest::RuleDesc::Mark, 0x10000u, {}, true,
      0xFFFFFFFFu, prefilter, route_key);

  std::istringstream input(v4);
  std::vector<std::string> lines;
  for (std::string line; std::getline(input, line);) lines.push_back(line);
  const auto restore_line = std::find_if(
      lines.begin(), lines.end(), [](const std::string& line) {
        return line.find("-m connmark !") != std::string::npos;
      });
  const auto classifier_line = std::find_if(
      lines.begin(), lines.end(), [](const std::string& line) {
        return line.find("-j MARK --set-xmark") != std::string::npos;
      });
  REQUIRE(restore_line != lines.end());
  REQUIRE(classifier_line != lines.end());
  CHECK(restore_line < classifier_line);
  // The second restore rule (RETURN for already marked packets) follows it
  // before any classifier.
  CHECK(std::find_if(restore_line, classifier_line,
                     [](const std::string& line) {
                       return line.find("-m mark !") != std::string::npos;
                     }) != classifier_line);
}

TEST_CASE("iptables empty owned marks retain unconditional restore output") {
  PrefilterFixture prefilter;
  prefilter.restore_conntrack_mark = true;
  prefilter.conntrack_mark_mask = 0xFFFFFFFFu;
  const FirewallRuleKey restore_key{"prefilter.restore_conntrack_mark", "one"};
  prefilter.restore_conntrack_mark_comment = restore_key.comment();
  const auto script = T::build_ipt_script(false, {}, prefilter);
  CHECK(script.find("-j CONNMARK --restore-mark") != std::string::npos);
  CHECK(script.find("-m mark ! --mark 0x0/0xffffffff") != std::string::npos);
}

TEST_CASE("build_ipt_script: IPv4 drop rule") {
  auto s = T::build_ipt_script(false, {drop_rule("blacklist", false)});
  CHECK(s.find("-A KeenPbrTable -m set --match-set blacklist dst -j DROP") !=
        std::string::npos);
}

TEST_CASE("build_ipt_script: IPv4 pass rule") {
  auto s = T::build_ipt_script(false, {pass_rule("allowlist", false)});
  CHECK(s.find("-A KeenPbrTable -m set --match-set allowlist dst -j RETURN") !=
        std::string::npos);
}

TEST_CASE("build_ipt_script: IPv6 mark rule") {
  auto s = T::build_ipt_script(true, {mark_rule("v6set", true, 0x200)});
  CHECK(s.find("-A KeenPbrTable -m set --match-set v6set dst -j MARK "
               "--set-xmark 0x200/0xffffffff") != std::string::npos);
  CHECK(s.find("-A KeenPbrTable -m set --match-set v6set dst -j RETURN") !=
        std::string::npos);
  CHECK(s.substr(s.size() - 7) == "COMMIT\n");
}

TEST_CASE("build_ipt_script: ipv6=false filters out IPv6 rules") {
  auto s = T::build_ipt_script(false, {mark_rule("v4set", false, 0x100),
                                       mark_rule("v6set", true, 0x200)});
  CHECK(s.find("v4set") != std::string::npos);
  CHECK(s.find("v6set") == std::string::npos);
}

TEST_CASE("build_ipt_script: ipv6=true filters out IPv4 rules") {
  auto s = T::build_ipt_script(true, {mark_rule("v4set", false, 0x100),
                                      mark_rule("v6set", true, 0x200)});
  CHECK(s.find("v6set") != std::string::npos);
  CHECK(s.find("v4set") == std::string::npos);
}

TEST_CASE("build_ipt_script: zero fwmark") {
  auto s = T::build_ipt_script(false, {mark_rule("zeroset", false, 0)});
  CHECK(s.find("--set-xmark 0x0/0xffffffff") != std::string::npos);
}

TEST_CASE("build_ipt_script: multiple rules appear in order") {
  auto s = T::build_ipt_script(
      false, {mark_rule("first", false, 0x1), drop_rule("second", false)});
  auto pos_first = s.find("first");
  auto pos_second = s.find("second");
  CHECK(pos_first != std::string::npos);
  CHECK(pos_second != std::string::npos);
  CHECK(pos_first < pos_second);
}

TEST_CASE("build_ipt_script: empty rules still build the chains and hooks (fresh install)") {
  CHECK(T::build_mangle_script(false, {}) ==
        "*mangle\n:KeenPbrTable - [0:0]\n:KeenPbrOutput - [0:0]\n"
        "-A PREROUTING -j KeenPbrTable\n-A OUTPUT -j KeenPbrOutput\nCOMMIT\n");
  // IPv6 renders identically for the same layout.
  CHECK(T::build_mangle_script(true, {}) == T::build_mangle_script(false, {}));
}

TEST_CASE("mangle restore: route rules land in both chains, DNS-detour only in OUTPUT") {
  // Create a custom plan with both route and DNS-detour rules
  FirewallPlan plan;
  plan.fwmark_mask = 0xFFFFFFFFu;

  FirewallRuleInstance route;
  route.family = FirewallFamily::ipv4;
  route.hook = FirewallHook::prerouting;
  route.criteria.dst_set_name = "kpbr4s_x";
  route.action = MarkAction{0x100, 0xFFFFFFFFu};
  plan.rules.push_back(std::move(route));

  FirewallRuleInstance detour;
  detour.family = FirewallFamily::ipv4;
  detour.hook = FirewallHook::output;
  detour.criteria.dst_set_name = "kpbr4s_dns";
  detour.action = MarkAction{0x200, 0xFFFFFFFFu};
  plan.rules.push_back(std::move(detour));

  const auto ruleset = T::lower(plan);
  const auto script = T::table_script("mangle", false,
                                      {{"KeenPbrTable", "PREROUTING"},
                                       {"KeenPbrOutput", "OUTPUT"}},
                                      ruleset);
  CHECK(script.find("-A KeenPbrTable -m set --match-set kpbr4s_x dst -j MARK") !=
        std::string::npos);
  CHECK(script.find("-A KeenPbrOutput -m set --match-set kpbr4s_x dst -j MARK") !=
        std::string::npos);
  CHECK(script.find("-A KeenPbrTable -m set --match-set kpbr4s_dns") ==
        std::string::npos);
  CHECK(script.find("-A KeenPbrOutput -m set --match-set kpbr4s_dns dst -j MARK") !=
        std::string::npos);
  // PREROUTING chain rules come first, then OUTPUT, then the hooks.
  CHECK(script.find("-A KeenPbrTable ") < script.find("-A KeenPbrOutput "));
  CHECK(script.find("-A KeenPbrOutput ") < script.find("-A PREROUTING "));
  CHECK(script.substr(script.size() - 7) == "COMMIT\n");
}

TEST_CASE("mangle restore: the inbound-interface guard is PREROUTING only") {
  const auto script = T::build_mangle_script(
      false, {mark_rule("myset", false, 0x100)},
      prefilter_with_interfaces({"br0"}));
  CHECK(script.find("-A KeenPbrTable ! -i br0 -j RETURN\n") !=
        std::string::npos);
  CHECK(script.find("-A KeenPbrOutput ! -i br0") == std::string::npos);
  CHECK(script.find("-A KeenPbrOutput -m set --match-set myset dst -j MARK") !=
        std::string::npos);
}

TEST_CASE("restore re-apply is idempotent: hooks are not duplicated, extras are dropped") {
  const std::string live =
      "-P PREROUTING ACCEPT\n-P OUTPUT ACCEPT\n-N KeenPbrTable\n-N KeenPbrOutput\n"
      "-A PREROUTING -j KeenPbrTable\n-A OUTPUT -j KeenPbrOutput\n"
      "-A KeenPbrTable -j RETURN\n";
  const auto script = T::build_mangle_script(false, {}, {}, live);
  CHECK(script ==
        "*mangle\n:KeenPbrTable - [0:0]\n:KeenPbrOutput - [0:0]\nCOMMIT\n");

  const std::string duplicated =
      live + "-A PREROUTING -j KeenPbrTable\n-A PREROUTING -p tcp -j KeenPbrTable\n";
  const auto fixed = T::build_mangle_script(false, {}, {}, duplicated);
  // Two exact hooks -> one removed; the conditional foreign-looking jump is
  // not ours and is left alone.
  CHECK(fixed ==
        "*mangle\n:KeenPbrTable - [0:0]\n:KeenPbrOutput - [0:0]\n"
        "-D PREROUTING -j KeenPbrTable\nCOMMIT\n");
}

TEST_CASE("restore upgrade from the legacy non-raw A/B layout deletes it in the same commit") {
  const std::string legacy =
      "-P PREROUTING ACCEPT\n-P OUTPUT ACCEPT\n"
      "-N KeenPbrTable\n-N KeenPbrTable_OUTPUT\n-N KeenPbrTable_A\n"
      "-N KeenPbrTable_B\n-N ForeignChain\n"
      "-A PREROUTING -j KeenPbrTable\n-A OUTPUT -j KeenPbrTable_OUTPUT\n"
      "-A KeenPbrTable -j KeenPbrTable_B\n"
      "-A KeenPbrTable_OUTPUT -j KeenPbrTable_B\n"
      "-A KeenPbrTable_B -j RETURN\n"
      "-A ForeignChain -j ACCEPT\n";
  const auto script = T::build_mangle_script(false, {mark_rule("x", false, 1)}, {},
                                             legacy);
  const auto at = [&](const std::string &needle) { return script.find(needle); };
  // New chains and every legacy chain are declared (flushed) first.
  for (const char *chain : {"KeenPbrTable", "KeenPbrOutput", "KeenPbrTable_OUTPUT",
                            "KeenPbrTable_A", "KeenPbrTable_B"}) {
    CHECK_MESSAGE(at(std::string(":") + chain + " - [0:0]\n") != std::string::npos,
                  chain);
  }
  CHECK(at(":ForeignChain") == std::string::npos);
  // The legacy OUTPUT hook goes away before its chain is deleted; the
  // PREROUTING hook is kept single, and the new OUTPUT hook is added.
  REQUIRE(at("-D OUTPUT -j KeenPbrTable_OUTPUT\n") != std::string::npos);
  CHECK(at("-D OUTPUT -j KeenPbrTable_OUTPUT\n") < at("-X KeenPbrTable_OUTPUT\n"));
  CHECK(at("-A PREROUTING -j KeenPbrTable\n") == std::string::npos);
  CHECK(at("-D PREROUTING") == std::string::npos);
  CHECK(at("-A OUTPUT -j KeenPbrOutput\n") != std::string::npos);
  for (const char *chain :
       {"KeenPbrTable_OUTPUT", "KeenPbrTable_A", "KeenPbrTable_B"}) {
    CHECK_MESSAGE(at(std::string("-X ") + chain + "\n") != std::string::npos,
                  chain);
  }
  CHECK(at("-X KeenPbrTable\n") == std::string::npos);
  CHECK(at("-X KeenPbrOutput\n") == std::string::npos);
  // Every declaration precedes every rule and delete; COMMIT is last.
  CHECK(at(":KeenPbrTable_B - [0:0]\n") < at("-A KeenPbrTable "));
  CHECK(at("-A KeenPbrOutput ") < at("-X "));
  CHECK(script.substr(script.size() - 7) == "COMMIT\n");
  // Exactly one *table section: one commit.
  CHECK(script.find("COMMIT") == script.size() - 7);
}

TEST_CASE("restore does not delete a legacy chain that a foreign rule still reaches") {
  const std::string live =
      "-P OUTPUT ACCEPT\n-N KeenPbrTable_A\n-N Other\n"
      "-A Other -j KeenPbrTable_A\n";
  const auto script = T::build_mangle_script(false, {}, {}, live);
  CHECK(script.find(":KeenPbrTable_A - [0:0]\n") != std::string::npos);
  CHECK(script.find("-X KeenPbrTable_A") == std::string::npos);
}

TEST_CASE("restore upgrade from the real Keenetic legacy fixtures (raw layout)") {
  // Raw mode as seen on a router that ran the A/B layout: KeenPbrRaw is a
  // dispatcher to KeenPbrRaw_B with a stale KeenPbrRaw_A; mangle holds the
  // KeenPbrOutput dispatcher and KeenPbrOutput_B.
  const std::string raw_dump = read_fixture("keenetic_1.4.21/raw_v4.rules");
  const auto raw = T::build_raw_script_for_family(false, {mark_rule("x", false, 1)},
                                                  {}, raw_dump);
  CHECK(raw.find("*raw\n:KeenPbrRaw - [0:0]\n") == 0);
  CHECK(raw.find(":KeenPbrRaw_A - [0:0]\n") != std::string::npos);
  CHECK(raw.find(":KeenPbrRaw_B - [0:0]\n") != std::string::npos);
  CHECK(raw.find("-X KeenPbrRaw_A\n") != std::string::npos);
  CHECK(raw.find("-X KeenPbrRaw_B\n") != std::string::npos);
  CHECK(raw.find("-X KeenPbrRaw\n") == std::string::npos);
  // The hook `-A PREROUTING -j KeenPbrRaw` exists once: nothing added/removed.
  CHECK(raw.find("-A PREROUTING") == std::string::npos);
  CHECK(raw.find("-D PREROUTING") == std::string::npos);
  CHECK(raw.find("-A KeenPbrRaw -m set --match-set") != std::string::npos);
  CHECK(raw.find("-A KeenPbrRaw -j KeenPbrRaw_B") == std::string::npos);

  const std::string mangle_dump = read_fixture("keenetic_1.4.21/mangle_v4.rules");
  const auto mangle = T::build_output_script_for_family(
      false, {mark_rule("x", false, 1)}, {}, mangle_dump);
  CHECK(mangle.find(":KeenPbrOutput - [0:0]\n:KeenPbrOutput_B - [0:0]\n") !=
        std::string::npos);
  CHECK(mangle.find("-X KeenPbrOutput_B\n") != std::string::npos);
  CHECK(mangle.find("-X KeenPbrOutput\n") == std::string::npos);
  CHECK(mangle.find("-A OUTPUT") == std::string::npos);
  CHECK(mangle.find("-D OUTPUT") == std::string::npos);
  // Foreign NDM chains and their rules are never touched.
  CHECK(mangle.find("_NDM_") == std::string::npos);
  CHECK(mangle.find("*mangle\n") == 0);
  CHECK(mangle.find("-A KeenPbrOutput_B") == std::string::npos);
}

TEST_CASE("raw drop and pass rules are emitted into the single raw chain") {
  const auto raw_drop = T::build_raw_script({drop_rule("blocked", false)});
  CHECK(raw_drop.find(
            "-A KeenPbrRaw -m set --match-set blocked dst -j DROP\n") !=
        std::string::npos);
  CHECK(raw_drop.find(
            "-A KeenPbrTable -m set --match-set blocked dst -j DROP\n") ==
        std::string::npos);

  const auto raw_pass = T::build_raw_script({pass_rule("allowed", false)});
  CHECK(raw_pass.find(
            "-A KeenPbrRaw -m set --match-set allowed dst -j RETURN\n") !=
        std::string::npos);
}

TEST_CASE("build_ipt_script: global prefilter RETURN lines are emitted before "
          "route rules") {
  auto s = T::build_ipt_script(false, {mark_rule("myset", false, 0x100)},
                               prefilter_with_interfaces({"br0"}));

  const std::string dnat =
      "-A KeenPbrTable -m conntrack --ctstate DNAT -j RETURN\n";
  const std::string marked =
      "-A KeenPbrTable -m mark ! --mark 0x0/0xffffffff -j ACCEPT\n";
  const std::string iface = "-A KeenPbrTable ! -i br0 -j RETURN\n";
  const std::string mark = "-A KeenPbrTable -m set --match-set myset dst -j "
                           "MARK --set-xmark 0x100/0xffffffff\n";

  const auto dnat_pos = s.find(dnat);
  const auto marked_pos = s.find(marked);
  const auto iface_pos = s.find(iface);
  const auto mark_pos = s.find(mark);
  REQUIRE(dnat_pos != std::string::npos);
  REQUIRE(marked_pos != std::string::npos);
  REQUIRE(iface_pos != std::string::npos);
  REQUIRE(mark_pos != std::string::npos);
  CHECK(s.find("--ctstate RELATED,ESTABLISHED") == std::string::npos);
  CHECK(dnat_pos < marked_pos);
  CHECK(marked_pos < iface_pos);
  CHECK(iface_pos < mark_pos);
}

TEST_CASE("build_ipt_script: conntrack restore is original-direction and mask "
          "scoped") {
  PrefilterFixture prefilter;
  prefilter.restore_conntrack_mark = true;
  prefilter.conntrack_mark_mask = 0x00FF0000U;
  const auto script =
      T::build_ipt_script(false, {mark_rule("myset", false, 0x100)}, prefilter);
  CHECK(script.find(
            "-m conntrack --ctdir ORIGINAL -m connmark ! --mark 0x0/0xff0000") !=
        std::string::npos);
  CHECK(script.find("CONNMARK --restore-mark --mask 0xff0000") !=
        std::string::npos);
  CHECK(script.find("CONNMARK --save-mark --mask 0xff0000") !=
        std::string::npos);
  const auto restore =
      script.find("--ctdir ORIGINAL -m connmark ! --mark 0x0/0xff0000");
  const auto restored_return = script.find(
      "-m conntrack --ctdir ORIGINAL -m mark ! --mark 0x0/0xff0000 -j RETURN");
  const auto policy = script.find("--match-set myset dst -j MARK");
  const auto save = script.find(
      "--match-set myset dst -j CONNMARK --save-mark --mask 0xff0000");
  REQUIRE(restore != std::string::npos);
  REQUIRE(restored_return != std::string::npos);
  REQUIRE(policy != std::string::npos);
  REQUIRE(save != std::string::npos);
  CHECK(restore < restored_return);
  CHECK(restored_return < policy);
  CHECK(policy < save);
}

TEST_CASE("build_ipt_script: skip_marked_packets prefilter can be disabled") {
  PrefilterFixture prefilter;
  prefilter.skip_established_or_dnat = true;
  prefilter.skip_marked_packets = false;

  auto s =
      T::build_ipt_script(false, {mark_rule("myset", false, 0x100)}, prefilter);
  CHECK(s.find("-m mark ! --mark 0x0/0xffffffff -j ACCEPT") ==
        std::string::npos);
}

TEST_CASE("build_ipt_script: multi-interface prefilter expands route rules "
          "with -i matches") {
  auto s =
      T::build_ipt_script(false, {pass_rule("allowlist", false)},
                          prefilter_with_interfaces({"br0", "wg0"}, false));

  CHECK(s.find("-A KeenPbrTable -m set --match-set allowlist dst -i br0 -j "
               "RETURN\n") != std::string::npos);
  CHECK(s.find("-A KeenPbrTable -m set --match-set allowlist dst -i wg0 -j "
               "RETURN\n") != std::string::npos);
}

TEST_CASE("build_ipt_script: config-derived prefilter keeps route rule body "
          "unchanged") {
  auto cfg = parse_valid_config(R"({
    "outbounds":[
      {"tag":"wan","type":"interface","interface":"eth0","gateway":"192.0.2.1"}
    ],
    "lists":{
      "local":{"ip_cidrs":["192.168.0.0/16"]}
    },
    "route":{
      "inbound_interfaces":["br0"],
      "rules":[
        {"list":["local"],"outbound":"wan"}
      ]
    }
  })");

  const auto prefilter = prefilter_fixture_from_config(cfg);
  auto s = T::build_ipt_script(false, {mark_rule("kpbr4_local", false, 0x100)},
                               prefilter);

  const std::string iface = "-A KeenPbrTable ! -i br0 -j RETURN\n";
  const std::string mark = "-A KeenPbrTable -m set --match-set kpbr4_local dst "
                           "-j MARK --set-xmark 0x100/0xffffffff\n";
  const auto iface_pos = s.find(iface);
  const auto mark_pos = s.find(mark);
  REQUIRE(iface_pos != std::string::npos);
  REQUIRE(mark_pos != std::string::npos);
  CHECK(iface_pos < mark_pos);
}

TEST_CASE("build_ipt_script: config rejects interface restore injection before "
          "serialization") {
  CHECK_THROWS(parse_valid_config(
      "{\"route\":{\"inbound_interfaces\":[\"br0\\n-A KeenPbrTable -j DROP\"],"
      "\"rules\":[]}}"));
}

TEST_CASE("build_ipt_script_for_rule: masked mark rule uses set-xmark") {
  FirewallRuleCriteria criteria;
  auto s = T::build_ipt_script_for_rule(false, Rule::Mark, 0x00010000, criteria,
                                        true, 0x00FF0000);
  CHECK(s.find("-A KeenPbrTable -m set --match-set pairwise_set dst -j MARK "
               "--set-xmark 0x10000/0xff0000\n") != std::string::npos);
  CHECK(s.find("[0:0] -A") == std::string::npos);
}

TEST_CASE("build_ipt_script: config-derived prefilter omits interface guard "
          "when inbound list is empty") {
  auto cfg = parse_valid_config(R"({
    "outbounds":[
      {"tag":"wan","type":"interface","interface":"eth0","gateway":"192.0.2.1"}
    ],
    "lists":{
      "local":{"ip_cidrs":["192.168.0.0/16"]}
    },
    "route":{
      "inbound_interfaces":[],
      "rules":[
        {"list":["local"],"outbound":"wan"}
      ]
    }
  })");

  const auto prefilter = prefilter_fixture_from_config(cfg);
  auto s = T::build_ipt_script(false, {mark_rule("kpbr4_local", false, 0x100)},
                               prefilter);

  CHECK(s.find("! -i ") == std::string::npos);
  CHECK(s.find("-A KeenPbrTable -m set --match-set kpbr4_local dst -j MARK "
               "--set-xmark 0x100/0xffffffff\n") != std::string::npos);
}

// =============================================================================
// build_proto_port_fragment tests
// =============================================================================

TEST_CASE("build_proto_port_fragment: empty filter → empty string") {
  CHECK(T::build_proto_port_fragment("", "", "") == "");
}

TEST_CASE("build_proto_port_fragment: tcp + single dest_port") {
  auto frag = T::build_proto_port_fragment("tcp", "", "443");
  CHECK(frag == " -p tcp --dport 443");
}

TEST_CASE("build_proto_port_fragment: udp + port range") {
  auto frag = T::build_proto_port_fragment("udp", "", "8000-9000");
  CHECK(frag == " -p udp --dport 8000:9000");
}

TEST_CASE("build_proto_port_fragment: tcp + port list → multiport") {
  auto frag = T::build_proto_port_fragment("tcp", "", "80,443");
  CHECK(frag == " -p tcp -m multiport --dports 80,443");
}

TEST_CASE("build_proto_port_fragment: src_port + dest_port → sport and dport") {
  auto frag = T::build_proto_port_fragment("tcp", "1024-65535", "80");
  CHECK(frag == " -p tcp --sport 1024:65535 --dport 80");
}

TEST_CASE("build_proto_port_fragment: proto only, no ports") {
  auto frag = T::build_proto_port_fragment("udp", "", "");
  CHECK(frag == " -p udp");
}

// =============================================================================
// build_ipt_script with proto/port filter tests
// =============================================================================

TEST_CASE("build_ipt_script: tcp + single dest_port in rule") {
  ProtoPortFilter f;
  f.proto = L4Proto::Tcp;
  f.dst_port = "443";
  auto s = T::build_ipt_script(false, {mark_rule("myset", false, 0x100, f)});
  CHECK(s.find("-A KeenPbrTable -m set --match-set myset dst -p tcp --dport "
               "443 -j MARK --set-xmark 0x100/0xffffffff") !=
        std::string::npos);
}

TEST_CASE("build_ipt_script: dscp matcher is emitted") {
  ProtoPortFilter f;
  f.dscp = 46;
  auto s = T::build_ipt_script(false, {mark_rule("myset", false, 0x100, f)});
  CHECK(s.find("-A KeenPbrTable -m set --match-set myset dst -m dscp --dscp "
               "46 -j MARK --set-xmark 0x100/0xffffffff") != std::string::npos);
}

TEST_CASE("build_ipt_script: udp + port range in rule") {
  ProtoPortFilter f;
  f.proto = L4Proto::Udp;
  f.dst_port = "8000-9000";
  auto s = T::build_ipt_script(false, {drop_rule("bl", false, f)});
  CHECK(s.find("-A KeenPbrTable -m set --match-set bl dst -p udp --dport "
               "8000:9000 -j DROP") != std::string::npos);
}

TEST_CASE("build_ipt_script: tcp/udp + port list → two rules") {
  ProtoPortFilter f;
  f.proto = L4Proto::TcpUdp;
  f.dst_port = "80,443";
  // The backend expands tcp/udp, so simulate it by passing two pending rules.
  // already expanded
  ProtoPortFilter ftcp;
  ftcp.proto = L4Proto::Tcp;
  ftcp.dst_port = "80,443";
  ProtoPortFilter fudp;
  fudp.proto = L4Proto::Udp;
  fudp.dst_port = "80,443";
  auto s = T::build_ipt_script(false, {mark_rule("s", false, 0x10, ftcp),
                                       mark_rule("s", false, 0x10, fudp)});
  CHECK(s.find("-p tcp -m multiport --dports 80,443") != std::string::npos);
  CHECK(s.find("-p udp -m multiport --dports 80,443") != std::string::npos);
}

TEST_CASE("build_ipt_script: oversized multiport list is split at 15 slots") {
  ProtoPortFilter f;
  f.proto = L4Proto::Tcp;
  f.dst_port = "1,3,5,7,9,11,13,15,17,19,21,23,25,27,29,31";
  auto s = T::build_ipt_script(false, {drop_rule("bl", false, f)});
  CHECK(s.find("--dports 1,3,5,7,9,11,13,15,17,19,21,23,25,27,29 -j DROP") !=
        std::string::npos);
  CHECK(s.find("--dport 31 -j DROP") != std::string::npos);
}

TEST_CASE("build_ipt_script: multiport ranges consume two slots") {
  ProtoPortFilter f;
  f.proto = L4Proto::Udp;
  f.dst_port = "1-2,4-5,7-8,10-11,13-14,16-17,19-20,22-23";
  auto s = T::build_ipt_script(false, {drop_rule("bl", false, f)});
  CHECK(s.find("--dports 1:2,4:5,7:8,10:11,13:14,16:17,19:20 -j DROP") !=
        std::string::npos);
  CHECK(s.find("--dport 22:23 -j DROP") != std::string::npos);
}

TEST_CASE("build_ipt_script: oversized negated multiport list remains AND") {
  ProtoPortFilter f;
  f.proto = L4Proto::Tcp;
  f.dst_port = "1,3,5,7,9,11,13,15,17,19,21,23,25,27,29,31";
  f.negate_dst_port = true;
  auto s = T::build_ipt_script(false, {drop_rule("bl", false, f)});
  CHECK(s.find("-m multiport ! --dports "
               "1,3,5,7,9,11,13,15,17,19,21,23,25,27,29 "
               "! --dport 31 -j DROP") != std::string::npos);
}

TEST_CASE("build_ipt_script: source list preserves single destination port") {
  ProtoPortFilter f;
  f.proto = L4Proto::Tcp;
  f.src_port = "80,443";
  f.dst_port = "8443";
  auto s = T::build_ipt_script(false, {drop_rule("bl", false, f)});
  CHECK(s.find("-m multiport --sports 80,443 --dport 8443 -j DROP") !=
        std::string::npos);
}

TEST_CASE("build_ipt_script: destination list preserves single source port") {
  ProtoPortFilter f;
  f.proto = L4Proto::Tcp;
  f.src_port = "1024";
  f.dst_port = "80,443";
  auto s = T::build_ipt_script(false, {drop_rule("bl", false, f)});
  CHECK(s.find("--sport 1024 -m multiport --dports 80,443 -j DROP") !=
        std::string::npos);
}

TEST_CASE("build_ipt_script: oversized positive port lists cross product") {
  ProtoPortFilter f;
  f.proto = L4Proto::Tcp;
  f.src_port = "1,3,5,7,9,11,13,15,17,19,21,23,25,27,29,31";
  f.dst_port =
      "101,103,105,107,109,111,113,115,117,119,121,123,125,127,129,131";
  auto s = T::build_ipt_script(false, {drop_rule("bl", false, f)});
  const std::string src_a =
      "-m multiport --sports 1,3,5,7,9,11,13,15,17,19,21,23,25,27,29";
  const std::string src_b = "--sport 31";
  const std::string dst_a =
      "-m multiport --dports "
      "101,103,105,107,109,111,113,115,117,119,121,123,125,127,129";
  const std::string dst_b = "--dport 131";
  CHECK(s.find(src_a + " " + dst_a + " -j DROP") != std::string::npos);
  CHECK(s.find(src_a + " " + dst_b + " -j DROP") != std::string::npos);
  CHECK(s.find(src_b + " " + dst_a + " -j DROP") != std::string::npos);
  CHECK(s.find(src_b + " " + dst_b + " -j DROP") != std::string::npos);
}

TEST_CASE("build_ipt_script: negated chunks combine with positive alternatives") {
  ProtoPortFilter f;
  f.proto = L4Proto::Udp;
  f.src_port = "1,3,5,7,9,11,13,15,17,19,21,23,25,27,29,31";
  f.negate_src_port = true;
  f.dst_port =
      "101,103,105,107,109,111,113,115,117,119,121,123,125,127,129,131";
  auto s = T::build_ipt_script(false, {drop_rule("bl", false, f)});
  const std::string excluded =
      "-m multiport ! --sports 1,3,5,7,9,11,13,15,17,19,21,23,25,27,29 "
      "! --sport 31";
  CHECK(s.find(excluded + " -m multiport --dports "
                          "101,103,105,107,109,111,113,115,117,119,121,123,125,127,129"
                          " -j DROP") != std::string::npos);
  CHECK(s.find(excluded + " --dport 131 -j DROP") !=
        std::string::npos);
}

TEST_CASE("build_ipt_script: positive chunks combine with negated destination") {
  ProtoPortFilter f;
  f.proto = L4Proto::Udp;
  f.src_port = "1,3,5,7,9,11,13,15,17,19,21,23,25,27,29,31";
  f.dst_port =
      "101,103,105,107,109,111,113,115,117,119,121,123,125,127,129,131";
  f.negate_dst_port = true;
  auto s = T::build_ipt_script(false, {drop_rule("bl", false, f)});
  const std::string excluded =
      "-m multiport ! --dports "
      "101,103,105,107,109,111,113,115,117,119,121,123,125,127,129 "
      "! --dport 131";
  CHECK(s.find("-m multiport --sports "
               "1,3,5,7,9,11,13,15,17,19,21,23,25,27,29 " +
               excluded + " -j DROP") != std::string::npos);
  CHECK(s.find("--sport 31 " + excluded + " -j DROP") !=
        std::string::npos);
}

TEST_CASE(
    "build_ipt_script: any proto + src_port expands to tcp and udp rules") {
  ProtoPortFilter f;
  f.proto = L4Proto::Any;
  f.src_port = "11111";
  auto s = T::build_ipt_script(false, {mark_rule("myset", false, 0x100, f)});
  CHECK(s.find("-A KeenPbrTable -m set --match-set myset dst -p tcp --sport "
               "11111 -j MARK --set-xmark 0x100/0xffffffff") !=
        std::string::npos);
  CHECK(s.find("-A KeenPbrTable -m set --match-set myset dst -p udp --sport "
               "11111 -j MARK --set-xmark 0x100/0xffffffff") !=
        std::string::npos);
  CHECK(s.find("-A KeenPbrTable -m set --match-set myset dst --sport 11111") ==
        std::string::npos);
}

TEST_CASE(
    "build_ipt_script: no proto, no ports → no extra flags (regression)") {
  auto s = T::build_ipt_script(false, {mark_rule("myset", false, 0x100)});
  CHECK(s.find("-A KeenPbrTable -m set --match-set myset dst -j MARK "
               "--set-xmark 0x100/0xffffffff") != std::string::npos);
  CHECK(s.find("-p ") == std::string::npos);
  CHECK(s.find("--dport") == std::string::npos);
}

// =============================================================================
// build_ipt_script with src_addr / dest_addr tests
// =============================================================================

TEST_CASE("build_ipt_script: single src_addr → -s flag") {
  ProtoPortFilter f;
  f.src_addr = {"192.168.10.0/24"};
  auto s = T::build_ipt_script(false, {mark_rule("myset", false, 0x100, f)});
  CHECK(s.find("-A KeenPbrTable -m set --match-set myset dst -s "
               "192.168.10.0/24 -j MARK --set-xmark 0x100/0xffffffff") !=
        std::string::npos);
}

TEST_CASE("build_ipt_script: single dest_addr → -d flag") {
  ProtoPortFilter f;
  f.dst_addr = {"10.0.0.0/8"};
  auto s = T::build_ipt_script(false, {mark_rule("myset", false, 0x100, f)});
  CHECK(s.find("-A KeenPbrTable -m set --match-set myset dst -d 10.0.0.0/8 -j "
               "MARK --set-xmark 0x100/0xffffffff") != std::string::npos);
}

TEST_CASE("build_ipt_script: src_addr + dest_addr → both flags") {
  ProtoPortFilter f;
  f.src_addr = {"192.168.1.0/24"};
  f.dst_addr = {"8.8.8.0/24"};
  auto s = T::build_ipt_script(false, {mark_rule("myset", false, 0x100, f)});
  CHECK(s.find("-A KeenPbrTable -m set --match-set myset dst -s 192.168.1.0/24 "
               "-d 8.8.8.0/24 -j MARK --set-xmark 0x100/0xffffffff") !=
        std::string::npos);
}

TEST_CASE("build_ipt_script: src_addr + tcp/udp + dest_port → addr and proto "
          "present") {
  ProtoPortFilter f;
  f.src_addr = {"192.168.1.0/24"};
  f.proto = L4Proto::Tcp;
  f.dst_port = "443";
  auto s = T::build_ipt_script(false, {mark_rule("myset", false, 0x100, f)});
  CHECK(s.find("-A KeenPbrTable -m set --match-set myset dst -s 192.168.1.0/24 "
               "-p tcp --dport 443 -j MARK --set-xmark 0x100/0xffffffff") !=
        std::string::npos);
}

TEST_CASE("build_ipt_script: drop rule with src_addr → -s flag on DROP") {
  ProtoPortFilter f;
  f.src_addr = {"10.10.0.0/16"};
  auto s = T::build_ipt_script(false, {drop_rule("bl", false, f)});
  CHECK(s.find("-A KeenPbrTable -m set --match-set bl dst -s 10.10.0.0/16 -j "
               "DROP") != std::string::npos);
}

// =============================================================================
// build_proto_port_fragment negation tests
// =============================================================================

TEST_CASE(
    "build_proto_port_fragment: negated dest_port (single) → ! --dport 443") {
  auto frag = T::build_proto_port_fragment("tcp", "", "443", false, true);
  CHECK(frag == " -p tcp ! --dport 443");
}

TEST_CASE(
    "build_proto_port_fragment: negated port range → ! --dport 8000:9000") {
  auto frag = T::build_proto_port_fragment("udp", "", "8000-9000", false, true);
  CHECK(frag == " -p udp ! --dport 8000:9000");
}

TEST_CASE("build_proto_port_fragment: negated multiport list → -m multiport ! "
          "--dports 80,443") {
  auto frag = T::build_proto_port_fragment("tcp", "", "80,443", false, true);
  CHECK(frag == " -p tcp -m multiport ! --dports 80,443");
}

TEST_CASE("build_proto_port_fragment: negated src_port only → ! --sport 1024") {
  auto frag = T::build_proto_port_fragment("tcp", "1024", "", true, false);
  CHECK(frag == " -p tcp ! --sport 1024");
}

TEST_CASE("build_proto_port_fragment: both ports negated → sport and dport") {
  auto frag =
      T::build_proto_port_fragment("tcp", "1024-65535", "80", true, true);
  CHECK(frag == " -p tcp ! --sport 1024:65535 ! --dport 80");
}

TEST_CASE("build_proto_port_fragment: mixed negation → sport and dport") {
  auto frag = T::build_proto_port_fragment("tcp", "1024", "443", true, false);
  CHECK(frag == " -p tcp ! --sport 1024 --dport 443");
}

// =============================================================================
// build_ipt_script negation tests
// =============================================================================

TEST_CASE("build_ipt_script: negated src_addr → ! -s flag") {
  ProtoPortFilter f;
  f.src_addr = {"192.168.1.0/24"};
  f.negate_src_addr = true;
  auto s = T::build_ipt_script(false, {mark_rule("myset", false, 0x100, f)});
  CHECK(s.find("-A KeenPbrTable -m set --match-set myset dst ! -s "
               "192.168.1.0/24 -j MARK --set-xmark 0x100/0xffffffff") !=
        std::string::npos);
}

TEST_CASE("build_ipt_script: negated dest_addr → ! -d flag") {
  ProtoPortFilter f;
  f.dst_addr = {"10.0.0.0/8"};
  f.negate_dst_addr = true;
  auto s = T::build_ipt_script(false, {mark_rule("myset", false, 0x100, f)});
  CHECK(s.find("-A KeenPbrTable -m set --match-set myset dst ! -d 10.0.0.0/8 "
               "-j MARK --set-xmark 0x100/0xffffffff") != std::string::npos);
}

TEST_CASE("build_ipt_script: negated dest_port in full rule") {
  ProtoPortFilter f;
  f.proto = L4Proto::Tcp;
  f.dst_port = "443";
  f.negate_dst_port = true;
  auto s = T::build_ipt_script(false, {mark_rule("myset", false, 0x100, f)});
  CHECK(s.find("-A KeenPbrTable -m set --match-set myset dst -p tcp ! --dport "
               "443 -j MARK --set-xmark 0x100/0xffffffff") !=
        std::string::npos);
}

TEST_CASE("build_ipt_script: combined negated src_addr + negated dest_port") {
  ProtoPortFilter f;
  f.src_addr = {"192.168.1.0/24"};
  f.negate_src_addr = true;
  f.proto = L4Proto::Tcp;
  f.dst_port = "443";
  f.negate_dst_port = true;
  auto s = T::build_ipt_script(false, {mark_rule("myset", false, 0x100, f)});
  CHECK(
      s.find("-A KeenPbrTable -m set --match-set myset dst ! -s 192.168.1.0/24 "
             "-p tcp ! --dport 443 -j MARK --set-xmark 0x100/0xffffffff") !=
      std::string::npos);
}

TEST_CASE("build_ipt_script: drop rule with negated src_addr") {
  ProtoPortFilter f;
  f.src_addr = {"10.10.0.0/16"};
  f.negate_src_addr = true;
  auto s = T::build_ipt_script(false, {drop_rule("bl", false, f)});
  CHECK(s.find("-A KeenPbrTable -m set --match-set bl dst ! -s 10.10.0.0/16 -j "
               "DROP") != std::string::npos);
}

// =============================================================================
// Multiple CIDR / port negation tests
// (expand_and_push emits one rule per CIDR; each carries the shared negate
// flag)
// =============================================================================

TEST_CASE(
    "build_ipt_script: two negated src_addrs → two rules each with ! -s") {
  // Simulate what expand_and_push produces for negate_src_addr + two CIDRs
  ProtoPortFilter f1;
  f1.src_addr = {"192.168.1.0/24"};
  f1.negate_src_addr = true;
  ProtoPortFilter f2;
  f2.src_addr = {"10.0.0.0/8"};
  f2.negate_src_addr = true;
  auto s = T::build_ipt_script(false, {mark_rule("myset", false, 0x100, f1),
                                       mark_rule("myset", false, 0x100, f2)});
  CHECK(s.find("! -s 192.168.1.0/24") != std::string::npos);
  CHECK(s.find("! -s 10.0.0.0/8") != std::string::npos);
  // Both are mark rules
  CHECK(s.find("! -s 192.168.1.0/24 -j MARK") != std::string::npos);
  CHECK(s.find("! -s 10.0.0.0/8 -j MARK") != std::string::npos);
}

TEST_CASE(
    "build_ipt_script: two negated dst_addrs → two rules each with ! -d") {
  ProtoPortFilter f1;
  f1.dst_addr = {"8.8.8.0/24"};
  f1.negate_dst_addr = true;
  ProtoPortFilter f2;
  f2.dst_addr = {"1.1.1.0/24"};
  f2.negate_dst_addr = true;
  auto s = T::build_ipt_script(
      false, {drop_rule("bl", false, f1), drop_rule("bl", false, f2)});
  CHECK(s.find("! -d 8.8.8.0/24") != std::string::npos);
  CHECK(s.find("! -d 1.1.1.0/24") != std::string::npos);
}

TEST_CASE("build_proto_port_fragment: negated src port list → -m multiport ! "
          "--sports 80,8080") {
  auto frag = T::build_proto_port_fragment("tcp", "80,8080", "", true, false);
  CHECK(frag == " -p tcp -m multiport ! --sports 80,8080");
}

TEST_CASE(
    "build_proto_port_fragment: negated src port range → ! --sport 8000:9000") {
  auto frag = T::build_proto_port_fragment("tcp", "8000-9000", "", true, false);
  CHECK(frag == " -p tcp ! --sport 8000:9000");
}

// =============================================================================
// Mixed negation documentation tests
// (current design: negation is per-list, determined by the first element)
// =============================================================================

TEST_CASE(
    "build_ipt_script: non-negated and negated src_addrs in separate rules") {
  // A non-negated CIDR and a negated CIDR produce independent rules — each can
  // match different traffic, so there is no contradiction.
  ProtoPortFilter fpos;
  fpos.src_addr = {"172.16.0.0/12"};
  ProtoPortFilter fneg;
  fneg.src_addr = {"10.0.0.0/8"};
  fneg.negate_src_addr = true;
  auto s = T::build_ipt_script(false, {mark_rule("myset", false, 0x100, fpos),
                                       mark_rule("myset", false, 0x100, fneg)});
  CHECK(s.find(" -s 172.16.0.0/12") != std::string::npos);
  CHECK(s.find("! -s 10.0.0.0/8") != std::string::npos);
}

// =============================================================================
// Static / dynamic set split tests
// =============================================================================

TEST_CASE("static set naming: kpbr4s_ prefix, no timeout") {
  auto line = T::build_ipset_create_line("kpbr4s_mylist", "inet", 0);
  CHECK(line == "create kpbr4s_mylist hash:net family inet -exist\n");
}

TEST_CASE("dynamic set naming: kpbr4d_ prefix, no timeout when ttl_ms=0") {
  auto line = T::build_ipset_create_line("kpbr4d_mylist", "inet", 0);
  CHECK(line == "create kpbr4d_mylist hash:net family inet -exist\n");
}

TEST_CASE("dynamic set naming: kpbr4d_ prefix, with timeout when ttl_ms set") {
  auto line = T::build_ipset_create_line("kpbr4d_mylist", "inet", 3600);
  CHECK(line ==
        "create kpbr4d_mylist hash:net family inet timeout 3600 -exist\n");
}

TEST_CASE("dynamic set naming: kpbr6d_ IPv6 with timeout") {
  auto line = T::build_ipset_create_line("kpbr6d_mylist", "inet6", 86400);
  CHECK(line ==
        "create kpbr6d_mylist hash:net family inet6 timeout 86400 -exist\n");
}

TEST_CASE("dual-set mark rules: both static and dynamic sets get mark rules") {
  auto s =
      T::build_ipt_script(false, {mark_rule("kpbr4_mylist", false, 0x100),
                                  mark_rule("kpbr4d_mylist", false, 0x100)});
  CHECK(s.find("--match-set kpbr4_mylist dst -j MARK --set-xmark "
               "0x100/0xffffffff") != std::string::npos);
  CHECK(s.find("--match-set kpbr4d_mylist dst -j MARK --set-xmark "
               "0x100/0xffffffff") != std::string::npos);
}

TEST_CASE("dual-set drop rules: both static and dynamic sets get drop rules") {
  auto s = T::build_ipt_script(false, {drop_rule("kpbr4_mylist", false),
                                       drop_rule("kpbr4d_mylist", false)});
  CHECK(s.find("--match-set kpbr4_mylist dst -j DROP") != std::string::npos);
  CHECK(s.find("--match-set kpbr4d_mylist dst -j DROP") != std::string::npos);
}

TEST_CASE("dual-set IPv6 mark rules: kpbr6_ and kpbr6d_ both matched") {
  auto s = T::build_ipt_script(true, {mark_rule("kpbr6_mylist", true, 0x200),
                                      mark_rule("kpbr6d_mylist", true, 0x200)});
  CHECK(s.find("--match-set kpbr6_mylist dst -j MARK --set-xmark "
               "0x200/0xffffffff") != std::string::npos);
  CHECK(s.find("--match-set kpbr6d_mylist dst -j MARK --set-xmark "
               "0x200/0xffffffff") != std::string::npos);
}

// Helper for direct (no-set) mark rules
static Rule direct_mark_rule(bool ipv6, uint32_t fwmark,
                             ProtoPortFilter filter = {}) {
  Rule r;
  r.set_name = "";
  r.ipv6 = ipv6;
  r.direct = true;
  r.action = Rule::Mark;
  r.fwmark = fwmark;
  r.filter = filter;
  return r;
}

// =============================================================================
// create_direct_mark_rule / build_ipt_script with direct=true tests
// =============================================================================

TEST_CASE("build_ipt_script: direct mark rule IPv4 UDP dst port 53") {
  ProtoPortFilter f;
  f.proto = L4Proto::Udp;
  f.dst_port = "53";
  f.dst_addr = {"10.8.0.1"};
  auto s = T::build_ipt_script(false, {direct_mark_rule(false, 0x10000, f)});
  // Must NOT contain --match-set
  CHECK(s.find("--match-set") == std::string::npos);
  // Must contain dst addr and port
  CHECK(s.find("-d 10.8.0.1") != std::string::npos);
  CHECK(s.find("--dport 53") != std::string::npos);
  CHECK(s.find("-j MARK --set-xmark 0x10000/0xffffffff") != std::string::npos);
}

TEST_CASE("build_ipt_script: direct mark rule IPv4 TCP dst port 53") {
  ProtoPortFilter f;
  f.proto = L4Proto::Tcp;
  f.dst_port = "53";
  f.dst_addr = {"10.8.0.1"};
  auto s = T::build_ipt_script(false, {direct_mark_rule(false, 0x10000, f)});
  CHECK(s.find("--match-set") == std::string::npos);
  CHECK(s.find("-d 10.8.0.1") != std::string::npos);
  CHECK(s.find("-p tcp --dport 53") != std::string::npos);
  CHECK(s.find("-j MARK --set-xmark 0x10000/0xffffffff") != std::string::npos);
}

TEST_CASE("build_ipt_script: direct mark rule has no set_name reference") {
  ProtoPortFilter f;
  f.proto = L4Proto::Udp;
  f.dst_addr = {"192.0.2.1"};
  auto s = T::build_ipt_script(false, {direct_mark_rule(false, 0x20000, f)});
  CHECK(s.find("--match-set") == std::string::npos);
  CHECK(s.find("-d 192.0.2.1") != std::string::npos);
}

namespace {

enum class PairwiseRuleMode {
  ListBacked,
  Direct,
};

enum class PairwiseAction {
  Mark,
  Drop,
  Pass,
};

struct ProtoVariant {
  const char *name;
  L4Proto proto;
};

enum class PortShape {
  Empty,
  Single,
  Multi,
  Range,
};

struct PortVariant {
  const char *name;
  PortShape shape;
  const char *spec;
  const char *iptables_spec;
  bool negated;
};

struct AddrVariant {
  const char *name;
  std::vector<std::string> addrs;
  bool negated;
};

struct PairwiseIptablesCase {
  std::string name;
  PairwiseRuleMode mode;
  PairwiseAction action;
  ProtoVariant proto;
  PortVariant src_port;
  PortVariant dst_port;
  AddrVariant src_addr;
  AddrVariant dst_addr;
};

constexpr std::array<ProtoVariant, 4> kProtoVariants{{
    {"any", L4Proto::Any},
    {"tcp", L4Proto::Tcp},
    {"udp", L4Proto::Udp},
    {"tcp_udp", L4Proto::TcpUdp},
}};

constexpr std::array<PortVariant, 7> kPortVariants{{
    {"empty", PortShape::Empty, "", "", false},
    {"single", PortShape::Single, "443", "443", false},
    {"multi", PortShape::Multi, "80,443", "80,443", false},
    {"range", PortShape::Range, "8000-9000", "8000:9000", false},
    {"neg_single", PortShape::Single, "53", "53", true},
    {"neg_multi", PortShape::Multi, "53,123", "53,123", true},
    {"neg_range", PortShape::Range, "10000-10010", "10000:10010", true},
}};

const std::array<AddrVariant, 5> kAddrVariants{{
    {"empty", {}, false},
    {"single", {"192.0.2.0/24"}, false},
    {"multi", {"192.0.2.0/24", "198.51.100.0/24"}, false},
    {"neg_single", {"203.0.113.0/24"}, true},
    {"neg_multi", {"203.0.113.0/24", "198.18.0.0/15"}, true},
}};

constexpr std::array<const char *, 2> kModeNames{{"list", "direct"}};
constexpr std::array<const char *, 3> kActionNames{{"mark", "drop", "pass"}};

using PairwiseIndex = std::array<size_t, 7>;

size_t selector_count(const PairwiseIptablesCase &tc) {
  size_t count = 0;
  count += tc.src_port.shape != PortShape::Empty ? 1 : 0;
  count += tc.dst_port.shape != PortShape::Empty ? 1 : 0;
  count += tc.src_addr.addrs.empty() ? 0 : 1;
  count += tc.dst_addr.addrs.empty() ? 0 : 1;
  return count;
}

bool has_negated_selector(const PairwiseIptablesCase &tc) {
  return tc.src_port.negated || tc.dst_port.negated || tc.src_addr.negated ||
         tc.dst_addr.negated;
}

bool has_positive_selector(const PairwiseIptablesCase &tc) {
  return (tc.src_port.shape != PortShape::Empty && !tc.src_port.negated) ||
         (tc.dst_port.shape != PortShape::Empty && !tc.dst_port.negated) ||
         (!tc.src_addr.addrs.empty() && !tc.src_addr.negated) ||
         (!tc.dst_addr.addrs.empty() && !tc.dst_addr.negated);
}

std::string pairwise_combo_name(const PairwiseIndex &idx) {
  std::ostringstream os;
  os << kModeNames[idx[0]] << "__" << kActionNames[idx[1]] << "__"
     << kProtoVariants[idx[2]].name << "__srcp_" << kPortVariants[idx[3]].name
     << "__dstp_" << kPortVariants[idx[4]].name << "__srca_"
     << kAddrVariants[idx[5]].name << "__dsta_" << kAddrVariants[idx[6]].name;
  return os.str();
}

FirewallRuleCriteria build_pairwise_filter(const PairwiseIptablesCase &tc) {
  FirewallRuleCriteria filter;
  filter.proto = tc.proto.proto;
  filter.src_port = tc.src_port.spec;
  filter.dst_port = tc.dst_port.spec;
  filter.src_addr = tc.src_addr.addrs;
  filter.dst_addr = tc.dst_addr.addrs;
  filter.negate_src_port = tc.src_port.negated;
  filter.negate_dst_port = tc.dst_port.negated;
  filter.negate_src_addr = tc.src_addr.negated;
  filter.negate_dst_addr = tc.dst_addr.negated;
  return filter;
}

std::string format_fwmark(uint32_t fwmark) {
  std::ostringstream os;
  os << "0x" << std::hex << std::nouppercase << fwmark;
  return os.str();
}

std::vector<L4Proto> expand_proto(L4Proto proto, const PortVariant &src_port,
                                  const PortVariant &dst_port) {
  if (proto == L4Proto::Any && (src_port.shape != PortShape::Empty ||
                                dst_port.shape != PortShape::Empty)) {
    return {L4Proto::Tcp, L4Proto::Udp};
  }
  if (proto == L4Proto::TcpUdp) {
    return {L4Proto::Tcp, L4Proto::Udp};
  }
  return {proto};
}

std::string expected_proto_port_fragment(L4Proto proto,
                                         const PortVariant &src_port,
                                         const PortVariant &dst_port) {
  if (proto == L4Proto::Any && src_port.shape == PortShape::Empty &&
      dst_port.shape == PortShape::Empty) {
    return "";
  }

  std::string frag;
  if (proto != L4Proto::Any) {
    frag += " -p ";
    frag += l4_proto_name(proto);
  }

  const bool has_src = src_port.shape != PortShape::Empty;
  const bool has_dst = dst_port.shape != PortShape::Empty;
  const bool src_list = src_port.shape == PortShape::Multi;
  const bool dst_list = dst_port.shape == PortShape::Multi;

  if (has_src || has_dst) {
    if (src_list || dst_list) {
      if (src_list) {
        frag += " -m multiport";
        if (src_port.negated)
          frag += " !";
        frag += " --sports ";
        frag += src_port.iptables_spec;
      } else if (has_src) {
        if (src_port.negated)
          frag += " !";
        frag += " --sport ";
        frag += src_port.iptables_spec;
      }
      if (dst_list) {
        frag += " -m multiport";
        if (dst_port.negated)
          frag += " !";
        frag += " --dports ";
        frag += dst_port.iptables_spec;
      } else if (has_dst) {
        if (dst_port.negated)
          frag += " !";
        frag += " --dport ";
        frag += dst_port.iptables_spec;
      }
    } else {
      if (has_src) {
        if (src_port.negated)
          frag += " !";
        frag += " --sport ";
        frag += src_port.iptables_spec;
      }
      if (has_dst) {
        if (dst_port.negated)
          frag += " !";
        frag += " --dport ";
        frag += dst_port.iptables_spec;
      }
    }
  }

  return frag;
}

std::vector<std::string> expected_rule_lines(const PairwiseIptablesCase &tc,
                                             uint32_t fwmark) {
  std::vector<std::string> lines;
  const std::vector<std::string> src_addrs = tc.src_addr.addrs.empty()
                                                 ? std::vector<std::string>{""}
                                                 : tc.src_addr.addrs;
  const std::vector<std::string> dst_addrs = tc.dst_addr.addrs.empty()
                                                 ? std::vector<std::string>{""}
                                                 : tc.dst_addr.addrs;

  for (L4Proto proto : expand_proto(tc.proto.proto, tc.src_port, tc.dst_port)) {
    const std::string proto_port_frag =
        expected_proto_port_fragment(proto, tc.src_port, tc.dst_port);
    for (const auto &src_addr : src_addrs) {
      for (const auto &dst_addr : dst_addrs) {
        std::string prefix = "-A KeenPbrTable";
        if (tc.mode == PairwiseRuleMode::ListBacked) {
          prefix += " -m set --match-set pairwise_set dst";
        }

        if (!src_addr.empty()) {
          prefix += tc.src_addr.negated ? " ! -s " : " -s ";
          prefix += src_addr;
        }
        if (!dst_addr.empty()) {
          prefix += tc.dst_addr.negated ? " ! -d " : " -d ";
          prefix += dst_addr;
        }

        prefix += proto_port_frag;

        if (tc.action == PairwiseAction::Mark) {
          lines.push_back(prefix + " -j MARK --set-xmark " +
                          format_fwmark(fwmark) + "/0xffffffff" + "\n");
          lines.push_back(prefix + " -j RETURN\n");
        } else if (tc.action == PairwiseAction::Drop) {
          lines.push_back(prefix + " -j DROP\n");
        } else {
          lines.push_back(prefix + " -j RETURN\n");
        }
      }
    }
  }

  return lines;
}

std::vector<std::string> extract_rule_lines(const std::string &script) {
  std::vector<std::string> lines;
  std::istringstream input(script);
  std::string line;
  while (std::getline(input, line)) {
    if (line.rfind("-A KeenPbrTable", 0) == 0) {
      lines.push_back(line + "\n");
    }
  }
  return lines;
}

std::set<std::string> build_uncovered_pairs() {
  const std::array<size_t, 7> axis_sizes{
      kModeNames.size(),    kActionNames.size(),  kProtoVariants.size(),
      kPortVariants.size(), kPortVariants.size(), kAddrVariants.size(),
      kAddrVariants.size(),
  };

  std::set<std::string> uncovered;
  for (size_t a = 0; a < axis_sizes.size(); ++a) {
    for (size_t b = a + 1; b < axis_sizes.size(); ++b) {
      for (size_t va = 0; va < axis_sizes[a]; ++va) {
        for (size_t vb = 0; vb < axis_sizes[b]; ++vb) {
          uncovered.insert(std::to_string(a) + ":" + std::to_string(va) + "|" +
                           std::to_string(b) + ":" + std::to_string(vb));
        }
      }
    }
  }
  return uncovered;
}

std::vector<std::string> coverage_keys(const PairwiseIndex &idx) {
  std::vector<std::string> keys;
  for (size_t a = 0; a < idx.size(); ++a) {
    for (size_t b = a + 1; b < idx.size(); ++b) {
      keys.push_back(std::to_string(a) + ":" + std::to_string(idx[a]) + "|" +
                     std::to_string(b) + ":" + std::to_string(idx[b]));
    }
  }
  return keys;
}

std::vector<PairwiseIndex> generate_pairwise_indices() {
  std::vector<PairwiseIndex> all_combos;
  for (size_t mode = 0; mode < kModeNames.size(); ++mode) {
    for (size_t action = 0; action < kActionNames.size(); ++action) {
      for (size_t proto = 0; proto < kProtoVariants.size(); ++proto) {
        for (size_t src_port = 0; src_port < kPortVariants.size(); ++src_port) {
          for (size_t dst_port = 0; dst_port < kPortVariants.size();
               ++dst_port) {
            for (size_t src_addr = 0; src_addr < kAddrVariants.size();
                 ++src_addr) {
              for (size_t dst_addr = 0; dst_addr < kAddrVariants.size();
                   ++dst_addr) {
                all_combos.push_back({mode, action, proto, src_port, dst_port,
                                      src_addr, dst_addr});
              }
            }
          }
        }
      }
    }
  }

  std::set<std::string> uncovered = build_uncovered_pairs();
  std::vector<PairwiseIndex> selected;
  std::set<std::string> seen;

  const std::vector<PairwiseIndex> seeds{
      {0, 0, 1, 0, 1, 0, 0},
      {1, 1, 2, 0, 3, 1, 0},
      {0, 2, 3, 5, 1, 3, 2},
      {1, 0, 1, 4, 2, 3, 1},
  };

  auto add_combo = [&](const PairwiseIndex &combo) {
    const std::string key = pairwise_combo_name(combo);
    if (!seen.insert(key).second) {
      return;
    }
    selected.push_back(combo);
    for (const auto &coverage : coverage_keys(combo)) {
      uncovered.erase(coverage);
    }
  };

  for (const auto &seed : seeds) {
    add_combo(seed);
  }

  while (!uncovered.empty()) {
    size_t best_score = 0;
    size_t best_index = 0;
    for (size_t i = 0; i < all_combos.size(); ++i) {
      if (seen.count(pairwise_combo_name(all_combos[i])) != 0) {
        continue;
      }
      size_t score = 0;
      for (const auto &coverage : coverage_keys(all_combos[i])) {
        score += uncovered.count(coverage);
      }
      if (score > best_score) {
        best_score = score;
        best_index = i;
      }
    }
    add_combo(all_combos[best_index]);
  }

  return selected;
}

std::vector<PairwiseIptablesCase> generate_pairwise_cases() {
  std::vector<PairwiseIptablesCase> cases;
  for (const auto &idx : generate_pairwise_indices()) {
    cases.push_back({
        pairwise_combo_name(idx),
        idx[0] == 0 ? PairwiseRuleMode::ListBacked : PairwiseRuleMode::Direct,
        idx[1] == 0
            ? PairwiseAction::Mark
            : (idx[1] == 1 ? PairwiseAction::Drop : PairwiseAction::Pass),
        kProtoVariants[idx[2]],
        kPortVariants[idx[3]],
        kPortVariants[idx[4]],
        kAddrVariants[idx[5]],
        kAddrVariants[idx[6]],
    });
  }
  return cases;
}

bool pairwise_is_complete(const std::vector<PairwiseIndex> &cases) {
  std::set<std::string> uncovered = build_uncovered_pairs();
  for (const auto &combo : cases) {
    for (const auto &coverage : coverage_keys(combo)) {
      uncovered.erase(coverage);
    }
  }
  return uncovered.empty();
}

} // namespace
