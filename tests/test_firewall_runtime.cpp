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
#include <utility>
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

  void create_ipset(const std::string& name, int family, uint32_t timeout) override {
    ++set_declarations;
    set_declarations_detail.push_back({name, family, timeout});
  }

  std::unique_ptr<ListEntryVisitor>
  create_batch_loader(const std::string&) override {
    ++stream_count;
    throw std::runtime_error("RulesOnly unexpectedly requested list streaming");
  }

  void apply(const FirewallPlan& plan, FirewallApplyMode mode) override {
    applied_mode = mode;
    for (const auto& rule : plan.rules) {
      if (std::holds_alternative<MarkAction>(rule.action) ||
          std::holds_alternative<BalanceAction>(rule.action) ||
          std::holds_alternative<VerdictAction>(rule.action)) {
        ++rule_count;
      }
    }
  }

  void cleanup() override {}

  FirewallBackend backend() const override { return FirewallBackend::nftables; }

  int set_declarations{0};
  struct SetDeclaration {
    std::string name;
    int family;
    uint32_t timeout;
  };
  std::vector<SetDeclaration> set_declarations_detail;
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
  void prepare_apply(FirewallApplyMode mode) override {
    prepared_modes.push_back(mode);
    calls.push_back("prepare");
  }

  void create_ipset(const std::string &name, int, uint32_t) override {
    set_names.push_back(name);
    calls.push_back("set:" + name);
  }

  std::unique_ptr<ListEntryVisitor>
  create_batch_loader(const std::string &name) override {
    ++stream_count;
    calls.push_back("loader:" + name);
    return std::make_unique<Visitor>(*this);
  }

  void apply(const FirewallPlan& plan, FirewallApplyMode mode) override {
    applied_modes.push_back(mode);
    for (const auto& rule : plan.rules) {
      if (const auto* mark = std::get_if<MarkAction>(&rule.action)) {
        record_rule(RuleAction::Mark, mark->value, rule.criteria, {});
      } else if (const auto* balance = std::get_if<BalanceAction>(&rule.action)) {
        record_rule(RuleAction::Balance, balance->fallback_mark, rule.criteria,
                    balance->candidates);
      } else if (const auto verdict = std::get_if<VerdictAction>(&rule.action);
                 verdict != nullptr) {
        record_rule(*verdict == VerdictAction::drop ? RuleAction::Drop
                                                    : RuleAction::Pass,
                    0, rule.criteria, {});
      }
    }
    calls.push_back("apply");
    if (fail_apply) {
      throw FirewallError("controlled apply failure");
    }
    if (mode == FirewallApplyMode::RulesOnly && fail_rules_only) {
      fail_rules_only = false;
      throw FirewallRulesOnlyError("controlled RulesOnly preflight failure");
    }
  }

  void cleanup() override {}

  FirewallBackend backend() const override { return backend_type; }

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

  FirewallBackend backend_type{FirewallBackend::nftables};
  bool fail_apply{false};
  bool fail_rules_only{false};
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

Config empty_inline_list_config() {
  return parse_config(R"({
    "outbounds": [{"type":"table","tag":"wan","table":254}],
    "lists": {"remote": {"ip_cidrs":[]}},
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
      FirewallApplyMode::Destructive).rule_states;

  REQUIRE(states.size() == 5);
  REQUIRE(firewall.recorded_rules.size() == 5);
  CHECK(states[0].action_type == RuleActionType::Mark);
  CHECK(states[0].fwmark == 0x100U);
  CHECK(states[1].action_type == RuleActionType::Mark);
  CHECK(states[2].action_type == RuleActionType::Mark);
  CHECK(states[3].action_type == RuleActionType::Drop);
  CHECK(states[4].action_type == RuleActionType::Pass);
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

TEST_CASE("failed firewall apply does not publish a candidate plan") {
  const Config config = parse_config(R"({
    "outbounds": [{"type":"table","tag":"wan","table":100}],
    "route": {"rules": [{"outbound":"wan","dscp":46}]}
  })");
  RecordingFirewall firewall;
  firewall.fail_apply = true;
  CacheManager cache("/tmp/keen-pbr-firewall-runtime-failed-plan-test-cache");

  ActiveFirewall previous;
  previous.plan.fwmark_mask = 0x00FF0000u;
  previous.result.physical_set_names = {"kpbr4_old"};
  FirewallState state;
  state.publish_active_firewall(std::move(previous));
  const auto before = state.active_firewall();

  bool failed = false;
  try {
    // Injected failure inside firewall.apply(): nothing is returned, so the
    // daemon-side publish step is never reached.
    state.publish_active_firewall(apply_runtime_firewall(
        config, {{"wan", 0x100U}}, cache, firewall,
        FirewallApplyMode::PreserveSets, before.get()));
  } catch (const FirewallError&) {
    failed = true;
  }
  CHECK(failed);
  CHECK(firewall.calls.back() == "apply");
  REQUIRE(state.active_firewall() != nullptr);
  CHECK(state.active_firewall() == before);
  CHECK(state.active_firewall()->plan.fwmark_mask == 0x00FF0000u);
  CHECK(state.active_firewall()->result.physical_set_names ==
        std::vector<std::string>{"kpbr4_old"});
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
  const auto applied_result = apply_runtime_firewall(
      config, {{"wan", 0x100U}}, cache, firewall,
      FirewallApplyMode::PreserveSets);
  const auto& states = applied_result.rule_states;
  const auto& applied_plan = applied_result.plan;

  REQUIRE(states.size() == 1);
  const std::vector<std::string> expected_sets = ipv6_enabled
      ? std::vector<std::string>{"kpbr4_mixed", "kpbr6_mixed",
                                 "kpbr4d_mixed", "kpbr6d_mixed"}
      : std::vector<std::string>{"kpbr4_mixed", "kpbr4d_mixed"};
  CHECK(states.front().set_names == expected_sets);
  std::vector<std::string> expected_staged_sets;
  for (const auto& declaration : applied_plan.sets) {
    expected_staged_sets.push_back(firewall.physical_set_name(declaration.name));
  }
  CHECK(firewall.set_names == expected_staged_sets);
  const std::vector<std::string> expected_calls = ipv6_enabled
      ? std::vector<std::string>{
            "prepare", "set:kpbr4_mixed", "set:kpbr4d_mixed",
            "set:kpbr6_mixed", "set:kpbr6d_mixed", "loader:kpbr4_mixed",
            "loader:kpbr6_mixed", "finish", "finish", "rule", "rule", "rule",
            "rule", "apply"}
      : std::vector<std::string>{"prepare", "set:kpbr4_mixed", "set:kpbr4d_mixed",
                                 "loader:kpbr4_mixed", "finish", "rule", "rule",
                                 "apply"};
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

TEST_CASE("runtime projects and streams only finalized list-set declarations") {
  const Config config = parse_config(R"({
    "outbounds": [
      {"type":"table","tag":"wan","table":100},
      {"type":"urltest","tag":"auto","url":"https://example.test",
       "strategy":"balance","outbound_groups":[{"outbounds":["wan"]}]}
    ],
    "lists": {"shared": {"ip_cidrs":["192.0.2.0/24"]}},
    "route": {"rules": [{"list":["shared"],"outbound":"auto"}]}
  })");
  RecordingFirewall firewall;
  CacheManager cache("/tmp/keen-pbr-firewall-runtime-plan-projection-test-cache");

  // The balance route has no runtime mark, so its canonical plan omits the
  // list sets even though list analysis finds static entries.
  const auto applied_result = apply_runtime_firewall(
      config, {{"wan", 0x100U}}, cache, firewall,
      FirewallApplyMode::PreserveSets);
  const auto& states = applied_result.rule_states;
  const auto& applied_plan = applied_result.plan;

  REQUIRE(states.size() == 1);
  CHECK(states.front().action_type == RuleActionType::Skip);
  CHECK(states.front().rule_index == 0);
  CHECK(states.front().list_names == std::vector<std::string>{"shared"});
  CHECK(states.front().outbound_tag == "auto");
  CHECK(states.front().fwmark == 0);
  CHECK(states.front().set_names.empty());
  CHECK(applied_plan.sets.empty());
  CHECK(firewall.set_names.empty());
  CHECK(firewall.stream_count == 0);
  CHECK(firewall.calls == std::vector<std::string>{"prepare", "apply"});
}

TEST_CASE("RulesOnly preserves shared list usage after a skipped first rule") {
  const Config config = parse_config(R"({
    "daemon": {"ipv6_enabled":false},
    "outbounds": [
      {"type":"table","tag":"wan","table":100},
      {"type":"urltest","tag":"auto","url":"https://example.test",
       "strategy":"balance","outbound_groups":[{"outbounds":["wan"]}]}
    ],
    "lists": {"shared": {"ip_cidrs":["192.0.2.0/24"]}},
    "route": {"rules": [
      {"list":["shared"],"outbound":"auto"},
      {"list":["shared"],"outbound":"wan"}
    ]}
  })");
  const OutboundMarkMap marks{{"wan", 0x100U}};
  CacheManager cache("/tmp/keen-pbr-firewall-runtime-shared-rules-only-test-cache");

  RecordingFirewall initial_firewall;
  ActiveFirewall previous_plan;
  previous_plan = apply_runtime_firewall(
      config, marks, cache, initial_firewall, FirewallApplyMode::PreserveSets);
  const auto& previous_states = previous_plan.rule_states;
  REQUIRE(previous_states.size() == 2);
  CHECK(previous_states[0].action_type == RuleActionType::Skip);
  CHECK(previous_states[1].action_type == RuleActionType::Mark);
  CHECK(previous_states[1].set_names == std::vector<std::string>{"kpbr4_shared"});

  RulesOnlyFirewall rules_only_firewall;
  const auto states = apply_runtime_firewall(
      config, marks, cache, rules_only_firewall, FirewallApplyMode::RulesOnly,
      &previous_plan).rule_states;

  REQUIRE(states.size() == 2);
  CHECK(states[0].action_type == RuleActionType::Skip);
  CHECK(states[1].action_type == RuleActionType::Mark);
  CHECK(states[1].set_names == std::vector<std::string>{"kpbr4_shared"});
  CHECK(rules_only_firewall.applied_mode == FirewallApplyMode::RulesOnly);
  CHECK(rules_only_firewall.set_declarations == 1);
  CHECK(rules_only_firewall.rule_count == 1);
}

TEST_CASE("runtime streams a shared static list once per family") {
  const Config config = parse_config(R"({
    "daemon": {"firewall_backend":"nftables","ipv6_enabled":true},
    "outbounds": [{"type":"table","tag":"wan","table":100}],
    "lists": {"shared": {
      "ip_cidrs":["192.0.2.0/24","2001:db8::/32"]
    }},
    "route": {"rules": [
      {"list":["shared"],"outbound":"wan","dscp":46},
      {"list":["shared"],"outbound":"wan","dscp":47}
    ]}
  })");
  const bool ipv6_enabled = resolve_ipv6_support(config).enabled;
  RecordingFirewall firewall;
  CacheManager cache("/tmp/keen-pbr-firewall-runtime-shared-list-test-cache");

  const auto applied_result = apply_runtime_firewall(
      config, {{"wan", 0x100U}}, cache, firewall,
      FirewallApplyMode::PreserveSets);
  const auto& states = applied_result.rule_states;
  const auto& applied_plan = applied_result.plan;

  REQUIRE(states.size() == 2);
  const std::vector<std::string> expected_state_sets =
      ipv6_enabled ? std::vector<std::string>{"kpbr4_shared", "kpbr6_shared"}
                   : std::vector<std::string>{"kpbr4_shared"};
  for (const auto &state : states) {
    CHECK(state.set_names == expected_state_sets);
  }

  std::vector<std::string> plan_set_references;
  for (const auto &rule : applied_plan.rules) {
    if (rule.criteria.dst_set_name.has_value()) {
      plan_set_references.push_back(*rule.criteria.dst_set_name);
    }
  }
  const auto expected_rule_references = ipv6_enabled ? 4U : 2U;
  REQUIRE(plan_set_references.size() == expected_rule_references);
  CHECK(std::count(plan_set_references.begin(), plan_set_references.end(),
                   "kpbr4_shared") == 2);
  if (ipv6_enabled) {
    CHECK(std::count(plan_set_references.begin(), plan_set_references.end(),
                     "kpbr6_shared") == 2);
  }

  CHECK(firewall.stream_count == (ipv6_enabled ? 2 : 1));
  CHECK(firewall.streamed_entries == (ipv6_enabled ? 2 : 1));
  CHECK(firewall.finished_loaders == (ipv6_enabled ? 2 : 1));
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

TEST_CASE("runtime replays only the gateway family for populated route lists") {
  const Config config = parse_config(R"({
    "daemon": {"firewall_backend":"nftables","ipv6_enabled":true},
    "outbounds": [{"type":"table","tag":"wan","table":100}],
    "lists": {"remote": {"ip_cidrs":["192.0.2.0/24"],
                             "domains":["example.test"]}},
    "route": {"rules": [{"list":["remote"],
                            "default_gateway":"ipv4",
                            "proto":"tcp/udp","dest_port":"443",
                            "outbound":"wan"}]}
  })");
  RecordingFirewall firewall;
  CacheManager cache("/tmp/keen-pbr-firewall-runtime-gateway-list-test-cache");

  const auto states = apply_runtime_firewall(
      config, {{"wan", 0x100U}}, cache, firewall,
      FirewallApplyMode::PreserveSets).rule_states;
  const bool ipv6_enabled = resolve_ipv6_support(config).enabled;

  REQUIRE(states.size() == 1);
  const std::vector<std::string> expected_sets =
      ipv6_enabled
          ? std::vector<std::string>{"kpbr4_remote", "kpbr6_remote",
                                     "kpbr4d_remote", "kpbr6d_remote"}
          : std::vector<std::string>{"kpbr4_remote", "kpbr4d_remote"};
  CHECK(states.front().set_names == expected_sets);
  REQUIRE(firewall.recorded_rules.size() == 2);
  CHECK(firewall.referenced_sets ==
        std::vector<std::string>{"kpbr4_remote", "kpbr4d_remote"});
  for (const auto &recorded : firewall.recorded_rules) {
    CHECK(recorded.action == RecordingFirewall::RuleAction::Mark);
    CHECK(recorded.fwmark == 0x100U);
    CHECK(recorded.criteria.default_gateway == DefaultGatewayFamily::Ipv4);
    CHECK(recorded.criteria.proto == L4Proto::TcpUdp);
    CHECK(recorded.criteria.dst_port == PortSpec("443"));
  }
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

  const auto states = apply_runtime_firewall(
      config, {{"auto", 0x100U}, {"wan_a", 0x200U}, {"wan_b", 0x300U}},
      cache, firewall, FirewallApplyMode::PreserveSets, nullptr, false, {}, {},
      &candidates).rule_states;

  REQUIRE(states.size() == 1);
  CHECK(states.front().action_type == RuleActionType::Mark);
  CHECK(states.front().fwmark == 0x100U);
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

TEST_CASE("mixed nftables-only plan fails before backend mutation") {
  const Config config = parse_config(R"({
    "daemon": {"ipv6_enabled": false},
    "outbounds": [
      {"type":"table","tag":"wan_a","table":100},
      {"type":"table","tag":"wan_b","table":101},
      {"type":"urltest","tag":"auto","url":"https://example.test",
       "strategy":"balance","outbound_groups":[{"outbounds":["wan_a","wan_b"]}]}
    ],
    "route": {"rules": [
      {"outbound":"auto","dscp":46},
      {"outbound":"wan_a","default_gateway":"ipv4"}
    ]}
  })");
  RecordingFirewall firewall;
  firewall.backend_type = FirewallBackend::iptables;
  CacheManager cache("/tmp/keen-pbr-firewall-runtime-backend-validation-test-cache");
  const FirewallBalanceCandidates candidates = {
      {"auto", {{0x200U, true, true}, {0x300U, true, true}}}};

  CHECK_THROWS(apply_runtime_firewall(
      config, {{"auto", 0x100U}, {"wan_a", 0x200U}, {"wan_b", 0x300U}},
      cache, firewall, FirewallApplyMode::PreserveSets, nullptr, false, {}, {},
      &candidates));
  CHECK(firewall.calls.empty());
  CHECK(firewall.recorded_rules.empty());
  CHECK(firewall.ipv6_enabled());
  CHECK(firewall.fwmark_mask() == 0xFFFFFFFFU);
  CHECK_FALSE(firewall.clear_dynamic_sets_on_apply());
}

TEST_CASE("RulesOnly validates deferred list-backed actions before preparation") {
  const Config config = parse_config(R"({
    "outbounds": [
      {"type":"table","tag":"wan_a","table":100},
      {"type":"table","tag":"wan_b","table":101},
      {"type":"urltest","tag":"auto","url":"https://example.test",
       "strategy":"balance","outbound_groups":[{"outbounds":["wan_a","wan_b"]}]}
    ],
    "lists": {"remote": {"file":"/path/that-must-never-be-opened"}},
    "route": {"rules": [{"list":["remote"],"outbound":"auto"}]}
  })");
  RecordingFirewall firewall;
  firewall.backend_type = FirewallBackend::iptables;
  CacheManager cache("/tmp/keen-pbr-firewall-runtime-deferred-validation-test-cache");
  const FirewallBalanceCandidates candidates = {
      {"auto", {{0x200U, true, true}, {0x300U, true, true}}}};

  CHECK_THROWS(apply_runtime_firewall(
      config, {{"auto", 0x100U}, {"wan_a", 0x200U}, {"wan_b", 0x300U}},
      cache, firewall, FirewallApplyMode::RulesOnly, nullptr, false, {}, {},
      &candidates));
  CHECK(firewall.calls.empty());
}

TEST_CASE("RulesOnly rejects list-backed balance with an empty active plan") {
  const Config previous_config = empty_inline_list_config();
  const Config config = parse_config(R"({
    "daemon": {"ipv6_enabled":false},
    "outbounds": [
      {"type":"table","tag":"wan","table":254},
      {"type":"urltest","tag":"auto","strategy":"balance",
       "outbound_groups":[{"outbounds":["wan"]}]}
    ],
    "lists": {"remote": {"ip_cidrs":[]}},
    "route": {"rules": [{"list":["remote"],"outbound":"auto"}]}
  })");
  const OutboundMarkMap marks{{"auto", 1}, {"wan", 2}};
  const FirewallBalanceCandidates candidates = {
      {"auto", {{2, true, false}}}};
  CacheManager cache("/tmp/keen-pbr-firewall-runtime-empty-plan-balance-test-cache");
  RecordingFirewall previous_firewall;
  ActiveFirewall previous_plan;
  previous_plan = apply_runtime_firewall(
              previous_config, {{"wan", 1}}, cache, previous_firewall,
              FirewallApplyMode::PreserveSets);
  REQUIRE(previous_plan.rule_states.size() == 1);
  REQUIRE(previous_plan.plan.sets.empty());

  RecordingFirewall firewall;
  firewall.backend_type = FirewallBackend::iptables;
  CHECK_THROWS_AS(apply_runtime_firewall(
                      config, marks, cache, firewall,
                      FirewallApplyMode::RulesOnly, &previous_plan, false, {}, {},
                      &candidates),
                  FirewallError);
  CHECK(firewall.calls.empty());
  CHECK(firewall.prepared_modes.empty());
}

TEST_CASE("RulesOnly rejects list-backed default gateway with an empty active plan") {
  const Config previous_config = empty_inline_list_config();
  const Config config = parse_config(R"({
    "daemon": {"ipv6_enabled":false},
    "outbounds": [{"type":"table","tag":"wan","table":254}],
    "lists": {"remote": {"ip_cidrs":[]}},
    "route": {"rules": [{"list":["remote"],"default_gateway":"ipv4",
                            "outbound":"wan"}]}
  })");
  const OutboundMarkMap marks{{"wan", 1}};
  CacheManager cache("/tmp/keen-pbr-firewall-runtime-empty-plan-gateway-test-cache");
  RecordingFirewall previous_firewall;
  ActiveFirewall previous_plan;
  previous_plan = apply_runtime_firewall(
              previous_config, marks, cache, previous_firewall,
              FirewallApplyMode::PreserveSets);
  REQUIRE(previous_plan.rule_states.size() == 1);
  REQUIRE(previous_plan.plan.sets.empty());

  RecordingFirewall firewall;
  firewall.backend_type = FirewallBackend::iptables;
  CHECK_THROWS_AS(apply_runtime_firewall(
                      config, marks, cache, firewall,
                      FirewallApplyMode::RulesOnly, &previous_plan),
                  FirewallError);
  CHECK(firewall.calls.empty());
  CHECK(firewall.prepared_modes.empty());
}

TEST_CASE("runtime emits DNS detours as OUTPUT TCP/UDP rules") {
  const Config config = parse_config(R"({
    "daemon": {"ipv6_enabled":false},
    "outbounds": [{"type":"table","tag":"wan","table":100}],
    "dns": {"resolver_integration":"dnsmasq","servers":[{"tag":"upstream","address":"192.0.2.53:5353",
                            "detour":"wan"}]}
  })");
  RecordingFirewall firewall;
  CacheManager cache("/tmp/keen-pbr-firewall-runtime-dns-test-cache");

  (void)apply_runtime_firewall(
      config, {{internal_detour_mark_key("wan"), 0x200U}}, cache, firewall,
      FirewallApplyMode::PreserveSets);

  CHECK(firewall.calls == std::vector<std::string>{"prepare", "rule", "rule",
                                                    "apply"});
  REQUIRE(firewall.recorded_rules.size() == 2);
  for (const auto &rule : firewall.recorded_rules) {
    CHECK(rule.action == RecordingFirewall::RuleAction::Mark);
    CHECK(rule.fwmark == 0x200U);
    CHECK(rule.criteria.dst_port == PortSpec("5353"));
    CHECK(rule.criteria.dst_addr == std::vector<std::string>{"192.0.2.53"});
  }
  CHECK(firewall.recorded_rules[0].criteria.proto == L4Proto::Tcp);
  CHECK(firewall.recorded_rules[1].criteria.proto == L4Proto::Udp);
}

TEST_CASE("runtime DNS detour precedence follows configured order") {
  Config config = parse_config(R"({
    "daemon": {"ipv6_enabled":false},
    "outbounds": [
      {"type":"table","tag":"route_z","table":100},
      {"type":"table","tag":"route_a","table":101}
    ],
    "dns": {"resolver_integration":"dnsmasq","servers":[
      {"tag":"upstream_z","address":"192.0.2.54:5353",
       "detour":"route_z"},
      {"tag":"upstream_a","address":"192.0.2.53:5353",
       "detour":"route_a"}
    ]}
  })");
  const OutboundMarkMap marks{
      {internal_detour_mark_key("route_z"), 0x300U},
      {internal_detour_mark_key("route_a"), 0x200U}};
  const auto apply = [&](const Config& candidate) {
    RecordingFirewall firewall;
    CacheManager cache("/tmp/keen-pbr-firewall-runtime-dns-order-test-cache");
    (void)apply_runtime_firewall(candidate, marks, cache, firewall,
                                 FirewallApplyMode::PreserveSets);
    return firewall.recorded_rules;
  };

  const auto first = apply(config);
  REQUIRE(first.size() == 4);
  CHECK(first[0].criteria.dst_addr == std::vector<std::string>{"192.0.2.54"});
  CHECK(first[0].criteria.proto == L4Proto::Tcp);
  CHECK(first[0].fwmark == 0x300U);
  CHECK(first[1].criteria.proto == L4Proto::Udp);
  CHECK(first[2].criteria.dst_addr == std::vector<std::string>{"192.0.2.53"});
  CHECK(first[2].criteria.proto == L4Proto::Tcp);
  CHECK(first[2].fwmark == 0x200U);
  CHECK(first[3].criteria.proto == L4Proto::Udp);

  std::reverse(config.dns->servers->begin(), config.dns->servers->end());
  const auto reversed = apply(config);
  REQUIRE(reversed.size() == first.size());
  CHECK(reversed[0].criteria.dst_addr ==
        std::vector<std::string>{"192.0.2.53"});
  CHECK(reversed[0].criteria.proto == L4Proto::Tcp);
  CHECK(reversed[0].fwmark == 0x200U);
  CHECK(reversed[1].criteria.proto == L4Proto::Udp);
  CHECK(reversed[2].criteria.dst_addr ==
        std::vector<std::string>{"192.0.2.54"});
  CHECK(reversed[2].criteria.proto == L4Proto::Tcp);
  CHECK(reversed[2].fwmark == 0x300U);
  CHECK(reversed[3].criteria.proto == L4Proto::Udp);
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
      FirewallApplyMode::StaticSetsOnly).rule_states;

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
  const Config previous_config = empty_inline_list_config();
  const Config config = empty_source_list_config();
  RecordingFirewall previous_firewall;
  ActiveFirewall previous_plan;
  CacheManager cache("/tmp/keen-pbr-rules-only-test-cache");
  previous_plan = apply_runtime_firewall(
              previous_config, {{"wan", 1}}, cache, previous_firewall,
              FirewallApplyMode::PreserveSets);
  REQUIRE(previous_plan.rule_states.size() == 1);

  RulesOnlyFirewall firewall;
  const OutboundMarkMap marks{{"wan", 1}};

  const auto states = apply_runtime_firewall(
      config, marks, cache, firewall, FirewallApplyMode::RulesOnly,
      &previous_plan).rule_states;

  REQUIRE(states.size() == 1);
  CHECK(firewall.applied_mode == FirewallApplyMode::RulesOnly);
  CHECK(firewall.set_declarations == 0);
  CHECK(firewall.stream_count == 0);
}

TEST_CASE("non-RulesOnly list analysis fails before firewall preparation") {
  const Config config = empty_source_list_config();
  RecordingFirewall firewall;
  CacheManager cache("/tmp/keen-pbr-non-rules-only-analysis-test-cache");

  CHECK_THROWS(apply_runtime_firewall(
      config, {{"wan", 1}}, cache, firewall, FirewallApplyMode::PreserveSets));
  CHECK(firewall.prepared_modes.empty());
  CHECK(firewall.calls.empty());
}

TEST_CASE("RulesOnly falls back when the active plan is missing") {
  const Config config = empty_source_list_config();
  RulesOnlyFirewall firewall;
  const OutboundMarkMap marks{{"wan", 1}};
  CacheManager cache("/tmp/keen-pbr-rules-only-test-cache");

  CHECK_THROWS_AS(apply_runtime_firewall(
                      config, marks, cache, firewall,
                  FirewallApplyMode::RulesOnly, nullptr),
                  std::exception);
  CHECK(firewall.prepared_modes.empty());
}

TEST_CASE("RulesOnly falls back when active plan list identity is unknown") {
  const Config previous_config = parse_config(R"({
    "outbounds": [{"type":"table","tag":"wan","table":254}],
    "lists": {"other": {"ip_cidrs":[]}},
    "route": {"rules": [{"list":["other"],"outbound":"wan"}]}
  })");
  const Config config = empty_source_list_config();
  RecordingFirewall previous_firewall;
  ActiveFirewall previous_plan;
  CacheManager cache("/tmp/keen-pbr-rules-only-test-cache");
  previous_plan = apply_runtime_firewall(
              previous_config, {{"wan", 1}}, cache, previous_firewall,
              FirewallApplyMode::PreserveSets);
  REQUIRE(previous_plan.rule_states.size() == 1);

  RulesOnlyFirewall firewall;
  const OutboundMarkMap marks{{"wan", 1}};

  CHECK_THROWS_AS(apply_runtime_firewall(
                      config, marks, cache, firewall,
                      FirewallApplyMode::RulesOnly, &previous_plan),
                  std::exception);
  CHECK(firewall.prepared_modes.empty());
}

TEST_CASE("RulesOnly reuses aligned empty URL list without streaming") {
  const Config previous_config = empty_inline_list_config();
  const Config config = empty_url_list_config();
  RecordingFirewall previous_firewall;
  ActiveFirewall previous_plan;
  CacheManager cache("/tmp/keen-pbr-rules-only-test-cache");
  previous_plan = apply_runtime_firewall(
              previous_config, {{"wan", 1}}, cache, previous_firewall,
              FirewallApplyMode::PreserveSets);
  REQUIRE(previous_plan.rule_states.size() == 1);

  RulesOnlyFirewall firewall;
  const OutboundMarkMap marks{{"wan", 1}};

  (void)apply_runtime_firewall(config, marks, cache, firewall,
                               FirewallApplyMode::RulesOnly, &previous_plan);

  CHECK(firewall.applied_mode == FirewallApplyMode::RulesOnly);
  CHECK(firewall.set_declarations == 0);
  CHECK(firewall.stream_count == 0);
}

TEST_CASE("RulesOnly trusts realized empty state after inline entries parse away") {
  const Config previous_config = empty_inline_list_config();
  const Config config = invalid_inline_list_config();
  RecordingFirewall previous_firewall;
  ActiveFirewall previous_plan;
  CacheManager cache("/tmp/keen-pbr-rules-only-test-cache");
  previous_plan = apply_runtime_firewall(
              previous_config, {{"wan", 1}}, cache, previous_firewall,
              FirewallApplyMode::PreserveSets);
  REQUIRE(previous_plan.rule_states.size() == 1);

  RulesOnlyFirewall firewall;
  const OutboundMarkMap marks{{"wan", 1}};

  (void)apply_runtime_firewall(config, marks, cache, firewall,
                               FirewallApplyMode::RulesOnly, &previous_plan);

  CHECK(firewall.applied_mode == FirewallApplyMode::RulesOnly);
  CHECK(firewall.set_declarations == 0);
  CHECK(firewall.stream_count == 0);
}

TEST_CASE("RulesOnly fallback preserves and materializes a valid list") {
  const Config config = valid_inline_list_config();
  RecordingFirewall previous_firewall;
  ActiveFirewall previous_plan;
  RecordingFirewall firewall;
  firewall.fail_rules_only = true;
  const OutboundMarkMap marks{{"wan", 1}};
  CacheManager cache("/tmp/keen-pbr-rules-only-valid-list-test-cache");
  previous_plan = apply_runtime_firewall(
              config, marks, cache, previous_firewall,
              FirewallApplyMode::PreserveSets);
  REQUIRE(previous_plan.rule_states.size() == 1);

  const auto states = apply_runtime_firewall(
      config, marks, cache, firewall, FirewallApplyMode::RulesOnly,
      &previous_plan).rule_states;

  REQUIRE(states.size() == 1);
  CHECK(firewall.prepared_modes ==
        std::vector<FirewallApplyMode>{FirewallApplyMode::RulesOnly,
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

TEST_CASE("RulesOnly falls back when the previous result lacks a planned set") {
  const Config config = valid_inline_list_config();
  const OutboundMarkMap marks{{"wan", 1}};
  RecordingFirewall previous_firewall;
  ActiveFirewall previous_plan;
  CacheManager cache("/tmp/keen-pbr-rules-only-stale-set-test-cache");
  previous_plan = apply_runtime_firewall(
              config, marks, cache, previous_firewall,
              FirewallApplyMode::PreserveSets);
  REQUIRE(previous_plan.rule_states.size() == 1);
  // The previous apply did not realize the set (e.g. the list is new).
  previous_plan.result.physical_set_names.clear();

  RecordingFirewall firewall;

  const auto states = apply_runtime_firewall(
      config, marks, cache, firewall, FirewallApplyMode::RulesOnly,
      &previous_plan).rule_states;

  REQUIRE(states.size() == 1);
  CHECK(firewall.prepared_modes ==
        std::vector<FirewallApplyMode>{FirewallApplyMode::RulesOnly,
                                       FirewallApplyMode::PreserveSets});
  CHECK(firewall.applied_modes ==
        std::vector<FirewallApplyMode>{FirewallApplyMode::PreserveSets});
  CHECK(firewall.stream_count == 1);
  CHECK(firewall.streamed_entries == 1);
  CHECK(states.front().set_names == std::vector<std::string>{"kpbr4_remote"});
}

TEST_CASE("RulesOnly validates the active plan IPv6 static set") {
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
  const OutboundMarkMap marks{{"wan", 1}};
  RecordingFirewall previous_firewall;
  ActiveFirewall previous_plan;
  CacheManager cache("/tmp/keen-pbr-rules-only-stale-ipv6-test-cache");
  previous_plan = apply_runtime_firewall(
              config, marks, cache, previous_firewall,
              FirewallApplyMode::PreserveSets);
  REQUIRE(previous_plan.rule_states.size() == 1);

  // The previous apply did not realize the IPv6 set.
  previous_plan.result.physical_set_names.erase(
      std::remove(previous_plan.result.physical_set_names.begin(),
                  previous_plan.result.physical_set_names.end(), "kpbr6_remote"),
      previous_plan.result.physical_set_names.end());

  RecordingFirewall firewall;
  const auto states = apply_runtime_firewall(
      config, marks, cache, firewall, FirewallApplyMode::RulesOnly,
      &previous_plan).rule_states;

  REQUIRE(states.size() == 1);
  CHECK(firewall.prepared_modes == std::vector<FirewallApplyMode>{
                                     FirewallApplyMode::RulesOnly,
                                     FirewallApplyMode::PreserveSets});
  CHECK(firewall.applied_modes ==
        std::vector<FirewallApplyMode>{FirewallApplyMode::PreserveSets});
  CHECK(firewall.stream_count == 2);
  CHECK(states.front().set_names ==
        std::vector<std::string>{"kpbr4_remote", "kpbr6_remote"});
  std::filesystem::remove_all(sandbox);
}

TEST_CASE("RulesOnly reuses dynamic timeout from the active plan") {
  const Config previous_config = parse_config(R"({
    "daemon": {"ipv6_enabled":false},
    "outbounds": [{"type":"table","tag":"wan","table":254}],
    "lists": {"remote": {"domains":["example.test"],"ttl_ms":30000}},
    "route": {"rules": [{"list":["remote"],"outbound":"wan"}]}
  })");
  const OutboundMarkMap marks{{"wan", 1}};
  RecordingFirewall previous_firewall;
  ActiveFirewall previous_plan;
  CacheManager cache("/tmp/keen-pbr-rules-only-dynamic-timeout-test-cache");
  previous_plan = apply_runtime_firewall(
              previous_config, marks, cache, previous_firewall,
              FirewallApplyMode::PreserveSets);
  REQUIRE(previous_plan.rule_states.size() == 1);

  RulesOnlyFirewall firewall;
  const auto states = apply_runtime_firewall(
      previous_config, marks, cache, firewall, FirewallApplyMode::RulesOnly,
      &previous_plan).rule_states;
  REQUIRE(states.size() == 1);
  const auto dynamic_set = std::find_if(
      firewall.set_declarations_detail.begin(),
      firewall.set_declarations_detail.end(),
      [](const RulesOnlyFirewall::SetDeclaration& declaration) {
        return declaration.name == "kpbr4d_remote";
      });
  REQUIRE(dynamic_set != firewall.set_declarations_detail.end());
  CHECK(dynamic_set->timeout == 30U);
}

TEST_CASE("RulesOnly falls back when dynamic timeout changes") {
  const Config previous_config = parse_config(R"({
    "daemon": {"ipv6_enabled":false},
    "outbounds": [{"type":"table","tag":"wan","table":254}],
    "lists": {"remote": {"domains":["example.test"],"ttl_ms":30000}},
    "route": {"rules": [{"list":["remote"],"outbound":"wan"}]}
  })");
  Config config = previous_config;
  config.lists->at("remote").ttl_ms = 60000;
  const OutboundMarkMap marks{{"wan", 1}};
  RecordingFirewall previous_firewall;
  ActiveFirewall previous_plan;
  CacheManager cache("/tmp/keen-pbr-rules-only-dynamic-timeout-change-test-cache");
  previous_plan = apply_runtime_firewall(
              previous_config, marks, cache, previous_firewall,
              FirewallApplyMode::PreserveSets);
  REQUIRE(previous_plan.rule_states.size() == 1);

  RulesOnlyFirewall firewall;
  const auto states = apply_runtime_firewall(
      config, marks, cache, firewall, FirewallApplyMode::RulesOnly,
      &previous_plan).rule_states;
  REQUIRE(states.size() == 1);
  CHECK(firewall.prepared_modes ==
        std::vector<FirewallApplyMode>{FirewallApplyMode::PreserveSets});
  CHECK(firewall.applied_mode == FirewallApplyMode::PreserveSets);
  const auto dynamic_set = std::find_if(
      firewall.set_declarations_detail.begin(),
      firewall.set_declarations_detail.end(),
      [](const RulesOnlyFirewall::SetDeclaration& declaration) {
        return declaration.name == "kpbr4d_remote";
      });
  REQUIRE(dynamic_set != firewall.set_declarations_detail.end());
  CHECK(dynamic_set->timeout == 60U);
}

namespace {

// FirewallPlan has no operator==; compare the fields that carry intent.
std::string plan_fingerprint(const FirewallPlan& plan) {
  std::string out = std::to_string(plan.fwmark_mask) + "|";
  for (const auto& rule : plan.rules) {
    out += rule.key.module_id + "/" + rule.key.instance_id + ":" +
           std::to_string(static_cast<int>(rule.stage)) + ":" +
           std::to_string(rule.priority) + ":" +
           std::to_string(rule.insertion_order) + ":" +
           rule.criteria.dst_set_name.value_or("-") + ";";
  }
  out += "|";
  for (const auto& set : plan.sets) {
    out += set.name + ":" + std::to_string(set.timeout) + ";";
  }
  for (const auto& name : plan.referenced_list_names) {
    out += "|" + name;
  }
  return out;
}

} // namespace

TEST_CASE("successful apply returns consistent plan, result and rule states") {
  const Config config = valid_inline_list_config();
  const OutboundMarkMap marks{{"wan", 1}};
  CacheManager cache("/tmp/keen-pbr-firewall-runtime-active-test-cache");
  RecordingFirewall firewall;

  const auto active = apply_runtime_firewall(
      config, marks, cache, firewall, FirewallApplyMode::PreserveSets);

  CHECK(active.result.mode == FirewallApplyMode::PreserveSets);
  REQUIRE(active.rule_states.size() == 1);
  CHECK(active.rule_states.front().set_names ==
        std::vector<std::string>{"kpbr4_remote"});
  REQUIRE(active.plan.sets.size() == 1);
  CHECK(active.plan.sets.front().name == "kpbr4_remote");
  CHECK(active.result.has_physical_set("kpbr4_remote"));
}

TEST_CASE("preparation failure keeps the previous active firewall") {
  // A list-backed default gateway cannot be planned for iptables from an empty
  // active plan; planning fails before the backend is touched.
  const Config previous_config = empty_inline_list_config();
  const Config config = parse_config(R"({
    "daemon": {"ipv6_enabled":false},
    "outbounds": [{"type":"table","tag":"wan","table":254}],
    "lists": {"remote": {"ip_cidrs":[]}},
    "route": {"rules": [{"list":["remote"],"default_gateway":"ipv4",
                            "outbound":"wan"}]}
  })");
  const OutboundMarkMap marks{{"wan", 1}};
  CacheManager cache("/tmp/keen-pbr-firewall-runtime-prep-fail-test-cache");
  RecordingFirewall previous_firewall;
  FirewallState state;
  state.publish_active_firewall(apply_runtime_firewall(
      previous_config, marks, cache, previous_firewall,
      FirewallApplyMode::PreserveSets));
  const auto before = state.active_firewall();
  REQUIRE(before != nullptr);

  RecordingFirewall firewall;
  firewall.backend_type = FirewallBackend::iptables;
  CHECK_THROWS_AS(state.publish_active_firewall(apply_runtime_firewall(
                      config, marks, cache, firewall,
                      FirewallApplyMode::RulesOnly, before.get())),
                  FirewallError);
  CHECK(firewall.calls.empty());
  CHECK(state.active_firewall() == before);
}

TEST_CASE("RulesOnly with a matching previous result stays RulesOnly") {
  const Config config = valid_inline_list_config();
  const OutboundMarkMap marks{{"wan", 1}};
  CacheManager cache("/tmp/keen-pbr-rules-only-match-test-cache");
  RecordingFirewall previous_firewall;
  const auto previous = apply_runtime_firewall(
      config, marks, cache, previous_firewall, FirewallApplyMode::PreserveSets);

  RecordingFirewall firewall;
  const auto active = apply_runtime_firewall(
      config, marks, cache, firewall, FirewallApplyMode::RulesOnly, &previous);

  CHECK(firewall.prepared_modes ==
        std::vector<FirewallApplyMode>{FirewallApplyMode::RulesOnly});
  CHECK(firewall.applied_modes ==
        std::vector<FirewallApplyMode>{FirewallApplyMode::RulesOnly});
  CHECK(active.result.mode == FirewallApplyMode::RulesOnly);
  CHECK(active.result.physical_set_names == previous.result.physical_set_names);
}

TEST_CASE("RulesOnly with a stale previous result falls back exactly once") {
  const Config config = valid_inline_list_config();
  const OutboundMarkMap marks{{"wan", 1}};
  CacheManager cache("/tmp/keen-pbr-rules-only-stale-result-test-cache");
  RecordingFirewall previous_firewall;
  const auto previous = apply_runtime_firewall(
      config, marks, cache, previous_firewall, FirewallApplyMode::PreserveSets);

  // The previous result no longer lists the set the plan needs.
  auto stale = previous;
  stale.result.physical_set_names = {"kpbr4_other"};
  RecordingFirewall firewall;
  const auto active = apply_runtime_firewall(
      config, marks, cache, firewall, FirewallApplyMode::RulesOnly, &stale);

  CHECK(firewall.prepared_modes ==
        std::vector<FirewallApplyMode>{FirewallApplyMode::RulesOnly,
                                       FirewallApplyMode::PreserveSets});
  CHECK(active.result.mode == FirewallApplyMode::PreserveSets);
  CHECK(active.result.physical_set_names ==
        std::vector<std::string>{"kpbr4_remote"});
}

TEST_CASE("FirewallState readers get one coherent snapshot across a publish") {
  FirewallState state;
  CHECK(state.active_firewall() == nullptr);
  CHECK(state.get_rules().empty());

  ActiveFirewall first;
  first.plan.fwmark_mask = 1;
  first.result.physical_set_names = {"a"};
  first.rule_states.resize(1);
  state.publish_active_firewall(std::move(first));
  const auto held = state.active_firewall();
  const FirewallState copy = state;

  ActiveFirewall second;
  second.plan.fwmark_mask = 2;
  second.result.physical_set_names = {"b"};
  second.rule_states.resize(2);
  state.publish_active_firewall(std::move(second));

  // A held snapshot (and a copied state) keeps the complete first apply.
  CHECK(held->plan.fwmark_mask == 1);
  CHECK(held->result.physical_set_names == std::vector<std::string>{"a"});
  CHECK(held->rule_states.size() == 1);
  CHECK(copy.active_firewall() == held);
  CHECK(state.active_firewall()->plan.fwmark_mask == 2);
  CHECK(state.get_rules().size() == 2);

  state.clear_active_firewall();
  CHECK(state.active_firewall() == nullptr);
  CHECK(held->rule_states.size() == 1);
}

namespace {

class CollectingVisitor final : public ListEntryVisitor {
public:
  void on_entry(EntryType type, std::string_view entry) override {
    entries.emplace_back(type, std::string(entry));
  }
  std::vector<std::pair<EntryType, std::string>> entries;
};

} // namespace

TEST_CASE("IpFamilySplitVisitor routes entries by address family") {
  CollectingVisitor v4;
  CollectingVisitor v6;
  IpFamilySplitVisitor splitter(&v4, &v6, "test");

  splitter.on_entry(EntryType::Ip, "192.0.2.1");
  splitter.on_entry(EntryType::Cidr, "192.0.2.0/24");
  splitter.on_entry(EntryType::Ip, "2001:db8::1");
  splitter.on_entry(EntryType::Cidr, "2001:db8::/32");
  splitter.on_entry(EntryType::Ip, "::ffff:192.0.2.1");  // IPv4-mapped IPv6
  splitter.on_entry(EntryType::Cidr, "::ffff:192.0.2.0/120");
  splitter.on_entry(EntryType::Domain, "example.test");

  using Entries = std::vector<std::pair<EntryType, std::string>>;
  CHECK(v4.entries == Entries{{EntryType::Ip, "192.0.2.1"},
                              {EntryType::Cidr, "192.0.2.0/24"}});
  CHECK(v6.entries == Entries{{EntryType::Ip, "2001:db8::1"},
                              {EntryType::Cidr, "2001:db8::/32"},
                              {EntryType::Ip, "::ffff:192.0.2.1"},
                              {EntryType::Cidr, "::ffff:192.0.2.0/120"}});
  CHECK(splitter.invalid_entries() == 0);
}

TEST_CASE("IpFamilySplitVisitor never sends invalid entries to the IPv4 loader") {
  CollectingVisitor v4;
  CollectingVisitor v6;
  IpFamilySplitVisitor splitter(&v4, &v6, "test");

  splitter.on_entry(EntryType::Ip, "not-an-ip");
  splitter.on_entry(EntryType::Ip, "192.0.2");
  splitter.on_entry(EntryType::Ip, "1.2.3.4:80");
  splitter.on_entry(EntryType::Ip, "2001:db8::zz");
  splitter.on_entry(EntryType::Ip, "fe80::1%eth0");
  splitter.on_entry(EntryType::Ip, "192.0.2.0/24");  // prefix on an Ip entry
  splitter.on_entry(EntryType::Cidr, "192.0.2.1");   // Cidr without prefix
  splitter.on_entry(EntryType::Cidr, "2001:db8::/");
  splitter.on_entry(EntryType::Ip, "");

  CHECK(v4.entries.empty());
  CHECK(v6.entries.empty());
  CHECK(splitter.invalid_entries() == 9);

  splitter.on_entry(EntryType::Ip, "192.0.2.9");
  CHECK(v4.entries.size() == 1);
}

TEST_CASE("IpFamilySplitVisitor tolerates a missing family loader") {
  CollectingVisitor v4;
  IpFamilySplitVisitor v4_only(&v4, nullptr, "test");
  v4_only.on_entry(EntryType::Ip, "2001:db8::1");
  v4_only.on_entry(EntryType::Ip, "192.0.2.1");
  CHECK(v4.entries.size() == 1);

  CollectingVisitor v6;
  IpFamilySplitVisitor v6_only(nullptr, &v6, "test");
  v6_only.on_entry(EntryType::Ip, "192.0.2.1");
  v6_only.on_entry(EntryType::Ip, "2001:db8::1");
  CHECK(v6.entries.size() == 1);
}

} // namespace keen_pbr3
