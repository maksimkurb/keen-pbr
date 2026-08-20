#include <doctest/doctest.h>

#include "../src/config/config.hpp"
#include "../src/config/routing_state.hpp"
#include "../src/firewall/ipset_restore_pipe.hpp"
#include "../src/firewall/iptables.hpp"
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
  // Public mirror of PendingRule for use in test functions.
  struct RuleDesc {
    std::string set_name;
    bool ipv6;
    bool direct = false;
    enum Action { Mark, Drop, Pass } action;
    uint32_t fwmark;
    ProtoPortFilter filter;
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
    firewall.target_v4_generation_ = generation;
    firewall.target_v6_generation_ = generation;
    return firewall.static_set_name(name, family);
  }

  static std::string static_set_name_for_live_rules(const std::string &rules) {
    IptablesFirewall firewall;
    const auto state = IptablesFirewall::parse_live_generation(
        rules, "KeenPbrTable", "KeenPbrTable_A", "KeenPbrTable_B");
    firewall.target_v4_generation_ =
        IptablesFirewall::target_generation_for_states(
            state, IptablesFirewall::LiveGenerationState::Missing);
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
    return firewall.prerouting_dispatcher_chain_name(ipv6);
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

  static std::string build_ipt_script(bool ipv6,
                                      const std::vector<RuleDesc> &descs,
                                      FirewallGlobalPrefilter prefilter = {}) {
    std::vector<IptablesFirewall::PendingRule> rules;
    rules.reserve(descs.size());
    for (const auto &d : descs) {
      IptablesFirewall::PendingRule pr;
      pr.ipv6 = d.ipv6;
      if (d.action == RuleDesc::Mark) {
        pr.action = IptablesFirewall::PendingRule::Mark;
      } else if (d.action == RuleDesc::Drop) {
        pr.action = IptablesFirewall::PendingRule::Drop;
      } else {
        pr.action = IptablesFirewall::PendingRule::Pass;
      }
      pr.fwmark = d.fwmark;
      pr.criteria = d.filter;
      if (!d.set_name.empty()) {
        pr.criteria.dst_set_name = d.set_name;
      }
      rules.push_back(std::move(pr));
    }
    return IptablesFirewall::build_ipt_script(
        ipv6, FirewallSetGeneration::A, rules, prefilter);
  }

  static std::string build_replacement_script() {
    return IptablesFirewall::build_ipt_script(false, FirewallSetGeneration::B,
                                              {}, {});
  }

  static std::string build_raw_script(const std::vector<RuleDesc> &descs,
                                      FirewallGlobalPrefilter prefilter = {}) {
    return build_raw_script_for_family(false, descs, prefilter);
  }

  static std::string
  build_raw_script_for_family(bool ipv6, const std::vector<RuleDesc> &descs,
                              FirewallGlobalPrefilter prefilter = {}) {
    std::vector<IptablesFirewall::PendingRule> rules;
    for (const auto &d : descs) {
      IptablesFirewall::PendingRule pr;
      pr.ipv6 = d.ipv6;
      pr.action =
          d.action == RuleDesc::Mark   ? IptablesFirewall::PendingRule::Mark
          : d.action == RuleDesc::Drop ? IptablesFirewall::PendingRule::Drop
                                       : IptablesFirewall::PendingRule::Pass;
      pr.fwmark = d.fwmark;
      pr.criteria = d.filter;
      if (!d.set_name.empty())
        pr.criteria.dst_set_name = d.set_name;
      rules.push_back(std::move(pr));
    }
    return IptablesFirewall::build_raw_prerouting_script(
        ipv6, FirewallSetGeneration::A, rules, prefilter);
  }

  static std::string
  build_raw_script_for_generation(FirewallSetGeneration generation,
                                  const std::vector<RuleDesc> &descs = {}) {
    std::vector<IptablesFirewall::PendingRule> rules;
    for (const auto &d : descs) {
      IptablesFirewall::PendingRule pr;
      pr.ipv6 = d.ipv6;
      pr.action =
          d.action == RuleDesc::Mark   ? IptablesFirewall::PendingRule::Mark
          : d.action == RuleDesc::Drop ? IptablesFirewall::PendingRule::Drop
                                       : IptablesFirewall::PendingRule::Pass;
      pr.fwmark = d.fwmark;
      pr.criteria = d.filter;
      if (!d.set_name.empty())
        pr.criteria.dst_set_name = d.set_name;
      rules.push_back(std::move(pr));
    }
    return IptablesFirewall::build_raw_prerouting_script(generation, rules, {});
  }

  static std::string
  build_output_script_for_generation(FirewallSetGeneration generation) {
    return IptablesFirewall::build_output_script(generation, {}, {});
  }

  static std::string build_output_script_for_family(
      bool ipv6, const std::vector<RuleDesc> &descs,
      FirewallGlobalPrefilter prefilter = {}) {
    std::vector<IptablesFirewall::PendingRule> rules;
    for (const auto &d : descs) {
      IptablesFirewall::PendingRule pr;
      pr.ipv6 = d.ipv6;
      pr.action = d.action == RuleDesc::Mark
                      ? IptablesFirewall::PendingRule::Mark
                      : d.action == RuleDesc::Drop
                            ? IptablesFirewall::PendingRule::Drop
                            : IptablesFirewall::PendingRule::Pass;
      pr.fwmark = d.fwmark;
      pr.criteria = d.filter;
      if (!d.set_name.empty()) pr.criteria.dst_set_name = d.set_name;
      rules.push_back(std::move(pr));
    }
    return IptablesFirewall::build_output_script(
        ipv6, FirewallSetGeneration::A, rules, prefilter);
  }

  static int
  live_generation_state(const std::string &rules,
                        const std::string &dispatcher = "KeenPbrTable") {
    return static_cast<int>(IptablesFirewall::parse_live_generation(
        rules, dispatcher, "KeenPbrTable_A", "KeenPbrTable_B"));
  }

  static int live_generation_state_for_dispatcher(
      const std::string &rules, const std::string &dispatcher,
      const std::string &generation_a, const std::string &generation_b) {
    return static_cast<int>(IptablesFirewall::parse_live_generation(
        rules, dispatcher, generation_a, generation_b));
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

  static int static_set_generation(const std::string &rules, bool ipv6 = false) {
    return static_cast<int>(IptablesFirewall::parse_static_set_references(
                                rules, ipv6)
                                .generation);
  }

  static std::set<std::string> static_set_names(const std::string &rules,
                                                bool ipv6 = false) {
    return IptablesFirewall::parse_static_set_references(rules, ipv6).names;
  }

  static FirewallSetGeneration static_target_for_mode(
      FirewallApplyMode mode, int live_static,
      FirewallSetGeneration rule_target) {
    return IptablesFirewall::static_target_for_mode(
        mode,
        static_cast<IptablesFirewall::LiveGenerationState>(live_static),
        rule_target);
  }

  static std::string static_name_for_generation(FirewallSetGeneration generation,
                                                const std::string &name,
                                                int family) {
    return IptablesFirewall::static_set_name_for_generation(name, family,
                                                             generation);
  }

  static FirewallSetGeneration target_for_states(int primary, int secondary) {
    return IptablesFirewall::target_generation_for_states(
        static_cast<IptablesFirewall::LiveGenerationState>(primary),
        static_cast<IptablesFirewall::LiveGenerationState>(secondary));
  }

  static FirewallSetGeneration plan_target_for_states(int primary,
                                                      int secondary) {
    return IptablesFirewall::generation_plan_for_states(
               static_cast<IptablesFirewall::LiveGenerationState>(primary),
               static_cast<IptablesFirewall::LiveGenerationState>(secondary))
        .target;
  }

  static bool plan_repairs_output(int primary, int secondary) {
    return IptablesFirewall::generation_plan_for_states(
               static_cast<IptablesFirewall::LiveGenerationState>(primary),
               static_cast<IptablesFirewall::LiveGenerationState>(secondary))
        .repair_output;
  }

  static std::string build_rules_for_slots(FirewallSetGeneration rule_generation,
                                            FirewallSetGeneration set_generation,
                                            bool ipv6) {
    IptablesFirewall firewall;
    firewall.target_v4_generation_ = rule_generation;
    firewall.target_v6_generation_ = rule_generation;
    firewall.target_static_v4_generation_ = set_generation;
    firewall.target_static_v6_generation_ = set_generation;
    firewall.static_generations_prepared_ = true;
    IptablesFirewall::PendingRule rule;
    rule.ipv6 = ipv6;
    rule.action = IptablesFirewall::PendingRule::Mark;
    rule.fwmark = 42;
    rule.criteria.dst_set_name =
        firewall.static_set_name("sample", ipv6 ? AF_INET6 : AF_INET);
    return IptablesFirewall::build_ipt_script(
        ipv6, rule_generation, {rule}, {});
  }

  static size_t count_exact_jump(const std::string &rules,
                                 const std::string &source,
                                 const std::string &target) {
    return IptablesFirewall::count_exact_jump(rules, source, target);
  }

  static std::string
  build_ipt_script_for_rule(bool ipv6, RuleDesc::Action action, uint32_t fwmark,
                            FirewallRuleCriteria criteria, bool list_backed,
                            uint32_t fwmark_mask = 0xFFFFFFFFu,
                            FirewallGlobalPrefilter prefilter = {}) {
    IptablesFirewall fw;
    fw.set_fwmark_mask(fwmark_mask);
    if (list_backed) {
      criteria.dst_set_name = "pairwise_set";
      fw.created_sets_["pairwise_set"] = ipv6 ? AF_INET6 : AF_INET;
    }

    IptablesFirewall::PendingRule::Action mapped_action =
        IptablesFirewall::PendingRule::Mark;
    if (action == RuleDesc::Drop) {
      mapped_action = IptablesFirewall::PendingRule::Drop;
    } else if (action == RuleDesc::Pass) {
      mapped_action = IptablesFirewall::PendingRule::Pass;
    }

    fw.append_rules_for_family(ipv6, mapped_action, fwmark, criteria);
    return IptablesFirewall::build_ipt_script(
        ipv6, FirewallSetGeneration::A, fw.pending_rules_, prefilter);
  }

  static std::string build_proto_port_fragment(const std::string &proto,
                                               const std::string &src_port,
                                               const std::string &dst_port,
                                               bool negate_src = false,
                                               bool negate_dst = false) {
    const auto fragments = IptablesFirewall::build_proto_port_fragments(
        parse_test_proto(proto), PortSpec(src_port), PortSpec(dst_port),
        negate_src, negate_dst);
    if (fragments.size() != 1) {
      throw std::invalid_argument(
          "Port specification requires multiple iptables rules");
    }
    return fragments.front();
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

TEST_CASE("IPv6 RAW RulesOnly references select one static-set generation") {
  const auto rules =
      "-A KeenPbrRaw -j KeenPbrRaw_A\n"
      "-A KeenPbrRaw_A -m set --match-set kpbr6s_remote dst -j RETURN\n";
  CHECK(T::static_set_generation(rules, true) == T::state_a());
  CHECK(T::static_set_names(rules, true) ==
        std::set<std::string>{"kpbr6s_remote"});
}

TEST_CASE("RAW6 A/B recovery treats PREROUTING as authoritative") {
  CHECK(T::live_generation_state_for_dispatcher(
            "-A KeenPbrRaw -j KeenPbrRaw_A\n", "KeenPbrRaw",
            "KeenPbrRaw_A", "KeenPbrRaw_B") == T::state_a());
  CHECK(T::live_generation_state_for_dispatcher(
            "-A KeenPbrRaw -j KeenPbrRaw_B\n", "KeenPbrRaw",
            "KeenPbrRaw_A", "KeenPbrRaw_B") == T::state_b());
  CHECK(T::target_for_states(T::state_a(), T::state_b()) ==
        FirewallSetGeneration::B);
  CHECK(T::target_for_states(T::state_b(), T::state_a()) ==
        FirewallSetGeneration::A);
}

TEST_CASE("raw prerouting rules use an isolated raw chain without conntrack") {
  Rule rule{"kpbr4s_minecraft", false, false, Rule::Mark, 0x100, {}};
  FirewallGlobalPrefilter prefilter;
  prefilter.restore_conntrack_mark = true;
  prefilter.conntrack_mark_mask = 0xff00;
  prefilter.skip_established_or_dnat = true;
  const std::string script = T::build_raw_script({rule}, prefilter);
  CHECK(script.find("*raw\n") != std::string::npos);
  CHECK(script.find(":KeenPbrRaw - [0:0]") != std::string::npos);
  CHECK(script.find(":KeenPbrRaw_A - [0:0]") != std::string::npos);
  CHECK(script.find(":KeenPbrRaw_B - [0:0]") == std::string::npos);
  CHECK(script.find("-A KeenPbrRaw -j KeenPbrRaw_A") != std::string::npos);
  CHECK(script.find("-A PREROUTING -j KeenPbrRaw") == std::string::npos);
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
  FirewallGlobalPrefilter prefilter;
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
  FirewallGlobalPrefilter prefilter;
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

static FirewallGlobalPrefilter
prefilter_with_interfaces(std::vector<std::string> interfaces,
                          bool skip_established_or_dnat = true) {
  FirewallGlobalPrefilter prefilter;
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

TEST_CASE("ipset reconcile: live dispatcher selects the inactive static slot") {
  CHECK(T::static_set_name_for_live_rules("") == "kpbr4s_sample");
  CHECK(T::static_set_name_for_live_rules(
            "-N KeenPbrTable\n-A KeenPbrTable -j KeenPbrTable_A\n") ==
        "kpbr4S_sample");
  CHECK(T::static_set_name_for_live_rules(
            "-N KeenPbrTable\n-A KeenPbrTable -j KeenPbrTable_B\n") ==
        "kpbr4s_sample");
  CHECK_THROWS(T::static_set_name_for_live_rules(
      "-A KeenPbrTable -j UnknownGeneration\n"));
}

TEST_CASE("RulesOnly generation plan keeps static and rule slots distinct") {
  CHECK(T::plan_target_for_states(T::state_a(), T::state_a()) ==
        FirewallSetGeneration::B);
  CHECK_FALSE(T::plan_repairs_output(T::state_a(), T::state_a()));
  CHECK(T::plan_target_for_states(T::state_a(), T::state_b()) ==
        FirewallSetGeneration::B);
  CHECK(T::plan_repairs_output(T::state_a(), T::state_b()));
  CHECK(T::plan_target_for_states(T::state_b(), T::state_a()) ==
        FirewallSetGeneration::A);
  CHECK(T::plan_repairs_output(T::state_b(), T::state_a()));
}

TEST_CASE("RulesOnly derives static slot from live match-set references") {
  CHECK(T::static_set_generation(
            "-A KeenPbrTable_B -m set --match-set kpbr4s_remote dst -j MARK\n") ==
        T::state_a());
  CHECK(T::static_set_generation(
            "-A KeenPbrTable_A -m set --match-set kpbr4S_remote dst -j MARK\n") ==
        T::state_b());
  CHECK(T::static_set_generation(
            "-A KeenPbrTable_A -m set --match-set kpbr4s_remote dst -j MARK\n"
            "-A KeenPbrTable_A -m set --match-set kpbr4s_other dst -j RETURN\n") ==
        T::state_a());
  CHECK(T::static_set_generation(
            "-A KeenPbrTable_A -m set --match-set kpbr4s_remote dst -j MARK\n"
            "-A KeenPbrTable_A -m set --match-set kpbr4S_other dst -j RETURN\n") ==
        T::state_invalid());
  CHECK(T::static_set_generation(
            "-A KeenPbrTable_A -m set --match-set kpbr4d_remote dst -j MARK\n") ==
        T::state_missing());
  CHECK(T::static_set_generation(
            "-A KeenPbrTable_B -m set --match-set kpbr6S_remote dst -j MARK\n",
            true) == T::state_b());
  CHECK(T::static_set_names(
            "-A KeenPbrTable_B -m set --match-set kpbr4S_remote dst -j MARK\n") ==
        std::set<std::string>{"kpbr4S_remote"});
}

TEST_CASE("static generation transition helper covers RulesOnly and refreshes") {
  CHECK(T::static_target_for_mode(FirewallApplyMode::RulesOnly, T::state_a(),
                                  FirewallSetGeneration::B) ==
        FirewallSetGeneration::A);
  CHECK(T::static_target_for_mode(FirewallApplyMode::RulesOnly, T::state_b(),
                                  FirewallSetGeneration::A) ==
        FirewallSetGeneration::B);
  CHECK(T::static_target_for_mode(FirewallApplyMode::PreserveSets, T::state_a(),
                                  FirewallSetGeneration::B) ==
        FirewallSetGeneration::B);
  CHECK(T::static_target_for_mode(FirewallApplyMode::PreserveSets, T::state_b(),
                                  FirewallSetGeneration::A) ==
        FirewallSetGeneration::A);
  CHECK(T::static_target_for_mode(FirewallApplyMode::StaticSetsOnly,
                                  T::state_a(), FirewallSetGeneration::A) ==
        FirewallSetGeneration::B);
  CHECK(T::static_target_for_mode(FirewallApplyMode::PreserveSets,
                                  T::state_missing(), FirewallSetGeneration::B) ==
        FirewallSetGeneration::B);

  CHECK(T::static_name_for_generation(FirewallSetGeneration::A, "remote",
                                      AF_INET) == "kpbr4s_remote");
  CHECK(T::static_name_for_generation(FirewallSetGeneration::B, "remote",
                                      AF_INET) == "kpbr4S_remote");
  CHECK(T::static_name_for_generation(FirewallSetGeneration::A, "remote",
                                      AF_INET6) == "kpbr6s_remote");
  CHECK(T::static_name_for_generation(FirewallSetGeneration::B, "remote",
                                      AF_INET6) == "kpbr6S_remote");
}

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
          "  /bin/printf '%s\\n' '-A OUTPUT -j KeenPbrTable_OUTPUT'\n"
          "elif [ \"$last\" = \"KeenPbrTable\" ]; then\n"
          "  /bin/printf '%s\\n' '-N KeenPbrTable' '-A KeenPbrTable -j KeenPbrTable_A'\n"
          "elif [ \"$last\" = \"KeenPbrTable_OUTPUT\" ]; then\n"
          "  /bin/printf '%s\\n' '-N KeenPbrTable_OUTPUT' '-A KeenPbrTable_OUTPUT -j KeenPbrTable_A'\n"
          "fi\n"
          "exit 0\n");
    };
    write_iptables(sandbox / "iptables");
    write_iptables(sandbox / "ip6tables");
    write_executable(
        sandbox / "iptables-restore",
        "#!/bin/sh\n"
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
      firewall.apply(FirewallApplyMode::Destructive);
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

TEST_CASE("RulesOnly stale generation is typed and does not repair OUTPUT") {
  const auto sandbox = std::filesystem::temp_directory_path() /
                       ("keen-pbr-iptables-rules-only-" +
                        std::to_string(static_cast<long long>(getpid())));
  std::filesystem::remove_all(sandbox);
  std::filesystem::create_directories(sandbox);
  const auto count_file = sandbox / "iptables-count";
  const auto restore_log = sandbox / "restore.log";

  write_executable(
      sandbox / "iptables",
      "#!/bin/sh\n"
      "count_file='" + count_file.string() + "'\n"
      "count=0\n"
      "if [ -f \"$count_file\" ]; then count=$(/bin/cat \"$count_file\"); fi\n"
      "count=$((count + 1))\n"
      "/bin/printf '%s\\n' \"$count\" > \"$count_file\"\n"
      "primary=A\n"
      "secondary=A\n"
      "if [ \"$count\" -gt 4 ]; then primary=B; secondary=A; fi\n"
      "last=''\n"
      "for arg in \"$@\"; do last=\"$arg\"; done\n"
      "if [ \"$last\" = \"-S\" ]; then\n"
      "  /bin/printf '%s\\n' \"-N KeenPbrTable\" \"-A KeenPbrTable -j KeenPbrTable_$primary\" \"-N KeenPbrTable_OUTPUT\" \"-A KeenPbrTable_OUTPUT -j KeenPbrTable_$secondary\"\n"
      "else\n"
      "  /bin/printf '%s\\n' \"-N $last\" \"-A $last -m set --match-set kpbr4s_remote dst -j MARK\"\n"
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
  firewall.create_mark_rule(1, criteria);

  CHECK_THROWS_AS(firewall.apply(FirewallApplyMode::RulesOnly),
                  FirewallRulesOnlyError);
  CHECK_FALSE(std::filesystem::exists(restore_log));
  std::filesystem::remove_all(sandbox);
}

TEST_CASE("consecutive RulesOnly applies flip rule slots while reusing static A") {
  const auto sandbox = std::filesystem::temp_directory_path() /
                       ("keen-pbr-iptables-rules-only-consecutive-" +
                        std::to_string(static_cast<long long>(getpid())));
  std::filesystem::remove_all(sandbox);
  std::filesystem::create_directories(sandbox);
  const auto state_file = sandbox / "generation";
  const auto restore_count_file = sandbox / "restore-count";
  const auto restore_log = sandbox / "restore-generations.log";
  const auto mutation_log = sandbox / "ipset-mutations.log";
  const auto restore_one = sandbox / "restore-1.rules";
  const auto restore_two = sandbox / "restore-2.rules";
  {
    std::ofstream initial_state(state_file);
    REQUIRE(initial_state.good());
    initial_state << "A\n";
  }

  write_executable(
      sandbox / "iptables",
      "#!/bin/sh\n"
      "state_file='" + state_file.string() + "'\n"
      "state=A\n"
      "if [ -f \"$state_file\" ]; then state=$(/bin/cat \"$state_file\"); fi\n"
      "last=''\n"
      "for arg in \"$@\"; do last=\"$arg\"; done\n"
      "if [ \"$last\" = \"-S\" ]; then\n"
      "  /bin/printf '%s\\n' \"-N KeenPbrTable\" \"-A KeenPbrTable -j KeenPbrTable_$state\" \"-N KeenPbrTable_OUTPUT\" \"-A KeenPbrTable_OUTPUT -j KeenPbrTable_$state\"\n"
      "  exit 0\n"
      "fi\n"
      "case \"$last\" in\n"
      "  KeenPbrTable|KeenPbrTable_OUTPUT)\n"
      "    /bin/printf '%s\\n' \"-N $last\" \"-A $last -j KeenPbrTable_$state\"\n"
      "    ;;\n"
      "  KeenPbrTable_A|KeenPbrTable_B)\n"
      "    /bin/printf '%s\\n' \"-N $last\" \"-A $last -m set --match-set kpbr4s_remote dst -j MARK\"\n"
      "    ;;\n"
      "  PREROUTING)\n"
      "    /bin/printf '%s\\n' '-A PREROUTING -j KeenPbrTable'\n"
      "    ;;\n"
      "  OUTPUT)\n"
      "    /bin/printf '%s\\n' '-A OUTPUT -j KeenPbrTable_OUTPUT'\n"
      "    ;;\n"
      "esac\n"
      "exit 0\n");
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
      "state_file='" + state_file.string() + "'\n"
      "count_file='" + restore_count_file.string() + "'\n"
      "restore_log='" + restore_log.string() + "'\n"
      "count=0\n"
      "if [ -f \"$count_file\" ]; then count=$(/bin/cat \"$count_file\"); fi\n"
      "count=$((count + 1))\n"
      "/bin/printf '%s\\n' \"$count\" > \"$count_file\"\n"
      "input_file='" + sandbox.string() + "/restore-'\"$count\"'.rules'\n"
      "/bin/cat > \"$input_file\"\n"
      "target=''\n"
      "while IFS= read -r line; do\n"
      "  case \"$line\" in\n"
      "    '-A KeenPbrTable -j KeenPbrTable_A') target=A ;;\n"
      "    '-A KeenPbrTable -j KeenPbrTable_B') target=B ;;\n"
      "  esac\n"
      "done < \"$input_file\"\n"
      "/bin/printf '%s\\n' \"$target\" >> \"$restore_log\"\n"
      "if [ -n \"$target\" ]; then /bin/printf '%s\\n' \"$target\" > \"$state_file\"; fi\n"
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
    firewall.create_mark_rule(1, criteria);
    firewall.apply(FirewallApplyMode::RulesOnly);
  };

  apply_rules_only();
  apply_rules_only();

  std::ifstream generations_input(restore_log);
  REQUIRE(generations_input.good());
  std::vector<std::string> generations;
  std::string generation;
  while (std::getline(generations_input, generation)) {
    generations.push_back(generation);
  }
  CHECK(generations == std::vector<std::string>{"B", "A"});
  CHECK_FALSE(std::filesystem::exists(mutation_log));

  const auto read_rules = [](const std::filesystem::path &path) {
    std::ifstream input(path);
    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
  };
  const auto first_rules = read_rules(restore_one);
  const auto second_rules = read_rules(restore_two);
  CHECK(first_rules.find("-A KeenPbrTable_B") != std::string::npos);
  CHECK(second_rules.find("-A KeenPbrTable_A") != std::string::npos);
  CHECK(first_rules.find("--match-set kpbr4s_remote") != std::string::npos);
  CHECK(second_rules.find("--match-set kpbr4s_remote") != std::string::npos);
  CHECK(first_rules.find("kpbr4S_remote") == std::string::npos);
  CHECK(second_rules.find("kpbr4S_remote") == std::string::npos);

  std::filesystem::remove_all(sandbox);
}

TEST_CASE("RulesOnly rules can target inactive generation while using active sets") {
  const auto a_sets_b_rules = T::build_rules_for_slots(
      FirewallSetGeneration::B, FirewallSetGeneration::A, false);
  CHECK(a_sets_b_rules.find("KeenPbrTable_B") != std::string::npos);
  CHECK(a_sets_b_rules.find("kpbr4s_sample") != std::string::npos);

  const auto b_sets_a_rules = T::build_rules_for_slots(
      FirewallSetGeneration::A, FirewallSetGeneration::B, true);
  CHECK(b_sets_a_rules.find("KeenPbrTable_A") != std::string::npos);
  CHECK(b_sets_a_rules.find("kpbr6S_sample") != std::string::npos);
}

TEST_CASE("set-refresh modes publish new rules with the inactive static slot") {
  // A prior RulesOnly apply may leave live B rules referring to static A
  // sets. PreserveSets and StaticSetsOnly must refresh B, then publish A
  // rules that refer to B rather than flushing the still-live A sets.
  const auto refreshed = T::build_rules_for_slots(
      FirewallSetGeneration::A, FirewallSetGeneration::B, false);
  CHECK(refreshed.find("-A KeenPbrTable_A -m set --match-set kpbr4S_sample") !=
        std::string::npos);
  CHECK(refreshed.find("--match-set kpbr4s_sample") == std::string::npos);
}

TEST_CASE("live generation parser rejects damaged dispatchers") {
  CHECK(T::live_generation_state(
            "-N KeenPbrTable\n-A KeenPbrTable -j KeenPbrTable_A\n") ==
        T::state_a());
  CHECK(T::live_generation_state(
            "-N KeenPbrTable\n-A KeenPbrTable -j KeenPbrTable_B\n") ==
        T::state_b());
  CHECK(T::live_generation_state("-N KeenPbrTable\n") == T::state_missing());
  CHECK(T::live_generation_state("-A KeenPbrTable -j KeenPbrTable_A\n"
                                 "-A KeenPbrTable -j KeenPbrTable_B\n") ==
        T::state_invalid());
  CHECK(T::live_generation_state("-A KeenPbrTable -j KeenPbrTable_A\n"
                                 "-A KeenPbrTable -j KeenPbrTable_A\n") ==
        T::state_invalid());
  CHECK(T::live_generation_state("-A KeenPbrTable -j ForeignTarget\n") ==
        T::state_invalid());
}

TEST_CASE("target generation accounts for both dispatchers") {
  CHECK(T::target_for_states(T::state_missing(), T::state_missing()) ==
        FirewallSetGeneration::A);
  CHECK(T::target_for_states(T::state_a(), T::state_missing()) ==
        FirewallSetGeneration::B);
  CHECK(T::target_for_states(T::state_missing(), T::state_b()) ==
        FirewallSetGeneration::A);
  CHECK(T::target_for_states(T::state_a(), T::state_a()) ==
        FirewallSetGeneration::B);
  CHECK(T::target_for_states(T::state_b(), T::state_b()) ==
        FirewallSetGeneration::A);
  // On a partial publication, PREROUTING is authoritative; OUTPUT is rolled
  // back to it before this target is used.
  CHECK(T::target_for_states(T::state_a(), T::state_b()) ==
        FirewallSetGeneration::B);
  CHECK(T::target_for_states(T::state_b(), T::state_a()) ==
        FirewallSetGeneration::A);
  CHECK_THROWS(
      T::target_for_states(T::state_invalid(), T::state_missing()));
  CHECK_THROWS(T::target_for_states(T::state_missing(), T::state_invalid()));
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
  CHECK(s.find("-A PREROUTING -j KeenPbrTable") == std::string::npos);
  CHECK(s.find("-A OUTPUT -j KeenPbrTable_OUTPUT") == std::string::npos);
  CHECK(s.find("-A KeenPbrTable_OUTPUT -j KeenPbrTable_A") !=
        std::string::npos);
  CHECK(s.find("-A KeenPbrTable_A -m set --match-set myset dst -j MARK "
               "--set-xmark 0x100/0xffffffff") != std::string::npos);
  CHECK(s.find("-A KeenPbrTable_A -m set --match-set myset dst -j RETURN") !=
        std::string::npos);
  CHECK(s.size() >= 7);
  CHECK(s.substr(s.size() - 7) == "COMMIT\n");
}

TEST_CASE("build_ipt_script: IPv4 drop rule") {
  auto s = T::build_ipt_script(false, {drop_rule("blacklist", false)});
  CHECK(s.find("-A KeenPbrTable_A -m set --match-set blacklist dst -j DROP") !=
        std::string::npos);
}

TEST_CASE("build_ipt_script: IPv4 pass rule") {
  auto s = T::build_ipt_script(false, {pass_rule("allowlist", false)});
  CHECK(s.find("-A KeenPbrTable_A -m set --match-set allowlist dst -j RETURN") !=
        std::string::npos);
}

TEST_CASE("build_ipt_script: IPv6 mark rule") {
  auto s = T::build_ipt_script(true, {mark_rule("v6set", true, 0x200)});
  CHECK(s.find("-A KeenPbrTable_A -m set --match-set v6set dst -j MARK "
               "--set-xmark 0x200/0xffffffff") != std::string::npos);
  CHECK(s.find("-A KeenPbrTable_A -m set --match-set v6set dst -j RETURN") !=
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

TEST_CASE("build_ipt_script: empty rules still build KeenPbrTable scaffold") {
  auto s = T::build_ipt_script(false, {});
  CHECK(s.find("*mangle\n") != std::string::npos);
  CHECK(s.find(":KeenPbrTable - [0:0]\n") != std::string::npos);
  CHECK(s.find("-A PREROUTING -j KeenPbrTable\n") == std::string::npos);
  CHECK(s.find("-A OUTPUT -j KeenPbrTable_OUTPUT\n") == std::string::npos);
  CHECK(s ==
        "*mangle\n:KeenPbrTable - [0:0]\n:KeenPbrTable_OUTPUT - [0:0]\n"
        ":KeenPbrTable_A - [0:0]\n-F KeenPbrTable_A\n-F KeenPbrTable\n"
        "-F KeenPbrTable_OUTPUT\n-A KeenPbrTable -j KeenPbrTable_A\n"
        "-A KeenPbrTable_OUTPUT -j KeenPbrTable_A\nCOMMIT\n");
}

TEST_CASE("build_ipt_script: replacement rebuilds inactive B chain and "
          "switches dispatcher") {
  const auto script =
      keen_pbr3::IptablesBuilderTest::build_replacement_script();
  const auto flush = script.find("-F KeenPbrTable_B");
  const auto dispatcher_flush =
      script.find("-F KeenPbrTable\n-F KeenPbrTable_OUTPUT");
  const auto dispatcher_jump = script.find("-A KeenPbrTable -j KeenPbrTable_B");
  const auto output_jump =
      script.find("-A KeenPbrTable_OUTPUT -j KeenPbrTable_B");

  REQUIRE(flush != std::string::npos);
  REQUIRE(dispatcher_flush != std::string::npos);
  REQUIRE(dispatcher_jump != std::string::npos);
  REQUIRE(output_jump != std::string::npos);
  CHECK(script.find(":KeenPbrTable_A - [0:0]") == std::string::npos);
  CHECK(script.find(":KeenPbrTable_B - [0:0]") != std::string::npos);
  CHECK(flush < dispatcher_flush);
  CHECK(dispatcher_flush < dispatcher_jump);
  CHECK(script.find("-X KeenPbrTable_A") == std::string::npos);
  CHECK(script.find("-R KeenPbrTable") == std::string::npos);
  CHECK(script.find("-F KeenPbrTable_A") == std::string::npos);
  CHECK(script.find("-A PREROUTING -j KeenPbrTable") == std::string::npos);
  CHECK(script.find("-A OUTPUT -j KeenPbrTable_OUTPUT") == std::string::npos);
}

TEST_CASE("generation scripts declare only the target slot so noflush "
          "preserves the active slot") {
  const auto raw = T::build_raw_script_for_generation(FirewallSetGeneration::B);
  CHECK(raw.find(":KeenPbrRaw - [0:0]") != std::string::npos);
  CHECK(raw.find(":KeenPbrRaw_A - [0:0]") == std::string::npos);
  CHECK(raw.find(":KeenPbrRaw_B - [0:0]") != std::string::npos);
  CHECK(raw.find("-F KeenPbrRaw_B\n") != std::string::npos);
  CHECK(raw.find("-F KeenPbrRaw_A\n") == std::string::npos);
  CHECK(raw.find("-F KeenPbrRaw\n") != std::string::npos);
  CHECK(raw.find("-A KeenPbrRaw -j KeenPbrRaw_B\n") != std::string::npos);
  CHECK(raw.find("-A PREROUTING") == std::string::npos);

  const auto output =
      T::build_output_script_for_generation(FirewallSetGeneration::B);
  CHECK(output.find(":KeenPbrOutput - [0:0]") != std::string::npos);
  CHECK(output.find(":KeenPbrOutput_A - [0:0]") == std::string::npos);
  CHECK(output.find(":KeenPbrOutput_B - [0:0]") != std::string::npos);
  CHECK(output.find("-F KeenPbrOutput_B\n") != std::string::npos);
  CHECK(output.find("-F KeenPbrOutput_A\n") == std::string::npos);
  CHECK(output.find("-A KeenPbrOutput -j KeenPbrOutput_B\n") !=
        std::string::npos);
  CHECK(output.find("-A OUTPUT") == std::string::npos);
}

TEST_CASE("generation-specific drop and pass rules are emitted into the target "
          "slot") {
  const auto raw_drop = T::build_raw_script_for_generation(
      FirewallSetGeneration::B, {drop_rule("blocked", false)});
  CHECK(raw_drop.find(
            "-A KeenPbrRaw_B -m set --match-set blocked dst -j DROP\n") !=
        std::string::npos);
  CHECK(raw_drop.find(
            "-A KeenPbrTable -m set --match-set blocked dst -j DROP\n") ==
        std::string::npos);

  const auto raw_pass = T::build_raw_script_for_generation(
      FirewallSetGeneration::B, {pass_rule("allowed", false)});
  CHECK(raw_pass.find(
            "-A KeenPbrRaw_B -m set --match-set allowed dst -j RETURN\n") !=
        std::string::npos);
}

TEST_CASE("build_ipt_script: global prefilter RETURN lines are emitted before "
          "route rules") {
  auto s = T::build_ipt_script(false, {mark_rule("myset", false, 0x100)},
                               prefilter_with_interfaces({"br0"}));

  const std::string dnat =
      "-A KeenPbrTable_A -m conntrack --ctstate DNAT -j RETURN\n";
  const std::string marked =
      "-A KeenPbrTable_A -m mark ! --mark 0x0/0xffffffff -j ACCEPT\n";
  const std::string iface = "-A KeenPbrTable_A ! -i br0 -j RETURN\n";
  const std::string mark = "-A KeenPbrTable_A -m set --match-set myset dst -j "
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
  FirewallGlobalPrefilter prefilter;
  prefilter.restore_conntrack_mark = true;
  prefilter.conntrack_mark_mask = 0x00FF0000U;
  const auto script =
      T::build_ipt_script(false, {mark_rule("myset", false, 0x100)}, prefilter);
  CHECK(script.find(
            "-m conntrack --ctdir ORIGINAL -m connmark ! --mark 0/0xff0000") !=
        std::string::npos);
  CHECK(script.find("CONNMARK --restore-mark --mask 0xff0000") !=
        std::string::npos);
  CHECK(script.find("CONNMARK --save-mark --mask 0xff0000") !=
        std::string::npos);
  const auto restore =
      script.find("--ctdir ORIGINAL -m connmark ! --mark 0/0xff0000");
  const auto restored_return = script.find(
      "-m conntrack --ctdir ORIGINAL -m mark ! --mark 0/0xff0000 -j RETURN");
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
  FirewallGlobalPrefilter prefilter;
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

  CHECK(s.find("-A KeenPbrTable_A -m set --match-set allowlist dst -i br0 -j "
               "RETURN\n") != std::string::npos);
  CHECK(s.find("-A KeenPbrTable_A -m set --match-set allowlist dst -i wg0 -j "
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

  const auto prefilter = build_firewall_global_prefilter(cfg);
  auto s = T::build_ipt_script(false, {mark_rule("kpbr4_local", false, 0x100)},
                               prefilter);

  const std::string iface = "-A KeenPbrTable_A ! -i br0 -j RETURN\n";
  const std::string mark = "-A KeenPbrTable_A -m set --match-set kpbr4_local dst "
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
  CHECK(s.find("-A KeenPbrTable_A -m set --match-set pairwise_set dst -j MARK "
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

  const auto prefilter = build_firewall_global_prefilter(cfg);
  auto s = T::build_ipt_script(false, {mark_rule("kpbr4_local", false, 0x100)},
                               prefilter);

  CHECK(s.find("! -i ") == std::string::npos);
  CHECK(s.find("-A KeenPbrTable_A -m set --match-set kpbr4_local dst -j MARK "
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
  CHECK(s.find("-A KeenPbrTable_A -m set --match-set myset dst -p tcp --dport "
               "443 -j MARK --set-xmark 0x100/0xffffffff") !=
        std::string::npos);
}

TEST_CASE("build_ipt_script: dscp matcher is emitted") {
  ProtoPortFilter f;
  f.dscp = 46;
  auto s = T::build_ipt_script(false, {mark_rule("myset", false, 0x100, f)});
  CHECK(s.find("-A KeenPbrTable_A -m set --match-set myset dst -m dscp --dscp "
               "46 -j MARK --set-xmark 0x100/0xffffffff") != std::string::npos);
}

TEST_CASE("build_ipt_script: udp + port range in rule") {
  ProtoPortFilter f;
  f.proto = L4Proto::Udp;
  f.dst_port = "8000-9000";
  auto s = T::build_ipt_script(false, {drop_rule("bl", false, f)});
  CHECK(s.find("-A KeenPbrTable_A -m set --match-set bl dst -p udp --dport "
               "8000:9000 -j DROP") != std::string::npos);
}

TEST_CASE("build_ipt_script: tcp/udp + port list → two rules") {
  ProtoPortFilter f;
  f.proto = L4Proto::TcpUdp;
  f.dst_port = "80,443";
  // create_mark_rule expands tcp/udp, so we simulate by passing two rules
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
  f.dst_port = "1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16";
  auto s = T::build_ipt_script(false, {drop_rule("bl", false, f)});
  CHECK(s.find("--dports 1,2,3,4,5,6,7,8,9,10,11,12,13,14,15 -j DROP") !=
        std::string::npos);
  CHECK(s.find("--dports 16 -j DROP") != std::string::npos);
}

TEST_CASE("build_ipt_script: multiport ranges consume two slots") {
  ProtoPortFilter f;
  f.proto = L4Proto::Udp;
  f.dst_port = "1-2,3-4,5-6,7-8,9-10,11-12,13-14,15-16";
  auto s = T::build_ipt_script(false, {drop_rule("bl", false, f)});
  CHECK(s.find("--dports 1:2,3:4,5:6,7:8,9:10,11:12,13:14 -j DROP") !=
        std::string::npos);
  CHECK(s.find("--dports 15:16 -j DROP") != std::string::npos);
}

TEST_CASE("build_ipt_script: oversized negated multiport list remains AND") {
  ProtoPortFilter f;
  f.proto = L4Proto::Tcp;
  f.dst_port = "1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16";
  f.negate_dst_port = true;
  auto s = T::build_ipt_script(false, {drop_rule("bl", false, f)});
  CHECK(s.find("-m multiport ! --dports "
               "1,2,3,4,5,6,7,8,9,10,11,12,13,14,15 "
               "-m multiport ! --dports 16 -j DROP") != std::string::npos);
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
  f.src_port = "1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16";
  f.dst_port =
      "101,102,103,104,105,106,107,108,109,110,111,112,113,114,115,116";
  auto s = T::build_ipt_script(false, {drop_rule("bl", false, f)});
  const std::string src_a =
      "-m multiport --sports 1,2,3,4,5,6,7,8,9,10,11,12,13,14,15";
  const std::string src_b = "-m multiport --sports 16";
  const std::string dst_a =
      "-m multiport --dports "
      "101,102,103,104,105,106,107,108,109,110,111,112,113,114,115";
  const std::string dst_b = "-m multiport --dports 116";
  CHECK(s.find(src_a + " " + dst_a + " -j DROP") != std::string::npos);
  CHECK(s.find(src_a + " " + dst_b + " -j DROP") != std::string::npos);
  CHECK(s.find(src_b + " " + dst_a + " -j DROP") != std::string::npos);
  CHECK(s.find(src_b + " " + dst_b + " -j DROP") != std::string::npos);
}

TEST_CASE("build_ipt_script: negated chunks combine with positive alternatives") {
  ProtoPortFilter f;
  f.proto = L4Proto::Udp;
  f.src_port = "1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16";
  f.negate_src_port = true;
  f.dst_port =
      "101,102,103,104,105,106,107,108,109,110,111,112,113,114,115,116";
  auto s = T::build_ipt_script(false, {drop_rule("bl", false, f)});
  const std::string excluded =
      "-m multiport ! --sports 1,2,3,4,5,6,7,8,9,10,11,12,13,14,15 "
      "-m multiport ! --sports 16";
  CHECK(s.find(excluded + " -m multiport --dports "
                          "101,102,103,104,105,106,107,108,109,110,111,112,"
                          "113,114,115 -j DROP") != std::string::npos);
  CHECK(s.find(excluded + " -m multiport --dports 116 -j DROP") !=
        std::string::npos);
}

TEST_CASE("build_ipt_script: positive chunks combine with negated destination") {
  ProtoPortFilter f;
  f.proto = L4Proto::Udp;
  f.src_port = "1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16";
  f.dst_port =
      "101,102,103,104,105,106,107,108,109,110,111,112,113,114,115,116";
  f.negate_dst_port = true;
  auto s = T::build_ipt_script(false, {drop_rule("bl", false, f)});
  const std::string excluded =
      "-m multiport ! --dports "
      "101,102,103,104,105,106,107,108,109,110,111,112,113,114,115 "
      "-m multiport ! --dports 116";
  CHECK(s.find("-m multiport --sports "
               "1,2,3,4,5,6,7,8,9,10,11,12,13,14,15 " +
               excluded + " -j DROP") != std::string::npos);
  CHECK(s.find("-m multiport --sports 16 " + excluded + " -j DROP") !=
        std::string::npos);
}

TEST_CASE(
    "build_ipt_script: any proto + src_port expands to tcp and udp rules") {
  ProtoPortFilter f;
  f.proto = L4Proto::Any;
  f.src_port = "11111";
  auto s = T::build_ipt_script(false, {mark_rule("myset", false, 0x100, f)});
  CHECK(s.find("-A KeenPbrTable_A -m set --match-set myset dst -p tcp --sport "
               "11111 -j MARK --set-xmark 0x100/0xffffffff") !=
        std::string::npos);
  CHECK(s.find("-A KeenPbrTable_A -m set --match-set myset dst -p udp --sport "
               "11111 -j MARK --set-xmark 0x100/0xffffffff") !=
        std::string::npos);
  CHECK(s.find("-A KeenPbrTable_A -m set --match-set myset dst --sport 11111") ==
        std::string::npos);
}

TEST_CASE(
    "build_ipt_script: no proto, no ports → no extra flags (regression)") {
  auto s = T::build_ipt_script(false, {mark_rule("myset", false, 0x100)});
  CHECK(s.find("-A KeenPbrTable_A -m set --match-set myset dst -j MARK "
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
  CHECK(s.find("-A KeenPbrTable_A -m set --match-set myset dst -s "
               "192.168.10.0/24 -j MARK --set-xmark 0x100/0xffffffff") !=
        std::string::npos);
}

TEST_CASE("build_ipt_script: single dest_addr → -d flag") {
  ProtoPortFilter f;
  f.dst_addr = {"10.0.0.0/8"};
  auto s = T::build_ipt_script(false, {mark_rule("myset", false, 0x100, f)});
  CHECK(s.find("-A KeenPbrTable_A -m set --match-set myset dst -d 10.0.0.0/8 -j "
               "MARK --set-xmark 0x100/0xffffffff") != std::string::npos);
}

TEST_CASE("build_ipt_script: src_addr + dest_addr → both flags") {
  ProtoPortFilter f;
  f.src_addr = {"192.168.1.0/24"};
  f.dst_addr = {"8.8.8.0/24"};
  auto s = T::build_ipt_script(false, {mark_rule("myset", false, 0x100, f)});
  CHECK(s.find("-A KeenPbrTable_A -m set --match-set myset dst -s 192.168.1.0/24 "
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
  CHECK(s.find("-A KeenPbrTable_A -m set --match-set myset dst -s 192.168.1.0/24 "
               "-p tcp --dport 443 -j MARK --set-xmark 0x100/0xffffffff") !=
        std::string::npos);
}

TEST_CASE("build_ipt_script: drop rule with src_addr → -s flag on DROP") {
  ProtoPortFilter f;
  f.src_addr = {"10.10.0.0/16"};
  auto s = T::build_ipt_script(false, {drop_rule("bl", false, f)});
  CHECK(s.find("-A KeenPbrTable_A -m set --match-set bl dst -s 10.10.0.0/16 -j "
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
  CHECK(s.find("-A KeenPbrTable_A -m set --match-set myset dst ! -s "
               "192.168.1.0/24 -j MARK --set-xmark 0x100/0xffffffff") !=
        std::string::npos);
}

TEST_CASE("build_ipt_script: negated dest_addr → ! -d flag") {
  ProtoPortFilter f;
  f.dst_addr = {"10.0.0.0/8"};
  f.negate_dst_addr = true;
  auto s = T::build_ipt_script(false, {mark_rule("myset", false, 0x100, f)});
  CHECK(s.find("-A KeenPbrTable_A -m set --match-set myset dst ! -d 10.0.0.0/8 "
               "-j MARK --set-xmark 0x100/0xffffffff") != std::string::npos);
}

TEST_CASE("build_ipt_script: negated dest_port in full rule") {
  ProtoPortFilter f;
  f.proto = L4Proto::Tcp;
  f.dst_port = "443";
  f.negate_dst_port = true;
  auto s = T::build_ipt_script(false, {mark_rule("myset", false, 0x100, f)});
  CHECK(s.find("-A KeenPbrTable_A -m set --match-set myset dst -p tcp ! --dport "
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
      s.find("-A KeenPbrTable_A -m set --match-set myset dst ! -s 192.168.1.0/24 "
             "-p tcp ! --dport 443 -j MARK --set-xmark 0x100/0xffffffff") !=
      std::string::npos);
}

TEST_CASE("build_ipt_script: drop rule with negated src_addr") {
  ProtoPortFilter f;
  f.src_addr = {"10.10.0.0/16"};
  f.negate_src_addr = true;
  auto s = T::build_ipt_script(false, {drop_rule("bl", false, f)});
  CHECK(s.find("-A KeenPbrTable_A -m set --match-set bl dst ! -s 10.10.0.0/16 -j "
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
