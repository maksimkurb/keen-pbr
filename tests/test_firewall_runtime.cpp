#include <doctest/doctest.h>

#include "../src/cache/cache_manager.hpp"
#include "../src/firewall/firewall_runtime.hpp"

#include <memory>
#include <stdexcept>
#include <vector>

namespace keen_pbr3 {

namespace {

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

} // namespace

TEST_CASE("RulesOnly reuses aligned empty file list without streaming") {
  const Config config = empty_source_list_config();
  RulesOnlyFirewall firewall;
  const OutboundMarkMap marks{{"wan", 1}};
  const std::vector<RuleState> previous{aligned_empty_rule_state()};
  CacheManager cache("/tmp/keen-pbr-rules-only-test-cache");

  const auto states = apply_runtime_firewall(
      config, marks, {}, cache, firewall, FirewallApplyMode::RulesOnly,
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
                      config, marks, {}, cache, firewall,
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
                      config, marks, {}, cache, firewall,
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

  (void)apply_runtime_firewall(config, marks, {}, cache, firewall,
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

  (void)apply_runtime_firewall(config, marks, {}, cache, firewall,
                               FirewallApplyMode::RulesOnly, &previous);

  CHECK(firewall.applied_mode == FirewallApplyMode::RulesOnly);
  CHECK(firewall.set_declarations == 0);
  CHECK(firewall.stream_count == 0);
}

} // namespace keen_pbr3
