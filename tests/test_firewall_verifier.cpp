// Policy-agnostic verifier: the kernel state is a REAL dump (fixtures under
// tests/firewall_it/fixtures/physical/), read through an injected
// CommandRunner exactly as the health check reads it, and corrupted by editing
// the fixture text.  The expected side comes from the backend
// (Firewall::expected_ruleset), never from the fixture.

#include <doctest/doctest.h>

#include "../src/firewall/firewall_lowering.hpp"
#include "../src/firewall/firewall_plan_verifier.hpp"
#include "../src/firewall/firewall_snapshot.hpp"
#include "../src/firewall/iptables.hpp"
#include "firewall_fixtures.hpp"

#include <algorithm>
#include <map>
#include <nlohmann/json.hpp>
#include <sstream>

namespace keen_pbr3 {
namespace {

using Json = nlohmann::json;

// ---------------------------------------------------------------------------
// Result helpers
// ---------------------------------------------------------------------------

std::string describe(const std::vector<FirewallRuleCheck>& checks) {
  std::ostringstream out;
  for (const auto& check : checks) {
    if (check.status == CheckStatus::ok) continue;
    out << "\n  [" << check.action << " " << check.set_name << "] "
        << (check.status == CheckStatus::missing ? "missing" : "mismatch")
        << ": " << check.detail;
  }
  return out.str();
}

std::size_t problems(const std::vector<FirewallRuleCheck>& checks) {
  return static_cast<std::size_t>(
      std::count_if(checks.begin(), checks.end(), [](const auto& check) {
        return check.status != CheckStatus::ok;
      }));
}

// Checks that are not attributed to a plan rule are appended after them.
std::size_t extras_of(const FirewallPlan& plan,
                      const std::vector<FirewallRuleCheck>& checks) {
  return checks.size() > plan.rules.size() ? checks.size() - plan.rules.size()
                                           : 0U;
}

bool any_detail(const std::vector<FirewallRuleCheck>& checks,
                const std::string& needle,
                CheckStatus status = CheckStatus::mismatch) {
  return std::any_of(checks.begin(), checks.end(), [&](const auto& check) {
    return check.status == status &&
           check.detail.find(needle) != std::string::npos;
  });
}

std::size_t plan_index(const FirewallPlan& plan, const std::string& module,
                       std::size_t nth = 0) {
  for (std::size_t i = 0; i < plan.rules.size(); ++i) {
    if (plan.rules[i].key.module_id == module && nth-- == 0) return i;
  }
  FAIL("plan has no rule of module " << module);
  return 0;
}

// ---------------------------------------------------------------------------
// iptables
// ---------------------------------------------------------------------------

struct IptTexts {
  std::string v4_mangle, v6_mangle, v4_raw, v6_raw;
};

std::string table_section(const std::string& save, const std::string& table) {
  const auto start = save.find("*" + table + "\n");
  REQUIRE(start != std::string::npos);
  const auto end = save.find("COMMIT\n", start);
  REQUIRE(end != std::string::npos);
  return save.substr(start, end + 7 - start);
}

IptTexts mangle_texts() {
  return {read_fixture("iptables_mangle_v4.rules"),
          read_fixture("iptables_mangle_v6.save"), {}, {}};
}

IptTexts raw_texts() {
  const auto v4 = read_fixture("iptables_raw_v4.save");
  const auto v6 = read_fixture("iptables_raw_v6.save");
  return {table_section(v4, "mangle"), table_section(v6, "mangle"),
          table_section(v4, "raw"), table_section(v6, "raw")};
}

const RawPreroutingMode kRawBoth{true, true};

PhysicalRuleset iptables_expected(const FirewallPlan& plan,
                                  RawPreroutingMode raw,
                                  bool comments = true) {
  // The capability probes need a real kernel; the fixtures stand in for them.
  IptablesFirewall firewall;
  firewall.override_capabilities_for_fixtures(comments, raw, true);
  firewall.set_ipv6_enabled(true);
  firewall.set_fwmark_mask(plan.fwmark_mask);
  return firewall.expected_ruleset(plan);
}

CommandRunner iptables_runner(const IptTexts& texts,
                              std::vector<std::vector<std::string>>* calls =
                                  nullptr) {
  return [texts, calls](const std::vector<std::string>& args) {
    if (calls != nullptr) calls->push_back(args);
    REQUIRE(args.size() == 4);
    REQUIRE(args[1] == "-t");
    REQUIRE(args[3] == "-S");
    const bool v6 = args[0] == "ip6tables";
    const bool raw = args[2] == "raw";
    return CommandResult{raw ? (v6 ? texts.v6_raw : texts.v4_raw)
                             : (v6 ? texts.v6_mangle : texts.v4_mangle),
                         0, false};
  };
}

FirewallSnapshot iptables_snapshot(const IptTexts& texts,
                                   RawPreroutingMode raw) {
  return inspect_iptables_snapshot(iptables_runner(texts), raw, true);
}

std::vector<FirewallRuleCheck> verify_iptables(const FirewallPlan& plan,
                                               const PhysicalRuleset& expected,
                                               const IptTexts& texts,
                                               RawPreroutingMode raw) {
  const auto snapshot = iptables_snapshot(texts, raw);
  REQUIRE_MESSAGE(snapshot.available, snapshot.error);
  return verify_firewall_plan(plan, expected, snapshot);
}

std::string replace_first(std::string text, const std::string& from,
                          const std::string& to) {
  const auto position = text.find(from);
  REQUIRE_MESSAGE(position != std::string::npos, "no '" << from << "'");
  text.replace(position, from.size(), to);
  return text;
}

std::vector<std::string> split_lines(const std::string& text) {
  std::vector<std::string> lines;
  std::size_t start = 0;
  while (start < text.size()) {
    const auto end = text.find('\n', start);
    lines.push_back(text.substr(start, end - start));
    start = end == std::string::npos ? text.size() : end + 1;
  }
  return lines;
}

std::string join_lines(const std::vector<std::string>& lines) {
  std::string text;
  for (const auto& line : lines) text += line + "\n";
  return text;
}

// Index of the nth line containing `needle`.
std::size_t line_with(const std::vector<std::string>& lines,
                      const std::string& needle, std::size_t nth = 0) {
  for (std::size_t i = 0; i < lines.size(); ++i) {
    if (lines[i].find(needle) != std::string::npos && nth-- == 0) return i;
  }
  FAIL("no line with " << needle);
  return 0;
}

std::string without_line(const std::string& text, const std::string& needle,
                         std::size_t nth = 0) {
  auto lines = split_lines(text);
  lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(
                                  line_with(lines, needle, nth)));
  return join_lines(lines);
}

std::string with_line_after(const std::string& text, const std::string& needle,
                            const std::string& added, std::size_t nth = 0) {
  auto lines = split_lines(text);
  lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(
                                   line_with(lines, needle, nth) + 1),
               added);
  return join_lines(lines);
}

std::string with_duplicated_line(const std::string& text,
                                 const std::string& needle) {
  auto lines = split_lines(text);
  const auto at = line_with(lines, needle);
  lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(at), lines[at]);
  return join_lines(lines);
}

std::string with_swapped_lines(const std::string& text, const std::string& a,
                               const std::string& b) {
  auto lines = split_lines(text);
  std::swap(lines[line_with(lines, a)], lines[line_with(lines, b)]);
  return join_lines(lines);
}

// Drops every ` -m comment --comment "..."` ownership match.
std::string without_comments(std::string text) {
  const std::string open = " -m comment --comment \"";
  for (auto at = text.find(open); at != std::string::npos;
       at = text.find(open)) {
    const auto close = text.find('"', at + open.size());
    text.erase(at, close + 1 - at);
  }
  return text;
}

IptTexts map_texts(const IptTexts& texts,
                   const std::function<std::string(const std::string&)>& fn,
                   bool v4_mangle = true, bool v6_mangle = false,
                   bool v4_raw = false) {
  IptTexts result = texts;
  if (v4_mangle) result.v4_mangle = fn(texts.v4_mangle);
  if (v6_mangle) result.v6_mangle = fn(texts.v6_mangle);
  if (v4_raw) result.v4_raw = fn(texts.v4_raw);
  return result;
}

} // namespace

TEST_CASE("verifier iptables mangle: the real kernel dump is entirely ok") {
  const auto plan = capture_plan(false, false, true);
  const auto expected = iptables_expected(plan, {});
  const auto checks = verify_iptables(plan, expected, mangle_texts(), {});
  CHECK_MESSAGE(problems(checks) == 0, describe(checks));
  CHECK(checks.size() == plan.rules.size());
  for (const auto& check : checks) CHECK(check.detail == "ok");
  // Mark rules report the mark they installed.
  const auto mark = plan_index(plan, "route.mark");
  CHECK(checks[mark].action == "mark");
  CHECK(checks[mark].expected_fwmark == 0x10000U);
  CHECK(checks[mark].actual_fwmark == 0x10000U);
  CHECK(checks[mark].set_name == "kpbr4_hybrid");
}

TEST_CASE("verifier iptables raw layout: the real kernel dump is entirely ok") {
  const auto plan = capture_plan(false, true, true);
  const auto expected = iptables_expected(plan, kRawBoth);
  const auto checks = verify_iptables(plan, expected, raw_texts(), kRawBoth);
  CHECK_MESSAGE(problems(checks) == 0, describe(checks));
  CHECK(extras_of(plan, checks) == 0);
}

TEST_CASE("verifier iptables: fixtures are verified without ownership comments") {
  const auto plan = capture_plan(false, false, true);
  const auto expected = iptables_expected(plan, {});
  const auto stripped = map_texts(mangle_texts(), without_comments, true, true);
  CHECK(stripped.v4_mangle.find("comment") == std::string::npos);
  const auto checks = verify_iptables(plan, expected, stripped, {});
  CHECK_MESSAGE(problems(checks) == 0, describe(checks));

  // The same when the backend itself could not emit comments.
  const auto no_comment_expected =
      iptables_expected(plan, {}, /*comments=*/false);
  CHECK(problems(verify_iptables(plan, no_comment_expected, stripped, {})) == 0);
  CHECK(problems(verify_iptables(plan, no_comment_expected, mangle_texts(),
                                 {})) == 0);
}

TEST_CASE("verifier iptables: classifier corruptions are attributed to their plan rule") {
  const auto plan = capture_plan(false, false, true);
  const auto expected = iptables_expected(plan, {});
  const auto first_mark = plan_index(plan, "route.mark");
  const auto pass = plan_index(plan, "route.pass");

  struct Corruption {
    const char* name;
    std::function<std::string(const std::string&)> edit;
    std::size_t plan_rule;
    CheckStatus status;
    const char* detail;
  };
  const std::vector<Corruption> corruptions = {
      {"wrong mark value",
       [](const std::string& t) {
         return replace_first(t, "--set-xmark 0x10000/0xff0000",
                              "--set-xmark 0x30000/0xff0000");
       },
       first_mark, CheckStatus::mismatch, "0x30000"},
      {"wrong mask",
       [](const std::string& t) {
         return replace_first(t, "--set-xmark 0x10000/0xff0000",
                              "--set-xmark 0x10000/0xffff0000");
       },
       first_mark, CheckStatus::mismatch, "0xffff0000"},
      {"wrong set name",
       [](const std::string& t) {
         return replace_first(t, "kpbr4s_hybrid", "kpbr4S_hybrid");
       },
       first_mark, CheckStatus::mismatch, "kpbr4S_hybrid"},
      {"missing rule",
       [](const std::string& t) { return without_line(t, "-d 8.8.8.8/32"); },
       pass, CheckStatus::missing, "rule missing"},
      {"duplicated rule",
       [](const std::string& t) {
         return with_duplicated_line(t, "-d 8.8.8.8/32");
       },
       pass, CheckStatus::mismatch, "duplicate rule"},
      {"swapped rules",
       [](const std::string& t) {
         return with_swapped_lines(t, "-d 8.8.8.8/32", "-d 9.9.9.9/32");
       },
       pass, CheckStatus::mismatch, "rule order differs"},
  };

  for (const bool with_comments : {true, false}) {
    for (const auto& corruption : corruptions) {
      CAPTURE(corruption.name);
      CAPTURE(with_comments);
      auto texts = map_texts(mangle_texts(), corruption.edit);
      if (!with_comments) texts = map_texts(texts, without_comments, true, true);
      const auto checks = verify_iptables(plan, expected, texts, {});
      // Exactly the owning plan rule is flagged, nothing else.
      CHECK_MESSAGE(problems(checks) == 1, describe(checks));
      REQUIRE(corruption.plan_rule < checks.size());
      CHECK(checks[corruption.plan_rule].status == corruption.status);
      CHECK_MESSAGE(checks[corruption.plan_rule].detail.find(corruption.detail) !=
                        std::string::npos,
                    checks[corruption.plan_rule].detail);
    }
  }

  // A wrong mark reports the mark that was observed.
  const auto wrong = verify_iptables(
      plan, expected,
      map_texts(mangle_texts(),
                [](const std::string& t) {
                  return replace_first(t, "--set-xmark 0x10000/0xff0000",
                                       "--set-xmark 0x30000/0xff0000");
                }),
      {});
  CHECK(wrong[first_mark].expected_fwmark == 0x10000U);
  CHECK(wrong[first_mark].actual_fwmark == 0x30000U);
}

TEST_CASE("verifier iptables: extra rules in an owned chain are unexpected") {
  const auto plan = capture_plan(false, false, true);
  const auto expected = iptables_expected(plan, {});

  SUBCASE("a foreign rule inserted between owned rules") {
    const auto texts = map_texts(mangle_texts(), [](const std::string& t) {
      return with_line_after(t, "-d 8.8.8.8/32", "-A KeenPbrTable_A -j LOG");
    });
    const auto checks = verify_iptables(plan, expected, texts, {});
    CHECK(problems(checks) == 1);
    CHECK(extras_of(plan, checks) == 1);
    CHECK_MESSAGE(any_detail(checks, "unexpected rule in mangle/KeenPbrTable_A at index"),
                  describe(checks));
  }
  SUBCASE("the real kernel dump with foreign rules appended and inserted") {
    auto texts = mangle_texts();
    texts.v4_mangle = read_fixture("iptables_mangle_v4_foreign.rules");
    const auto checks = verify_iptables(plan, expected, texts, {});
    // limit (inserted first), -s 203.0.113.9 and -o eth9 LOG; plus the extra
    // PREROUTING jump straight into the generation chain.  Rules of the plan
    // itself are all in place.
    CHECK_MESSAGE(extras_of(plan, checks) == 4, describe(checks));
    CHECK(problems(checks) == 4);
    CHECK(any_detail(checks, "unexpected rule in mangle/KeenPbrTable_A at index 0"));
    CHECK(any_detail(checks, "unexpected rule in mangle/PREROUTING at index 1"));
    for (std::size_t i = 0; i < plan.rules.size(); ++i) {
      CHECK(checks[i].status == CheckStatus::ok);
    }
  }
}

TEST_CASE("verifier iptables: hook and dispatcher jumps") {
  const auto plan = capture_plan(false, false, true);
  const auto expected = iptables_expected(plan, {});

  SUBCASE("missing PREROUTING jump") {
    const auto checks = verify_iptables(
        plan, expected,
        map_texts(mangle_texts(),
                  [](const std::string& t) {
                    return without_line(t, "-A PREROUTING -j KeenPbrTable");
                  }),
        {});
    CHECK(problems(checks) == 1);
    const auto& extra = checks.back();
    CHECK(extra.status == CheckStatus::missing);
    CHECK(extra.action == "hook");
    CHECK_MESSAGE(extra.detail.find("rule missing in mangle/PREROUTING") !=
                      std::string::npos,
                  extra.detail);
  }
  SUBCASE("duplicate PREROUTING jump") {
    const auto checks = verify_iptables(
        plan, expected,
        map_texts(mangle_texts(),
                  [](const std::string& t) {
                    return with_duplicated_line(t,
                                                "-A PREROUTING -j KeenPbrTable");
                  }),
        {});
    CHECK(problems(checks) == 1);
    CHECK_MESSAGE(any_detail(checks, "duplicate rule in mangle/PREROUTING"),
                  describe(checks));
  }
  SUBCASE("duplicate OUTPUT jump") {
    const auto checks = verify_iptables(
        plan, expected,
        map_texts(mangle_texts(),
                  [](const std::string& t) {
                    return with_duplicated_line(
                        t, "-A OUTPUT -j KeenPbrTable_OUTPUT");
                  }),
        {});
    CHECK(problems(checks) == 1);
    CHECK(any_detail(checks, "mangle/OUTPUT"));
  }
  SUBCASE("dispatcher jumping to the wrong generation") {
    const auto checks = verify_iptables(
        plan, expected,
        map_texts(mangle_texts(),
                  [](const std::string& t) {
                    return replace_first(t, "-A KeenPbrTable -j KeenPbrTable_A",
                                         "-A KeenPbrTable -j KeenPbrTable_B");
                  }),
        {});
    CHECK(problems(checks) == 1);
    CHECK_MESSAGE(any_detail(checks, "expected ipv4 -> jump KeenPbrTable_A but "
                                     "observed ipv4 -> jump KeenPbrTable_B"),
                  describe(checks));
  }
  SUBCASE("the IPv6 tables are checked too") {
    auto texts = mangle_texts();
    texts.v6_mangle = replace_first(texts.v6_mangle,
                                    "-A PREROUTING -j KeenPbrTable\n", "");
    const auto checks = verify_iptables(plan, expected, texts, {});
    CHECK(problems(checks) == 1);
    CHECK(any_detail(checks, "mangle/PREROUTING (ipv6)", CheckStatus::missing));
  }
  SUBCASE("a jump from another builtin chain into keen-pbr is unexpected") {
    const auto checks = verify_iptables(
        plan, expected,
        map_texts(mangle_texts(),
                  [](const std::string& t) {
                    return with_line_after(t, "-A OUTPUT -j KeenPbrTable_OUTPUT",
                                           "-A INPUT -j KeenPbrTable_A");
                  }),
        {});
    CHECK(problems(checks) == 1);
    CHECK(any_detail(checks, "unexpected rule in mangle/INPUT"));
  }
  SUBCASE("raw layout: wrong raw dispatcher target") {
    const auto raw_plan = capture_plan(false, true, true);
    const auto raw_expected = iptables_expected(raw_plan, kRawBoth);
    auto texts = raw_texts();
    texts.v4_raw = replace_first(texts.v4_raw, "-A KeenPbrRaw -j KeenPbrRaw_A",
                                 "-A KeenPbrRaw -j KeenPbrRaw_B");
    const auto checks = verify_iptables(raw_plan, raw_expected, texts, kRawBoth);
    CHECK(problems(checks) == 1);
    CHECK(any_detail(checks, "KeenPbrRaw_B"));
  }
}

TEST_CASE("verifier iptables: foreign state is ignored, stale keen-pbr state is not") {
  const auto plan = capture_plan(false, false, true);
  const auto expected = iptables_expected(plan, {});
  const auto verify = [&](const std::function<std::string(const std::string&)>& edit) {
    return verify_iptables(plan, expected, map_texts(mangle_texts(), edit), {});
  };

  SUBCASE("foreign rules in system chains and foreign chains") {
    const auto checks = verify([](const std::string& t) {
      auto text = with_line_after(t, "-A PREROUTING -j KeenPbrTable",
                                  "-A PREROUTING -i eth0 -j ACCEPT");
      text = with_line_after(text, "-A OUTPUT -j KeenPbrTable_OUTPUT",
                             "-A FORWARD -j ACCEPT");
      text = replace_first(text, "-N KeenPbrTable\n",
                           "-N KeenPbrTable\n-N SomeoneElse\n");
      return with_line_after(text, "-A FORWARD -j ACCEPT",
                             "-A SomeoneElse -j DROP");
    });
    CHECK_MESSAGE(problems(checks) == 0, describe(checks));
  }
  SUBCASE("the inactive generation may be absent, empty or stale") {
    // iptables.cpp: apply rewrites only the target generation chain and
    // leaves the previously active one as it was (stale after a rebuild,
    // absent after a destructive apply).
    CHECK(problems(verify([](const std::string& t) { return t; })) == 0);
    CHECK(problems(verify([](const std::string& t) {
            return replace_first(t, "-N KeenPbrTable_A\n",
                                 "-N KeenPbrTable_A\n-N KeenPbrTable_B\n");
          })) == 0);
    const auto stale = verify([](const std::string& t) {
      auto text = replace_first(t, "-N KeenPbrTable_A\n",
                                "-N KeenPbrTable_A\n-N KeenPbrTable_B\n");
      return with_line_after(text, "-A KeenPbrTable -j KeenPbrTable_A",
                             "-A KeenPbrTable_B -m set --match-set old dst -j "
                             "MARK --set-xmark 0x70000/0xff0000");
    });
    CHECK_MESSAGE(problems(stale) == 0, describe(stale));
  }
  SUBCASE("the active generation chain missing flags every rule it holds") {
    const auto checks = verify([](const std::string& t) {
      std::string text;
      for (const auto& line : split_lines(t)) {
        if (line.rfind("-A KeenPbrTable_A ", 0) == 0 || line == "-N KeenPbrTable_A") {
          continue;
        }
        text += line + "\n";
      }
      return text;
    });
    CHECK(any_detail(checks, "chain mangle/KeenPbrTable_A is missing",
                     CheckStatus::missing));
    CHECK(checks[plan_index(plan, "route.pass")].status ==
          CheckStatus::missing);
    CHECK(checks[plan_index(plan, "route.drop")].status ==
          CheckStatus::missing);
    // IPv6-only rules live in another chain and stay ok.
    CHECK(checks[plan_index(plan, "route.mark", 1)].status == CheckStatus::ok);
  }
  SUBCASE("an unreferenced leftover chain is harmless, a reachable one is not") {
    // Apply never deletes it (see the integration case
    // iptables_ab_convergence), so reporting it would never clear.
    CHECK(problems(verify([](const std::string& t) {
            return replace_first(t, "-N KeenPbrTable\n",
                                 "-N KeenPbrTable\n-N KeenPbrTable_Unknown\n"
                                 "-N KeenPbrOutput_A\n");
          })) == 0);
    const auto reachable = verify([](const std::string& t) {
      auto text = replace_first(t, "-N KeenPbrTable\n",
                                "-N KeenPbrTable\n-N KeenPbrTable_Unknown\n");
      return with_line_after(text, "-A KeenPbrTable -j KeenPbrTable_A",
                             "-A KeenPbrTable -j KeenPbrTable_Unknown");
    });
    CHECK(problems(reachable) == 1);
    CHECK(any_detail(reachable, "unexpected rule in mangle/KeenPbrTable at"));
  }
}

TEST_CASE("verifier iptables: an unreadable kernel reports everything missing") {
  const auto plan = capture_plan(false, false, true);
  const auto expected = iptables_expected(plan, {});
  const auto fails = [](CommandResult result) {
    return [result](const std::vector<std::string>&) { return result; };
  };
  for (const auto& result : {CommandResult{"", 1, false},
                             CommandResult{"truncated", 0, true}}) {
    const auto snapshot = inspect_iptables_snapshot(fails(result), {}, true);
    CHECK_FALSE(snapshot.available);
    CHECK_FALSE(snapshot.error.empty());
    const auto checks = verify_firewall_plan(plan, expected, snapshot);
    REQUIRE(checks.size() == plan.rules.size());
    for (const auto& check : checks) {
      CHECK(check.status == CheckStatus::missing);
      CHECK(check.detail == snapshot.error);
    }
  }
}

// ---------------------------------------------------------------------------
// nftables
// ---------------------------------------------------------------------------

namespace {

PhysicalRuleset nft_expected(const FirewallPlan& plan) {
  FirewallLoweringContext context;
  context.backend = FirewallBackend::nftables;
  context.fwmark_mask = plan.fwmark_mask;
  return lower_firewall_plan(plan, context);
}

CommandRunner nft_runner(const Json& document,
                         std::vector<std::vector<std::string>>* calls =
                             nullptr) {
  const std::string text = document.dump();
  return [text, calls](const std::vector<std::string>& args) {
    if (calls != nullptr) calls->push_back(args);
    return CommandResult{text, 0, false};
  };
}

std::vector<FirewallRuleCheck> verify_nft(const FirewallPlan& plan,
                                          const PhysicalRuleset& expected,
                                          const Json& document) {
  const auto snapshot = inspect_nftables_snapshot(nft_runner(document));
  REQUIRE_MESSAGE(snapshot.available, snapshot.error);
  return verify_firewall_plan(plan, expected, snapshot);
}

Json& items(Json& document) { return document["nftables"]; }

// Position of the rule with this ownership comment inside the item array
// (nth match: a plan rule can expand to several physical rules).
std::size_t nft_rule_at(Json& document, const FirewallRuleKey& key,
                        std::size_t nth = 0) {
  auto& all = items(document);
  for (std::size_t i = 0; i < all.size(); ++i) {
    const auto& item = all[i];
    if (item.contains("rule") &&
        item["rule"].value("comment", "") == key.comment() && nth-- == 0) {
      return i;
    }
  }
  FAIL("no nft rule for " << key.comment());
  return 0;
}

std::size_t nft_chain_at(Json& document, const std::string& name) {
  auto& all = items(document);
  for (std::size_t i = 0; i < all.size(); ++i) {
    if (all[i].contains("chain") && all[i]["chain"]["name"] == name) return i;
  }
  FAIL("no nft chain " << name);
  return 0;
}

std::size_t nft_setter_rule_at(Json& document, const std::string& chain) {
  auto& all = items(document);
  for (std::size_t i = 0; i < all.size(); ++i) {
    if (all[i].contains("rule") && all[i]["rule"]["chain"] == chain) return i;
  }
  FAIL("no rule in " << chain);
  return 0;
}

void strip_nft_comments(Json& document) {
  for (auto& item : items(document)) {
    if (item.contains("rule")) item["rule"].erase("comment");
  }
}

Json nft_balance_document() {
  return Json::parse(read_fixture("nft_balance.json"));
}

} // namespace

TEST_CASE("verifier nftables: the real kernel dump is entirely ok") {
  const auto plan = capture_plan(true, true, true);
  const auto expected = nft_expected(plan);
  const auto document = nft_balance_document();
  auto checks = verify_nft(plan, expected, document);
  CHECK_MESSAGE(problems(checks) == 0, describe(checks));
  CHECK(checks.size() == plan.rules.size());

  SUBCASE("also without ownership comments") {
    auto stripped = document;
    strip_nft_comments(stripped);
    checks = verify_nft(plan, expected, stripped);
    CHECK_MESSAGE(problems(checks) == 0, describe(checks));
  }
}

TEST_CASE("verifier nftables: corruptions are attributed to their plan rule") {
  const auto plan = capture_plan(true, true, true);
  const auto expected = nft_expected(plan);
  const auto& first_mark = plan.rules[plan_index(plan, "route.mark")];
  const auto& drop = plan.rules[plan_index(plan, "route.drop")];
  const auto& pass = plan.rules[plan_index(plan, "route.pass")];
  const auto& balance_v4 = plan.rules[plan_index(plan, "route.balance", 0)];
  const auto index_of = [&](const FirewallRuleKey& key) {
    for (std::size_t i = 0; i < plan.rules.size(); ++i) {
      if (plan.rules[i].key == key) return i;
    }
    FAIL("unknown key");
    return std::size_t{0};
  };

  // Each corruption is applied to the real dump, located by comment, and then
  // verified with and without ownership comments.
  using Check = std::function<void(const std::vector<FirewallRuleCheck>&)>;
  const auto run = [&](const char* name,
                       const std::function<void(Json&)>& edit,
                       const Check& check) {
    for (const bool with_comments : {true, false}) {
      CAPTURE(name);
      CAPTURE(with_comments);
      auto document = nft_balance_document();
      edit(document);
      if (!with_comments) strip_nft_comments(document);
      check(verify_nft(plan, expected, document));
    }
  };
  const auto only = [&](const FirewallRuleKey& key, CheckStatus status,
                        const std::string& detail) -> Check {
    return [&, key, status, detail](
               const std::vector<FirewallRuleCheck>& checks) {
      CHECK_MESSAGE(problems(checks) == 1, describe(checks));
      const auto& check = checks[index_of(key)];
      CHECK(check.status == status);
      CHECK_MESSAGE(check.detail.find(detail) != std::string::npos,
                    check.detail);
    };
  };
  const auto rule_expr = [](Json& document, const FirewallRuleKey& key) -> Json& {
    return items(document)[nft_rule_at(document, key)]["rule"]["expr"];
  };

  run("rule installs another mark (jump to another setter)",
      [&](Json& document) {
        auto& expr = rule_expr(document, first_mark.key);
        expr[expr.size() - 1]["jump"]["target"] = "setmark_00040000";
      },
      [&](const std::vector<FirewallRuleCheck>& checks) {
        only(first_mark.key, CheckStatus::mismatch, "setmark_00040000")(checks);
        const auto& check = checks[index_of(first_mark.key)];
        CHECK(check.actual_fwmark == 0x40000U);
        CHECK(check.expected_fwmark == 0x10000U);
      });
  run("wrong set name",
      [&](Json& document) {
        rule_expr(document, drop.key)[0]["match"]["right"] = "@kpbr4_other";
      },
      only(drop.key, CheckStatus::mismatch, "kpbr4_other"));
  run("missing rule",
      [&](Json& document) {
        const auto at = nft_rule_at(document, pass.key);
        items(document).erase(items(document).begin() +
                              static_cast<std::ptrdiff_t>(at));
      },
      only(pass.key, CheckStatus::missing, "rule missing"));
  run("duplicated rule",
      [&](Json& document) {
        const auto at = nft_rule_at(document, pass.key);
        const Json copy = items(document)[at];
        items(document).insert(
            items(document).begin() + static_cast<std::ptrdiff_t>(at), copy);
      },
      only(pass.key, CheckStatus::mismatch, "duplicate rule"));
  run("two rules swapped",
      [&](Json& document) {
        const auto a = nft_rule_at(document, drop.key);
        const auto b = nft_rule_at(document, pass.key);
        std::swap(items(document)[a]["rule"]["expr"],
                  items(document)[b]["rule"]["expr"]);
        std::swap(items(document)[a]["rule"]["comment"],
                  items(document)[b]["rule"]["comment"]);
      },
      [&](const std::vector<FirewallRuleCheck>& checks) {
        // Either order can be reported against the swapped pair.
        CHECK_MESSAGE(problems(checks) >= 1, describe(checks));
        const bool reported = any_detail(checks, "rule order differs") ||
                              any_detail(checks, "rule mismatch");
        CHECK(reported);
      });
  run("balance: wrong numgen modulus",
      [&](Json& document) {
        for (auto& statement : rule_expr(document, balance_v4.key)) {
          if (statement.contains("vmap")) {
            statement["vmap"]["key"]["numgen"]["mod"] = 3;
          }
        }
      },
      only(balance_v4.key, CheckStatus::mismatch, "vmap numgen mod 0x3"));
  run("balance: vmap entry missing",
      [&](Json& document) {
        for (auto& statement : rule_expr(document, balance_v4.key)) {
          if (statement.contains("vmap")) {
            statement["vmap"]["data"]["set"].erase(1);
          }
        }
      },
      only(balance_v4.key, CheckStatus::mismatch, "vmap"));
  run("balance: vmap entries reordered",
      [&](Json& document) {
        for (auto& statement : rule_expr(document, balance_v4.key)) {
          if (statement.contains("vmap")) {
            auto& entries = statement["vmap"]["data"]["set"];
            std::swap(entries[0][1], entries[1][1]);
          }
        }
      },
      only(balance_v4.key, CheckStatus::mismatch, "vmap"));
}

TEST_CASE("verifier nftables: setter chains") {
  const auto plan = capture_plan(true, true, true);
  const auto expected = nft_expected(plan);

  SUBCASE("setter chain installs a wrong mark") {
    auto document = nft_balance_document();
    auto& expr = items(document)[nft_setter_rule_at(document,
                                                     "setmark_00010000")]
                     ["rule"]["expr"];
    expr[0]["mangle"]["value"]["|"][1] = 0x70000;
    const auto checks = verify_nft(plan, expected, document);
    CHECK(problems(checks) == 1);
    CHECK(checks.back().action == "setter");
    CHECK(checks.back().status == CheckStatus::mismatch);
    CHECK(checks.back().set_name == "setmark_00010000");
    CHECK_MESSAGE(checks.back().detail.find("set-mark 0x70000") !=
                      std::string::npos,
                  checks.back().detail);
  }
  SUBCASE("setter chain installs a wrong mask") {
    auto document = nft_balance_document();
    auto& expr = items(document)[nft_setter_rule_at(document,
                                                     "setmark_00010000")]
                     ["rule"]["expr"];
    expr[0]["mangle"]["value"]["|"][0]["&"][1] = 4278190335U;
    const auto checks = verify_nft(plan, expected, document);
    CHECK(problems(checks) == 1);
    CHECK(checks.back().action == "setter");
  }
  SUBCASE("setter chain missing") {
    auto document = nft_balance_document();
    auto& all = items(document);
    const auto chain = nft_chain_at(document, "setmark_00020000");
    for (std::size_t i = all.size(); i-- > 0;) {
      const bool is_chain =
          all[i].contains("chain") && all[i]["chain"]["name"] == "setmark_00020000";
      const bool is_rule = all[i].contains("rule") &&
                           all[i]["rule"]["chain"] == "setmark_00020000";
      if (is_chain || is_rule) all.erase(all.begin() + static_cast<std::ptrdiff_t>(i));
    }
    (void)chain;
    const auto checks = verify_nft(plan, expected, document);
    CHECK(problems(checks) == 1);
    CHECK(checks.back().status == CheckStatus::missing);
    CHECK(checks.back().action == "setter");
    CHECK(checks.back().expected_fwmark == 0x20000U);
  }
  SUBCASE("stale setter chain present") {
    // Apply deletes every live setter chain before recreating the needed
    // ones (nftables.cpp build_apply_document), so a surplus one is stale.
    auto document = nft_balance_document();
    auto& all = items(document);
    all.push_back({{"chain", {{"family", "inet"}, {"table", "KeenPbrTable"},
                              {"name", "setmark_00090000"}}}});
    const auto checks = verify_nft(plan, expected, document);
    CHECK(problems(checks) == 1);
    CHECK(checks.back().action == "setter");
    CHECK(checks.back().actual_fwmark == 0x90000U);
    CHECK(checks.back().detail.find("unexpected keen-pbr chain") !=
          std::string::npos);
  }
}

TEST_CASE("verifier nftables: base chain attributes and foreign rules") {
  const auto plan = capture_plan(true, true, true);
  const auto expected = nft_expected(plan);

  SUBCASE("priority and hook are compared") {
    auto document = nft_balance_document();
    items(document)[nft_chain_at(document, "prerouting")]["chain"]["prio"] = -100;
    auto checks = verify_nft(plan, expected, document);
    CHECK(problems(checks) == 1);
    CHECK(any_detail(checks, "base chain attributes of prerouting differ"));

    document = nft_balance_document();
    auto& chain = items(document)[nft_chain_at(document, "output")]["chain"];
    chain.erase("hook");
    chain.erase("prio");
    chain.erase("type");
    chain.erase("policy");
    checks = verify_nft(plan, expected, document);
    CHECK(problems(checks) == 1);
    CHECK(any_detail(checks, "base chain attributes of output differ"));
  }
  SUBCASE("rules appended to the real dump with the stock tool are unexpected") {
    const auto document = Json::parse(read_fixture("nft_foreign.json"));
    const auto checks = verify_nft(plan, expected, document);
    CHECK_MESSAGE(extras_of(plan, checks) == 3, describe(checks));
    CHECK(problems(checks) == 3);
    CHECK(any_detail(checks, "unexpected rule in prerouting at index"));
    for (std::size_t i = 0; i < plan.rules.size(); ++i) {
      CHECK(checks[i].status == CheckStatus::ok);
    }
  }
  SUBCASE("a chain of another table never reaches the ruleset") {
    auto document = nft_balance_document();
    items(document).push_back(
        {{"chain", {{"family", "inet"}, {"table", "other"}, {"name", "x"}}}});
    CHECK(problems(verify_nft(plan, expected, document)) == 0);
  }
}

TEST_CASE("verifier nftables: an absent table or failed read reports everything missing") {
  const auto plan = capture_plan(true, true, true);
  const auto expected = nft_expected(plan);
  const auto expect_all_missing = [&](const FirewallSnapshot& snapshot) {
    CHECK_FALSE(snapshot.available);
    const auto checks = verify_firewall_plan(plan, expected, snapshot);
    REQUIRE(checks.size() == plan.rules.size());
    for (const auto& check : checks) {
      CHECK(check.status == CheckStatus::missing);
      CHECK(check.detail == snapshot.error);
    }
  };
  expect_all_missing(inspect_nftables_snapshot(
      [](const std::vector<std::string>&) { return CommandResult{"", 1, false}; }));
  expect_all_missing(inspect_nftables_snapshot(
      nft_runner(Json{{"nftables", Json::array()}})));
}

} // namespace keen_pbr3
