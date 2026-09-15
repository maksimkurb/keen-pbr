#include <doctest/doctest.h>

#include "../src/cache/cache_manager.hpp"
#include "../src/firewall/firewall_runtime.hpp"
#include "../src/lists/list_entry_visitor.hpp"
#include "../src/util/ipv6_support.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace keen_pbr3 {

namespace {

class PathGuard {
public:
  PathGuard() : old_path_(std::getenv("PATH")) {}

  ~PathGuard() {
    if (old_path_.has_value()) {
      (void)setenv("PATH", old_path_->c_str(), 1);
    } else {
      (void)unsetenv("PATH");
    }
  }

private:
  std::optional<std::string> old_path_;
};

void write_executable(const std::filesystem::path &path,
                     const std::string &contents) {
  std::ofstream output(path);
  REQUIRE(output.good());
  output << contents;
  output.close();
  REQUIRE(chmod(path.c_str(), 0755) == 0);
}

class RulesOnlyFirewall final : public Firewall {
public:
  void prepare_apply(FirewallApplyMode mode) override {
    prepared_modes.push_back(mode);
  }

  void create_ipset(const std::string&, int, uint32_t) override {
    ++set_declarations;
  }

  void create_mark_rule(uint32_t,
                        const FirewallRuleCriteria&) override {
    ++rule_count;
  }

  void create_drop_rule(const FirewallRuleCriteria&) override { ++rule_count; }

  void create_pass_rule(const FirewallRuleCriteria&) override { ++rule_count; }

  std::unique_ptr<ListEntryVisitor>
  create_batch_loader(const std::string&) override {
    ++stream_count;
    throw std::runtime_error("RulesOnly unexpectedly requested list streaming");
  }

  void apply(FirewallApplyMode mode) override {
    applied_mode = mode;
  }

  void cleanup() override {}

  FirewallBackend backend() const override { return FirewallBackend::nftables; }

  int set_declarations{0};
  int stream_count{0};
  int rule_count{0};
  FirewallApplyMode applied_mode{FirewallApplyMode::Destructive};
  std::vector<FirewallApplyMode> prepared_modes;
};

class RecordingFirewall final : public Firewall {
public:
  enum class RuleAction { Mark, Drop, Pass, Balance };

  struct RecordedRule {
    RuleAction action;
    uint32_t fwmark{0};
    FirewallRuleCriteria criteria;
    std::vector<FirewallBalanceCandidate> candidates;
  };

private:
  class Visitor final : public ListEntryVisitor {
  public:
    explicit Visitor(RecordingFirewall &owner) : owner_(owner) {}

    void on_entry(EntryType, std::string_view) override {
      ++owner_.streamed_entries;
    }

    void finish() override {
      ++owner_.finished_loaders;
      owner_.calls.push_back("finish");
    }

  private:
    RecordingFirewall &owner_;
  };

public:
  std::string static_set_name(const std::string &list_name,
                              int family) const override {
    if (!generation_names) {
      return Firewall::static_set_name(list_name, family);
    }
    return std::string(family == AF_INET6 ? "kpbr6s_" : "kpbr4s_") +
           list_name;
  }

  std::vector<std::string>
  static_set_names(const std::string &list_name, int family) const override {
    if (!generation_names) {
      return Firewall::static_set_names(list_name, family);
    }
    const std::string prefix = family == AF_INET6 ? "kpbr6" : "kpbr4";
    return {prefix + "s_" + list_name, prefix + "S_" + list_name};
  }

  void prepare_apply(FirewallApplyMode mode) override {
    prepared_modes.push_back(mode);
    calls.push_back("prepare");
  }

  void create_ipset(const std::string &name, int, uint32_t) override {
    set_names.push_back(name);
    calls.push_back("set:" + name);
  }

  void create_mark_rule(uint32_t fwmark,
                        const FirewallRuleCriteria &criteria) override {
    record_rule(RuleAction::Mark, fwmark, criteria, {});
  }

  void create_drop_rule(const FirewallRuleCriteria &criteria) override {
    record_rule(RuleAction::Drop, 0, criteria, {});
  }

  void create_pass_rule(const FirewallRuleCriteria &criteria) override {
    record_rule(RuleAction::Pass, 0, criteria, {});
  }

  void create_balance_rule(
      uint32_t fallback_fwmark,
      const std::vector<FirewallBalanceCandidate> &candidates,
      const FirewallRuleCriteria &criteria) override {
    record_rule(RuleAction::Balance, fallback_fwmark, criteria, candidates);
  }

  std::unique_ptr<ListEntryVisitor>
  create_batch_loader(const std::string &name) override {
    ++stream_count;
    calls.push_back("loader:" + name);
    return std::make_unique<Visitor>(*this);
  }

  void apply(FirewallApplyMode mode) override {
    applied_modes.push_back(mode);
    calls.push_back("apply");
    if (mode == FirewallApplyMode::RulesOnly && fail_rules_only) {
      fail_rules_only = false;
      throw FirewallRulesOnlyError("controlled RulesOnly preflight failure");
    }
  }

  void cleanup() override {}

  FirewallBackend backend() const override { return FirewallBackend::nftables; }

private:
  void record_rule(RuleAction action, uint32_t fwmark,
                   const FirewallRuleCriteria &criteria,
                   const std::vector<FirewallBalanceCandidate> &candidates) {
    ++rule_count;
    calls.push_back("rule");
    if (criteria.dst_set_name.has_value()) {
      referenced_sets.push_back(*criteria.dst_set_name);
    }
    recorded_rules.push_back({action, fwmark, criteria, candidates});
  }

public:

  bool fail_rules_only{false};
  bool generation_names{false};
  int stream_count{0};
  int streamed_entries{0};
  int finished_loaders{0};
  int rule_count{0};
  std::vector<std::string> set_names;
  std::vector<std::string> referenced_sets;
  std::vector<std::string> calls;
  std::vector<RecordedRule> recorded_rules;
  std::vector<FirewallApplyMode> prepared_modes;
  std::vector<FirewallApplyMode> applied_modes;
};

Config empty_source_list_config() {
  return parse_config(R"({
    "outbounds": [{"type":"table","tag":"wan","table":254}],
    "lists": {"remote": {"file":"/path/that-must-never-be-opened"}},
    "route": {"rules": [{"list":["remote"],"outbound":"wan"}]}
  })");
}

Config empty_url_list_config() {
  return parse_config(R"({
    "outbounds": [{"type":"table","tag":"wan","table":254}],
    "lists": {"remote": {"url":"http://127.0.0.1:1/empty"}},
    "route": {"rules": [{"list":["remote"],"outbound":"wan"}]}
  })");
}

Config invalid_inline_list_config() {
  return parse_config(R"({
    "outbounds": [{"type":"table","tag":"wan","table":254}],
    "lists": {"remote": {"ip_cidrs":["not an address"]}},
    "route": {"rules": [{"list":["remote"],"outbound":"wan"}]}
  })");
}

RuleState aligned_empty_rule_state() {
  RuleState state;
  state.rule_index = 0;
  state.list_names = {"remote"};
  state.action_type = RuleActionType::Mark;
  state.fwmark = 1;
  return state;
}

Config valid_inline_list_config() {
  return parse_config(R"({
    "outbounds": [{"type":"table","tag":"wan","table":254}],
    "lists": {"remote": {"ip_cidrs":["192.0.2.0/24"]}},
    "route": {"rules": [{"list":["remote"],"outbound":"wan"}]}
  })");
}

Config valid_inline_ipv6_list_config() {
  return parse_config(R"({
    "daemon": {"firewall_backend":"iptables", "ipv6_enabled":true},
    "outbounds": [{"type":"table","tag":"wan","table":254}],
    "lists": {"remote": {"ip_cidrs":["192.0.2.0/24"]}},
    "route": {"rules": [{"list":["remote"],"outbound":"wan"}]}
  })");
}

RuleState aligned_static_rule_state() {
  RuleState state = aligned_empty_rule_state();
  state.set_names = {"kpbr4_remote"};
  return state;
}

} // namespace

TEST_CASE("iptables capacity changes force destructive recreation") {
  Config current;
  Config candidate;
  candidate.daemon = DaemonConfig{};
  candidate.daemon->ipset_maxelem = 131072;

  const auto policy =
      firewall_config_apply_policy(FirewallBackend::iptables, current,
                                   candidate);
  CHECK(policy.mode == FirewallApplyMode::Destructive);
  CHECK(policy.force_clear_dynamic_sets);
}

TEST_CASE("iptables capacity set and clear transitions force recreation") {
  Config configured;
  configured.daemon = DaemonConfig{};
  configured.daemon->ipset_hashsize = 2048;

  Config cleared;
  const auto to_cleared =
      firewall_config_apply_policy(FirewallBackend::iptables, configured,
                                   cleared);
  const auto to_configured =
      firewall_config_apply_policy(FirewallBackend::iptables, cleared,
                                   configured);
  CHECK(to_cleared.mode == FirewallApplyMode::Destructive);
  CHECK(to_cleared.force_clear_dynamic_sets);
  CHECK(to_configured.mode == FirewallApplyMode::Destructive);
  CHECK(to_configured.force_clear_dynamic_sets);
}

TEST_CASE("iptables capacity defaults and rounded hashsize preserve sets") {
  Config defaults;
  Config explicit_defaults;
  explicit_defaults.daemon = DaemonConfig{};
  explicit_defaults.daemon->ipset_hashsize = 1024;
  explicit_defaults.daemon->ipset_maxelem = 65536;
  const auto default_policy = firewall_config_apply_policy(
      FirewallBackend::iptables, defaults, explicit_defaults);
  CHECK(default_policy.mode == FirewallApplyMode::PreserveSets);
  CHECK_FALSE(default_policy.force_clear_dynamic_sets);

  Config rounded_low;
  rounded_low.daemon = DaemonConfig{};
  rounded_low.daemon->ipset_hashsize = 1536;
  Config rounded_high;
  rounded_high.daemon = DaemonConfig{};
  rounded_high.daemon->ipset_hashsize = 2048;
  const auto rounded_policy = firewall_config_apply_policy(
      FirewallBackend::iptables, rounded_low, rounded_high);
  CHECK(rounded_policy.mode == FirewallApplyMode::PreserveSets);
  CHECK_FALSE(rounded_policy.force_clear_dynamic_sets);
}

TEST_CASE("unchanged or nftables ipset capacities preserve sets") {
  Config current;
  current.daemon = DaemonConfig{};
  current.daemon->ipset_hashsize = 2048;
  current.daemon->ipset_maxelem = 131072;
  Config same = current;

  const auto unchanged =
      firewall_config_apply_policy(FirewallBackend::iptables, current, same);
  const auto nftables = firewall_config_apply_policy(
      FirewallBackend::nftables, current, Config{});
  CHECK(unchanged.mode == FirewallApplyMode::PreserveSets);
  CHECK_FALSE(unchanged.force_clear_dynamic_sets);
  CHECK(nftables.mode == FirewallApplyMode::PreserveSets);
  CHECK_FALSE(nftables.force_clear_dynamic_sets);
}

TEST_CASE("runtime preserves ordered direct rule actions and selectors") {
  const Config config = parse_config(R"({
    "daemon": {"ipv6_enabled": false},
    "outbounds": [
      {"type":"table","tag":"wan","table":100},
      {"type":"blackhole","tag":"blocked"},
      {"type":"ignore","tag":"direct"}
    ],
    "route": {"rules": [
      {"outbound":"wan","dscp":46},
      {"outbound":"wan","dscp":47,"proto":"tcp","dest_port":"443"},
      {"outbound":"wan","dscp":48,"proto":"udp","src_port":"53"},
      {"outbound":"blocked","dest_addr":"203.0.113.7","dest_port":"22"},
      {"outbound":"direct","src_addr":"!10.0.0.0/8","dest_port":"!53"}
    ]}
  })");
  RecordingFirewall firewall;
  CacheManager cache("/tmp/keen-pbr-firewall-runtime-direct-test-cache");

  const auto states = apply_runtime_firewall(
      config, {{"wan", 0x100U}}, cache, firewall,
      FirewallApplyMode::Destructive);

  REQUIRE(states.size() == 5);
  REQUIRE(firewall.recorded_rules.size() == 5);
  CHECK(firewall.calls == std::vector<std::string>{
                               "prepare", "rule", "rule", "rule", "rule",
                               "rule", "apply"});
  CHECK(firewall.recorded_rules[0].action == RecordingFirewall::RuleAction::Mark);
  CHECK(firewall.recorded_rules[0].fwmark == 0x100U);
  CHECK(firewall.recorded_rules[0].criteria.proto == L4Proto::Any);
  CHECK(firewall.recorded_rules[0].criteria.dscp == 46);
  CHECK(firewall.recorded_rules[1].criteria.proto == L4Proto::Tcp);
  CHECK(firewall.recorded_rules[1].criteria.dst_port == PortSpec("443"));
  CHECK(firewall.recorded_rules[2].criteria.proto == L4Proto::Udp);
  CHECK(firewall.recorded_rules[2].criteria.src_port == PortSpec("53"));
  CHECK(firewall.recorded_rules[3].action == RecordingFirewall::RuleAction::Drop);
  CHECK(firewall.recorded_rules[3].criteria.dst_addr ==
        std::vector<std::string>{"203.0.113.7"});
  CHECK(firewall.recorded_rules[4].action == RecordingFirewall::RuleAction::Pass);
  CHECK(firewall.recorded_rules[4].criteria.negate_src_addr);
  CHECK(firewall.recorded_rules[4].criteria.negate_dst_port);
}

TEST_CASE("runtime emits static and dynamic list sets in family order") {
  const Config config = parse_config(R"({
    "daemon": {"firewall_backend":"iptables","ipv6_enabled":true},
    "outbounds": [{"type":"table","tag":"wan","table":100}],
    "lists": {"mixed": {
      "ip_cidrs":["192.0.2.0/24","2001:db8::/32"],
      "domains":["example.test"], "ttl_ms":30000
    }},
    "route": {"rules": [{"list":["mixed"],"outbound":"wan"}]}
  })");
  const bool ipv6_enabled = resolve_ipv6_support(config).enabled;

  RecordingFirewall firewall;
  CacheManager cache("/tmp/keen-pbr-firewall-runtime-list-test-cache");
  const auto states = apply_runtime_firewall(
      config, {{"wan", 0x100U}}, cache, firewall,
      FirewallApplyMode::PreserveSets);

  REQUIRE(states.size() == 1);
  const std::vector<std::string> expected_sets = ipv6_enabled
      ? std::vector<std::string>{"kpbr4_mixed", "kpbr6_mixed",
                                 "kpbr4d_mixed", "kpbr6d_mixed"}
      : std::vector<std::string>{"kpbr4_mixed", "kpbr4d_mixed"};
  CHECK(states.front().set_names == expected_sets);
  CHECK(firewall.set_names == states.front().set_names);
  const std::vector<std::string> expected_calls = ipv6_enabled
      ? std::vector<std::string>{
            "prepare", "set:kpbr4_mixed", "set:kpbr6_mixed",
            "loader:kpbr4_mixed", "loader:kpbr6_mixed", "finish", "finish",
            "set:kpbr4d_mixed", "set:kpbr6d_mixed", "rule", "rule", "rule",
            "rule", "apply"}
      : std::vector<std::string>{"prepare", "set:kpbr4_mixed",
                                 "loader:kpbr4_mixed", "finish",
                                 "set:kpbr4d_mixed", "rule", "rule", "apply"};
  CHECK(firewall.calls == expected_calls);
  CHECK(firewall.streamed_entries == (ipv6_enabled ? 2 : 1));
  CHECK(firewall.finished_loaders == (ipv6_enabled ? 2 : 1));
  REQUIRE(firewall.recorded_rules.size() == (ipv6_enabled ? 4 : 2));
  CHECK(firewall.recorded_rules[0].criteria.dst_set_name == "kpbr4_mixed");
  if (ipv6_enabled) {
    CHECK(firewall.recorded_rules[1].criteria.dst_set_name == "kpbr6_mixed");
    CHECK(firewall.recorded_rules[2].criteria.dst_set_name == "kpbr4d_mixed");
    CHECK(firewall.recorded_rules[3].criteria.dst_set_name == "kpbr6d_mixed");
  } else {
    CHECK(firewall.recorded_rules[1].criteria.dst_set_name == "kpbr4d_mixed");
  }
}

TEST_CASE("runtime captures OUTPUT default-gateway bypass criteria") {
  const Config config = parse_config(R"({
    "daemon": {"firewall_backend":"nftables","ipv6_enabled":false},
    "outbounds": [{"type":"table","tag":"wan","table":100}],
    "route": {"rules": [{"default_gateway":"ipv4","outbound":"wan"}]}
  })");
  RecordingFirewall firewall;
  CacheManager cache("/tmp/keen-pbr-firewall-runtime-gateway-test-cache");
  const std::vector<DumpedRoute> main_routes = {
      {"default", 254, {}, {}, false, false, AF_INET},
      {"192.0.2.0/24", 254, {}, {}, false, false, AF_INET},
      {"::/0", 254, {}, {}, false, false, AF_INET6}};
  DumpedInterface interface;
  interface.ipv4_addresses = {"198.51.100.7/32"};

  (void)apply_runtime_firewall(config, {{"wan", 0x100U}}, cache, firewall,
                               FirewallApplyMode::PreserveSets, nullptr, false,
                               main_routes, {interface});

  REQUIRE(firewall.recorded_rules.size() == 1);
  const auto &criteria = firewall.recorded_rules.front().criteria;
  CHECK(criteria.apply_output);
  CHECK(criteria.default_gateway == DefaultGatewayFamily::Ipv4);
  CHECK(std::find(criteria.default_gateway_bypass.begin(),
                  criteria.default_gateway_bypass.end(),
                  "192.0.2.0/24") != criteria.default_gateway_bypass.end());
  CHECK(std::find(criteria.default_gateway_bypass.begin(),
                  criteria.default_gateway_bypass.end(),
                  "198.51.100.7/32") != criteria.default_gateway_bypass.end());
  CHECK(std::find(criteria.default_gateway_bypass.begin(),
                  criteria.default_gateway_bypass.end(), "default") ==
        criteria.default_gateway_bypass.end());
}

TEST_CASE("runtime passes balance fallback and candidates to the firewall") {
  const Config config = parse_config(R"({
    "daemon": {"firewall_backend":"nftables","ipv6_enabled":false},
    "outbounds": [
      {"type":"table","tag":"wan_a","table":100},
      {"type":"table","tag":"wan_b","table":101},
      {"type":"urltest","tag":"auto","url":"https://example.test",
       "strategy":"balance","outbound_groups":[{"outbounds":["wan_a","wan_b"]}]}
    ],
    "route": {"rules": [{"outbound":"auto","dscp":46}]}
  })");
  RecordingFirewall firewall;
  CacheManager cache("/tmp/keen-pbr-firewall-runtime-balance-test-cache");
  const FirewallBalanceCandidates candidates = {
      {"auto", {{0x200U, true, false}, {0x300U, true, true}}}};

  (void)apply_runtime_firewall(config,
                               {{"auto", 0x100U}, {"wan_a", 0x200U},
                                {"wan_b", 0x300U}},
                               cache, firewall, FirewallApplyMode::PreserveSets,
                               nullptr, false, {}, {}, &candidates);

  REQUIRE(firewall.recorded_rules.size() == 1);
  const auto &rule = firewall.recorded_rules.front();
  CHECK(rule.action == RecordingFirewall::RuleAction::Balance);
  CHECK(rule.fwmark == 0x100U);
  REQUIRE(rule.candidates.size() == candidates.at("auto").size());
  for (size_t index = 0; index < rule.candidates.size(); ++index) {
    CHECK(rule.candidates[index].fwmark == candidates.at("auto")[index].fwmark);
    CHECK(rule.candidates[index].ipv4 == candidates.at("auto")[index].ipv4);
    CHECK(rule.candidates[index].ipv6 == candidates.at("auto")[index].ipv6);
  }
  CHECK(rule.criteria.dscp == 46);
}

TEST_CASE("runtime emits DNS detours as OUTPUT TCP/UDP rules") {
  const Config config = parse_config(R"({
    "daemon": {"ipv6_enabled":false},
    "outbounds": [{"type":"table","tag":"wan","table":100}],
    "dns": {"servers":[{"tag":"upstream","address":"192.0.2.53:5353",
                            "detour":"wan"}]}
  })");
  RecordingFirewall firewall;
  CacheManager cache("/tmp/keen-pbr-firewall-runtime-dns-test-cache");

  (void)apply_runtime_firewall(
      config, {{internal_detour_mark_key("wan"), 0x200U}}, cache, firewall,
      FirewallApplyMode::PreserveSets);

  REQUIRE(firewall.recorded_rules.size() == 1);
  const auto &rule = firewall.recorded_rules.front();
  CHECK(rule.action == RecordingFirewall::RuleAction::Mark);
  CHECK(rule.fwmark == 0x200U);
  CHECK(rule.criteria.proto == L4Proto::TcpUdp);
  CHECK(rule.criteria.dst_port == PortSpec("5353"));
  CHECK(rule.criteria.dst_addr == std::vector<std::string>{"192.0.2.53"});
  CHECK(rule.criteria.apply_output);
}

TEST_CASE("runtime leaves inactive and empty route cases without rules") {
  const Config config = parse_config(R"({
    "daemon": {"ipv6_enabled":false},
    "outbounds": [{"type":"table","tag":"wan","table":100}],
    "lists": {"empty": {"ip_cidrs":[]}},
    "route": {"rules":[
      {"outbound":"wan","dscp":46,"enabled":false},
      {"outbound":"missing","dscp":47},
      {"list":["empty"],"outbound":"wan"}
    ]}
  })");
  RecordingFirewall firewall;
  CacheManager cache("/tmp/keen-pbr-firewall-runtime-noop-test-cache");

  const auto states = apply_runtime_firewall(
      config, {{"wan", 0x100U}}, cache, firewall,
      FirewallApplyMode::StaticSetsOnly);

  REQUIRE(states.size() == 3);
  CHECK(states[0].action_type == RuleActionType::Skip);
  CHECK(states[1].action_type == RuleActionType::Skip);
  CHECK(firewall.recorded_rules.empty());
  CHECK(firewall.calls == std::vector<std::string>{"prepare", "apply"});
}

TEST_CASE("runtime forwards every apply mode and preserves RulesOnly no-streaming") {
  const Config config = parse_config(R"({
    "daemon": {"ipv6_enabled":false},
    "outbounds": [{"type":"table","tag":"wan","table":100}],
    "route": {"rules":[{"outbound":"wan","dscp":46}]}
  })");
  CacheManager cache("/tmp/keen-pbr-firewall-runtime-modes-test-cache");
  for (const auto mode : {FirewallApplyMode::Destructive,
                          FirewallApplyMode::PreserveSets,
                          FirewallApplyMode::StaticSetsOnly,
                          FirewallApplyMode::RulesOnly}) {
    RecordingFirewall firewall;
    (void)apply_runtime_firewall(config, {{"wan", 0x100U}}, cache, firewall,
                                 mode);
    REQUIRE(firewall.prepared_modes == std::vector<FirewallApplyMode>{mode});
    REQUIRE(firewall.applied_modes == std::vector<FirewallApplyMode>{mode});
    CHECK(firewall.stream_count == 0);
  }
}

TEST_CASE("RulesOnly reuses aligned empty file list without streaming") {
  const Config config = empty_source_list_config();
  RulesOnlyFirewall firewall;
  const OutboundMarkMap marks{{"wan", 1}};
  const std::vector<RuleState> previous{aligned_empty_rule_state()};
  CacheManager cache("/tmp/keen-pbr-rules-only-test-cache");

  const auto states = apply_runtime_firewall(
      config, marks, cache, firewall, FirewallApplyMode::RulesOnly,
      &previous);

  REQUIRE(states.size() == 1);
  CHECK(firewall.applied_mode == FirewallApplyMode::RulesOnly);
  CHECK(firewall.set_declarations == 0);
  CHECK(firewall.stream_count == 0);
}

TEST_CASE("RulesOnly falls back when realized rule state is missing") {
  const Config config = empty_source_list_config();
  RulesOnlyFirewall firewall;
  const OutboundMarkMap marks{{"wan", 1}};
  CacheManager cache("/tmp/keen-pbr-rules-only-test-cache");

  CHECK_THROWS_AS(apply_runtime_firewall(
                      config, marks, cache, firewall,
                  FirewallApplyMode::RulesOnly, nullptr),
                  std::exception);
  REQUIRE(firewall.prepared_modes.size() == 2);
  CHECK(firewall.prepared_modes[0] == FirewallApplyMode::RulesOnly);
  CHECK(firewall.prepared_modes[1] == FirewallApplyMode::PreserveSets);
}

TEST_CASE("RulesOnly falls back when realized rule state is misaligned") {
  const Config config = empty_source_list_config();
  RulesOnlyFirewall firewall;
  const OutboundMarkMap marks{{"wan", 1}};
  RuleState misaligned = aligned_empty_rule_state();
  misaligned.rule_index = 1;
  const std::vector<RuleState> previous{misaligned};
  CacheManager cache("/tmp/keen-pbr-rules-only-test-cache");

  CHECK_THROWS_AS(apply_runtime_firewall(
                      config, marks, cache, firewall,
                      FirewallApplyMode::RulesOnly, &previous),
                  std::exception);
  REQUIRE(firewall.prepared_modes.size() == 2);
  CHECK(firewall.prepared_modes[0] == FirewallApplyMode::RulesOnly);
  CHECK(firewall.prepared_modes[1] == FirewallApplyMode::PreserveSets);
}

TEST_CASE("RulesOnly reuses aligned empty URL list without streaming") {
  const Config config = empty_url_list_config();
  RulesOnlyFirewall firewall;
  const OutboundMarkMap marks{{"wan", 1}};
  const std::vector<RuleState> previous{aligned_empty_rule_state()};
  CacheManager cache("/tmp/keen-pbr-rules-only-test-cache");

  (void)apply_runtime_firewall(config, marks, cache, firewall,
                               FirewallApplyMode::RulesOnly, &previous);

  CHECK(firewall.applied_mode == FirewallApplyMode::RulesOnly);
  CHECK(firewall.set_declarations == 0);
  CHECK(firewall.stream_count == 0);
}

TEST_CASE("RulesOnly trusts realized empty state after inline entries parse away") {
  const Config config = invalid_inline_list_config();
  RulesOnlyFirewall firewall;
  const OutboundMarkMap marks{{"wan", 1}};
  const std::vector<RuleState> previous{aligned_empty_rule_state()};
  CacheManager cache("/tmp/keen-pbr-rules-only-test-cache");

  (void)apply_runtime_firewall(config, marks, cache, firewall,
                               FirewallApplyMode::RulesOnly, &previous);

  CHECK(firewall.applied_mode == FirewallApplyMode::RulesOnly);
  CHECK(firewall.set_declarations == 0);
  CHECK(firewall.stream_count == 0);
}

TEST_CASE("RulesOnly fallback preserves and materializes a valid list") {
  const Config config = valid_inline_list_config();
  RecordingFirewall firewall;
  firewall.fail_rules_only = true;
  const OutboundMarkMap marks{{"wan", 1}};
  const std::vector<RuleState> previous{aligned_static_rule_state()};
  CacheManager cache("/tmp/keen-pbr-rules-only-valid-list-test-cache");

  const auto states = apply_runtime_firewall(
      config, marks, cache, firewall, FirewallApplyMode::RulesOnly,
      &previous);

  REQUIRE(states.size() == 1);
  CHECK(firewall.prepared_modes == std::vector<FirewallApplyMode>{
                                     FirewallApplyMode::RulesOnly,
                                     FirewallApplyMode::PreserveSets});
  CHECK(firewall.applied_modes == std::vector<FirewallApplyMode>{
                                    FirewallApplyMode::RulesOnly,
                                    FirewallApplyMode::PreserveSets});
  CHECK(firewall.stream_count == 1);
  CHECK(firewall.streamed_entries == 1);
  CHECK(firewall.finished_loaders == 1);
  CHECK(firewall.rule_count > 0);
  CHECK(states.front().set_names == std::vector<std::string>{"kpbr4_remote"});
  CHECK(std::find(firewall.referenced_sets.begin(), firewall.referenced_sets.end(),
                  "kpbr4_remote") != firewall.referenced_sets.end());
}

TEST_CASE("RulesOnly rejects a realized static set from the other generation") {
  const Config config = valid_inline_list_config();
  RecordingFirewall firewall;
  firewall.generation_names = true;
  const OutboundMarkMap marks{{"wan", 1}};
  RuleState previous = aligned_static_rule_state();
  previous.set_names = {"kpbr4S_remote"};
  const std::vector<RuleState> previous_states{previous};
  CacheManager cache("/tmp/keen-pbr-rules-only-stale-set-test-cache");

  const auto states = apply_runtime_firewall(
      config, marks, cache, firewall, FirewallApplyMode::RulesOnly,
      &previous_states);

  REQUIRE(states.size() == 1);
  CHECK(firewall.prepared_modes == std::vector<FirewallApplyMode>{
                                     FirewallApplyMode::RulesOnly,
                                     FirewallApplyMode::PreserveSets});
  CHECK(firewall.applied_modes ==
        std::vector<FirewallApplyMode>{FirewallApplyMode::PreserveSets});
  CHECK(firewall.stream_count == 1);
  CHECK(firewall.streamed_entries == 1);
  CHECK(states.front().set_names == std::vector<std::string>{"kpbr4s_remote"});
}

TEST_CASE("RulesOnly validates IPv6 stale static sets even when IPv4 is present") {
  if (!system_ipv6_supported()) {
    MESSAGE("IPv6 is unavailable in the test environment; regression skipped");
    return;
  }

  const auto sandbox = std::filesystem::temp_directory_path() /
                       ("keen-pbr-firewall-runtime-ipv6-" +
                        std::to_string(static_cast<long long>(getpid())));
  std::filesystem::remove_all(sandbox);
  std::filesystem::create_directories(sandbox);
  write_executable(sandbox / "ip6tables", "#!/bin/sh\nexit 0\n");
  write_executable(sandbox / "ip6tables-restore", "#!/bin/sh\nexit 0\n");

  PathGuard path_guard;
  const char *old_path = std::getenv("PATH");
  const std::string path = sandbox.string() + ":" +
                           (old_path == nullptr ? std::string{} : old_path);
  REQUIRE(setenv("PATH", path.c_str(), 1) == 0);

  const Config config = valid_inline_ipv6_list_config();
  RecordingFirewall firewall;
  firewall.generation_names = true;
  const OutboundMarkMap marks{{"wan", 1}};
  RuleState previous = aligned_static_rule_state();
  previous.set_names = {"kpbr4s_remote", "kpbr6S_remote"};
  const std::vector<RuleState> previous_states{previous};
  CacheManager cache("/tmp/keen-pbr-rules-only-stale-ipv6-test-cache");

  const auto states = apply_runtime_firewall(
      config, marks, cache, firewall, FirewallApplyMode::RulesOnly,
      &previous_states);

  REQUIRE(states.size() == 1);
  CHECK(firewall.prepared_modes == std::vector<FirewallApplyMode>{
                                     FirewallApplyMode::RulesOnly,
                                     FirewallApplyMode::PreserveSets});
  CHECK(firewall.applied_modes ==
        std::vector<FirewallApplyMode>{FirewallApplyMode::PreserveSets});
  // PreserveSets creates one loader per enabled address family.  The inline
  // list contains only IPv4 data, so only the IPv4 loader receives an entry.
  CHECK(firewall.stream_count == 2);
  CHECK(firewall.streamed_entries == 1);
  CHECK(firewall.finished_loaders == 2);
  CHECK(states.front().set_names ==
        std::vector<std::string>{"kpbr4s_remote", "kpbr6s_remote"});
  CHECK(firewall.set_names ==
        std::vector<std::string>{"kpbr4s_remote", "kpbr6s_remote"});
  std::filesystem::remove_all(sandbox);
}

} // namespace keen_pbr3
