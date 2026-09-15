#include <doctest/doctest.h>

#include "../src/firewall/firewall_rule.hpp"

#include <stdexcept>

namespace keen_pbr3 {

TEST_CASE("FirewallRuleKey comments round trip") {
  const FirewallRuleKey key{"route.mark", "outbound-vpn"};
  CHECK(key.comment() == "kpbr:v1:route.mark:outbound-vpn");
  CHECK(FirewallRuleKey::from_comment(key.comment()) == key);
}

TEST_CASE("FirewallRuleKey rejects malformed comments and IDs") {
  CHECK_THROWS_AS(FirewallRuleKey::from_comment("kpbr:v2:route.mark:one"),
                  std::invalid_argument);
  CHECK_THROWS_AS(FirewallRuleKey::from_comment("kpbr:v1:route.mark"),
                  std::invalid_argument);
  CHECK_THROWS_AS(FirewallRuleKey::from_comment("kpbr:v1:route.mark:one:two"),
                  std::invalid_argument);
  const FirewallRuleKey invalid_module{"route/mark", "one"};
  CHECK_THROWS_AS(invalid_module.comment(), std::invalid_argument);
  const FirewallRuleKey empty_instance{"route.mark", ""};
  CHECK_THROWS_AS(empty_instance.comment(), std::invalid_argument);
  CHECK_THROWS_AS(FirewallRuleKey::from_comment("kpbr:v1::one"),
                  std::invalid_argument);
  const FirewallRuleKey oversized_module{
      std::string(kFirewallRuleIdMaxLength + 1U, 'm'), "one"};
  CHECK_THROWS_AS(oversized_module.comment(), std::invalid_argument);
  CHECK_THROWS_AS(FirewallRuleKey::from_comment(
                      "kpbr:v1:" +
                      std::string(kFirewallRuleIdMaxLength + 1U, 'm') +
                      ":one"),
                  std::invalid_argument);
  CHECK_THROWS_AS(FirewallRuleKey::from_comment(
                      "kpbr:v1:route.mark:" +
                      std::string(kFirewallRuleCommentMaxLength, 'i')),
                  std::invalid_argument);
}

TEST_CASE("FirewallRuleKey compact factory produces reversible stable digest") {
  const auto key = FirewallRuleKey::compact(
      std::string(kFirewallRuleIdMaxLength, 'm'),
      std::string(kFirewallRuleIdMaxLength, 'i'));
  const std::string expected_digest =
      "a1586b466913c56a933f84642d70bc99";
  const auto comment = key.comment();
  CHECK(comment == "kpbr:v1:" + std::string(kFirewallRuleIdMaxLength, 'm') +
                     ":" + expected_digest);
  CHECK(comment.size() <= kFirewallRuleCommentMaxLength);
  CHECK(FirewallRuleKey::from_comment(comment) == key);
}

TEST_CASE("FirewallRuleKey comment rejects an oversized uncompact key") {
  const FirewallRuleKey key{std::string(kFirewallRuleIdMaxLength, 'm'),
                            std::string(kFirewallRuleIdMaxLength, 'i')};
  CHECK_THROWS_AS(key.comment(), std::length_error);
  CHECK_THROWS_AS(FirewallRuleKey::compact("route.mark", ""),
                  std::invalid_argument);
}

TEST_CASE("FirewallRuleInstance carries canonical action and placement values") {
  FirewallRuleInstance instance;
  instance.key = {"route.mark", "vpn"};
  instance.stage = FirewallRuleStage::route_classification;
  instance.hook = FirewallHook::output;
  instance.family = FirewallFamily::ipv6;
  instance.action = MarkAction{0x100U, 0xFF00U};

  CHECK(instance.key.comment() == "kpbr:v1:route.mark:vpn");
  CHECK(std::get<MarkAction>(instance.action) == MarkAction{0x100U, 0xFF00U});
  CHECK(instance.criteria.proto == L4Proto::Any);
}

} // namespace keen_pbr3
