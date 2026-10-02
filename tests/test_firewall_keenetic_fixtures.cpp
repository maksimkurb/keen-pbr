#include <doctest/doctest.h>

#include "../src/firewall/firewall_physical.hpp"
#include "firewall_fixtures.hpp"

#include <algorithm>
#include <string>
#include <variant>
#include <vector>

namespace keen_pbr3 {
namespace {

using Role = PhysicalChainRole;
using Gen = PhysicalGeneration;
using Table = PhysicalTable;
using Fam = FirewallFamily;

// Helper to create chain identifiers
PhysicalChainId ipt_id(Role role, Table table, Fam family,
                       Gen generation = Gen::none) {
  PhysicalChainId id;
  id.role = role;
  id.table = table;
  id.family = family;
  id.generation = generation;
  return id;
}

PhysicalChainId sys_id(Role role, Table table, Fam family) {
  PhysicalChainId id;
  id.role = role;
  id.table = table;
  id.family = family;
  return id;
}

// Check if a rule contains UnknownMatch or UnknownStmt
bool rule_has_unknown(const PhysicalRule &rule) {
  for (const auto &match : rule.matches) {
    if (std::holds_alternative<UnknownMatch>(match)) return true;
  }
  for (const auto &stmt : rule.statements) {
    if (std::holds_alternative<UnknownStmt>(stmt)) return true;
    // LateMatchStmt can contain unknown matches
    if (auto late_match = std::get_if<LateMatchStmt>(&stmt)) {
      if (std::holds_alternative<UnknownMatch>(late_match->match))
        return true;
    }
  }
  return false;
}

// Check if any rule in the chain has unknown matches/statements
bool chain_has_unknown(const PhysicalChain &chain) {
  for (const auto &rule : chain.rules) {
    if (rule_has_unknown(rule)) return true;
  }
  return false;
}

// Check if any rule references NDM targets
bool rule_references_ndm(const PhysicalRule &rule) {
  for (const auto &stmt : rule.statements) {
    if (auto jump = std::get_if<JumpStmt>(&stmt)) {
      if (!jump->target.name.empty() &&
          jump->target.name.find("_NDM") != std::string::npos) {
        return true;
      }
    }
  }
  return false;
}

// Check if chain name starts with _NDM
bool is_ndm_chain(const std::string &name) {
  return name.size() > 4 && name.substr(0, 4) == "_NDM";
}

// Get a system chain from ruleset
const PhysicalChain *find_system_chain(const PhysicalRuleset &set,
                                       const std::string &chain_name,
                                       Table table, Fam family) {
  for (const auto &chain : set.chains) {
    if (chain.id.name == chain_name && chain.id.table == table &&
        chain.id.family == family) {
      return &chain;
    }
  }
  return nullptr;
}

// ---------------------------------------------------------------------------
// Test 1: mangle_v4 fixture parsing and validation
// ---------------------------------------------------------------------------

TEST_CASE("keenetic fixtures: mangle_v4 parsing and validation") {
  const std::string text =
      read_fixture("keenetic_1.4.21/mangle_v4.rules");
  const auto set = parse_iptables_save(text, Fam::ipv4, Table::mangle);

  SUBCASE("parse succeeds and all chains are present") {
    CHECK(!set.chains.empty());
  }

  SUBCASE("no unknown matches or statements in all keen-pbr chains") {
    for (const auto &chain : set.chains) {
      // Check keen-pbr owned chains only (not foreign _NDM chains)
      if (!chain.id.name.empty() && chain.id.name.find("KeenPbr") != std::string::npos) {
        for (const auto &rule : chain.rules) {
          CHECK_MESSAGE(!rule_has_unknown(rule),
                        "Chain " << chain.id.name << " has unknown in rule");
        }
      }
    }
  }

  SUBCASE("no foreign NDM chains are captured") {
    for (const auto &chain : set.chains) {
      CHECK_MESSAGE(!is_ndm_chain(chain.id.name),
                    "NDM chain captured: " << chain.id.name);
    }
  }

  SUBCASE("no rules reference NDM targets") {
    for (const auto &chain : set.chains) {
      for (const auto &rule : chain.rules) {
        CHECK_MESSAGE(!rule_references_ndm(rule),
                      "Chain " << chain.id.name
                               << " references NDM target");
      }
    }
  }

  SUBCASE("system OUTPUT chain has exactly one rule (jump to KeenPbrOutput)") {
    auto output = find_system_chain(set, "OUTPUT", Table::mangle, Fam::ipv4);
    REQUIRE_MESSAGE(output != nullptr, "OUTPUT chain not found");
    REQUIRE(output->rules.size() == 1);
    REQUIRE(output->rules[0].statements.size() == 1);
    auto jump = std::get_if<JumpStmt>(&output->rules[0].statements[0]);
    REQUIRE(jump != nullptr);
    CHECK(jump->target.name == "KeenPbrOutput");
  }

  SUBCASE("system PREROUTING has no keen-pbr jump") {
    auto prerouting = find_system_chain(set, "PREROUTING", Table::mangle, Fam::ipv4);
    if (prerouting != nullptr) {
      for (const auto &rule : prerouting->rules) {
        for (const auto &stmt : rule.statements) {
          if (auto jump = std::get_if<JumpStmt>(&stmt)) {
            CHECK_MESSAGE(jump->target.name.find("KeenPbr") == std::string::npos,
                          "PREROUTING has KeenPbr jump");
          }
        }
      }
    }
  }

  SUBCASE("KeenPbrOutput_B has exactly 20 rules") {
    auto chain_b = ipt_id(Role::output_generation, Table::mangle,
                          Fam::ipv4, Gen::b);
    const PhysicalChain *b = set.find(chain_b);
    REQUIRE_MESSAGE(b != nullptr, "KeenPbrOutput_B not found");
    CHECK(b->rules.size() == 20);
  }

  SUBCASE("KeenPbrOutput_B first rule has CopyMark restore statement") {
    auto chain_b = ipt_id(Role::output_generation, Table::mangle,
                          Fam::ipv4, Gen::b);
    const PhysicalChain *b = set.find(chain_b);
    REQUIRE_MESSAGE(b != nullptr, "KeenPbrOutput_B not found");
    REQUIRE(b->rules.size() > 0);
    const auto &rule = b->rules[0];

    bool has_restore = false;
    for (const auto &stmt : rule.statements) {
      if (auto copy_mark = std::get_if<CopyMarkStmt>(&stmt)) {
        if (!copy_mark->to_conntrack) {  // restore-mark
          has_restore = true;
        }
      }
    }
    CHECK_MESSAGE(has_restore, "First rule missing CopyMarkStmt restore");
  }

  SUBCASE("KeenPbrOutput_B fifth rule (index 4) has IifMatch negated br0 and RETURN") {
    auto chain_b = ipt_id(Role::output_generation, Table::mangle,
                          Fam::ipv4, Gen::b);
    const PhysicalChain *b = set.find(chain_b);
    REQUIRE_MESSAGE(b != nullptr, "KeenPbrOutput_B not found");
    REQUIRE(b->rules.size() > 4);
    const auto &rule = b->rules[4];

    bool has_iif_match = false;
    for (const auto &match : rule.matches) {
      if (auto iif = std::get_if<IifMatch>(&match)) {
        if (iif->negate && iif->names.size() == 1 && iif->names[0] == "br0") {
          has_iif_match = true;
        }
      }
    }
    CHECK_MESSAGE(has_iif_match, "Rule 5 missing negated IifMatch br0");

    bool has_return = false;
    for (const auto &stmt : rule.statements) {
      if (auto verdict = std::get_if<VerdictStmt>(&stmt)) {
        if (verdict->verdict == PhysicalVerdict::return_) {
          has_return = true;
        }
      }
    }
    CHECK_MESSAGE(has_return, "Rule 5 missing RETURN verdict");
  }

  SUBCASE("DSCP rule parses correctly (0x2c -> 44, SetMark 0x10000/0xff0000)") {
    auto chain_b = ipt_id(Role::output_generation, Table::mangle,
                          Fam::ipv4, Gen::b);
    const PhysicalChain *b = set.find(chain_b);
    REQUIRE_MESSAGE(b != nullptr, "KeenPbrOutput_B not found");

    bool found_dscp = false;
    for (const auto &rule : b->rules) {
      bool has_dscp_0x2c = false;
      bool has_setmark = false;

      for (const auto &match : rule.matches) {
        if (auto dscp = std::get_if<DscpMatch>(&match)) {
          if (dscp->value == 0x2c) {  // 44 in decimal
            has_dscp_0x2c = true;
          }
        }
      }

      for (const auto &stmt : rule.statements) {
        if (auto setmark = std::get_if<SetMarkStmt>(&stmt)) {
          if (setmark->value == 0x10000 && setmark->mask == 0xff0000) {
            has_setmark = true;
          }
        }
      }

      if (has_dscp_0x2c && has_setmark) {
        found_dscp = true;
        break;
      }
    }
    CHECK_MESSAGE(found_dscp, "DSCP 0x2c rule not parsed correctly");
  }

  SUBCASE("mark negation parses with full mask (! --mark 0x0 -> mask 0xffffffff)") {
    auto chain_b = ipt_id(Role::output_generation, Table::mangle,
                          Fam::ipv4, Gen::b);
    const PhysicalChain *b = set.find(chain_b);
    REQUIRE_MESSAGE(b != nullptr, "KeenPbrOutput_B not found");

    bool found_mark_negation = false;
    for (const auto &rule : b->rules) {
      for (const auto &match : rule.matches) {
        if (auto mark = std::get_if<MarkMatch>(&match)) {
          if (mark->negate && mark->mask == 0xFFFFFFFFu &&
              mark->values.size() == 1 && mark->values[0] == 0) {
            found_mark_negation = true;
          }
        }
      }
    }
    CHECK_MESSAGE(found_mark_negation,
                  "Mark negation with full mask not found");
  }

  SUBCASE("all keen-pbr rules have no key") {
    for (const auto &chain : set.chains) {
      if (!chain.id.name.empty() && chain.id.name.find("KeenPbr") != std::string::npos) {
        for (const auto &rule : chain.rules) {
          CHECK_MESSAGE(!rule.key.has_value(),
                        "KeenPbr rule has key (unexpected)");
        }
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Test 2: raw_v4 fixture parsing and validation
// ---------------------------------------------------------------------------

TEST_CASE("keenetic fixtures: raw_v4 parsing and validation") {
  const std::string text =
      read_fixture("keenetic_1.4.21/raw_v4.rules");
  const auto set = parse_iptables_save(text, Fam::ipv4, Table::raw);

  SUBCASE("parse succeeds and all chains are present") {
    CHECK(!set.chains.empty());
  }

  SUBCASE("no unknown matches or statements in all keen-pbr chains") {
    for (const auto &chain : set.chains) {
      if (!chain.id.name.empty() && chain.id.name.find("KeenPbr") != std::string::npos) {
        for (const auto &rule : chain.rules) {
          CHECK_MESSAGE(!rule_has_unknown(rule),
                        "Chain " << chain.id.name << " has unknown");
        }
      }
    }
  }

  SUBCASE("system PREROUTING has exactly one rule (jump to KeenPbrRaw)") {
    auto prerouting = find_system_chain(set, "PREROUTING", Table::raw, Fam::ipv4);
    REQUIRE_MESSAGE(prerouting != nullptr, "PREROUTING chain not found");
    REQUIRE(prerouting->rules.size() == 1);
    REQUIRE(prerouting->rules[0].statements.size() == 1);
    auto jump = std::get_if<JumpStmt>(&prerouting->rules[0].statements[0]);
    REQUIRE(jump != nullptr);
    CHECK(jump->target.name == "KeenPbrRaw");
  }

  SUBCASE("both KeenPbrRaw_A and KeenPbrRaw_B are captured") {
    auto chain_a = ipt_id(Role::prerouting_generation, Table::raw,
                          Fam::ipv4, Gen::a);
    auto chain_b = ipt_id(Role::prerouting_generation, Table::raw,
                          Fam::ipv4, Gen::b);
    const PhysicalChain *a = set.find(chain_a);
    const PhysicalChain *b = set.find(chain_b);
    REQUIRE_MESSAGE(a != nullptr, "KeenPbrRaw_A not found");
    REQUIRE_MESSAGE(b != nullptr, "KeenPbrRaw_B not found");
    CHECK(a->rules.size() > 0);
    CHECK(b->rules.size() > 0);
  }

  SUBCASE("all keen-pbr rules have no key") {
    for (const auto &chain : set.chains) {
      if (!chain.id.name.empty() && chain.id.name.find("KeenPbr") != std::string::npos) {
        for (const auto &rule : chain.rules) {
          CHECK_MESSAGE(!rule.key.has_value(),
                        "KeenPbr rule has key (unexpected)");
        }
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Test 3: mangle_v6 fixture parsing and validation
// ---------------------------------------------------------------------------

TEST_CASE("keenetic fixtures: mangle_v6 parsing and validation") {
  const std::string text =
      read_fixture("keenetic_1.4.21/mangle_v6.rules");
  const auto set = parse_iptables_save(text, Fam::ipv6, Table::mangle);

  SUBCASE("parse succeeds and all chains are present") {
    CHECK(!set.chains.empty());
  }

  SUBCASE("no unknown matches or statements in all keen-pbr chains") {
    for (const auto &chain : set.chains) {
      if (!chain.id.name.empty() && chain.id.name.find("KeenPbr") != std::string::npos) {
        for (const auto &rule : chain.rules) {
          CHECK_MESSAGE(!rule_has_unknown(rule),
                        "Chain " << chain.id.name << " has unknown");
        }
      }
    }
  }

  SUBCASE("no foreign NDM chains are captured") {
    for (const auto &chain : set.chains) {
      CHECK_MESSAGE(!is_ndm_chain(chain.id.name),
                    "NDM chain captured: " << chain.id.name);
    }
  }

  SUBCASE("system OUTPUT chain has exactly one rule (jump to KeenPbrOutput)") {
    auto output = find_system_chain(set, "OUTPUT", Table::mangle, Fam::ipv6);
    REQUIRE_MESSAGE(output != nullptr, "OUTPUT chain not found");
    REQUIRE(output->rules.size() == 1);
    REQUIRE(output->rules[0].statements.size() == 1);
    auto jump = std::get_if<JumpStmt>(&output->rules[0].statements[0]);
    REQUIRE(jump != nullptr);
    CHECK(jump->target.name == "KeenPbrOutput");
  }

  SUBCASE("all keen-pbr rules have no key") {
    for (const auto &chain : set.chains) {
      if (!chain.id.name.empty() && chain.id.name.find("KeenPbr") != std::string::npos) {
        for (const auto &rule : chain.rules) {
          CHECK_MESSAGE(!rule.key.has_value(),
                        "KeenPbr rule has key (unexpected)");
        }
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Test 4: raw_v6 fixture parsing and validation
// ---------------------------------------------------------------------------

TEST_CASE("keenetic fixtures: raw_v6 parsing and validation") {
  const std::string text =
      read_fixture("keenetic_1.4.21/raw_v6.rules");
  const auto set = parse_iptables_save(text, Fam::ipv6, Table::raw);

  SUBCASE("parse succeeds and all chains are present") {
    CHECK(!set.chains.empty());
  }

  SUBCASE("no unknown matches or statements in all keen-pbr chains") {
    for (const auto &chain : set.chains) {
      if (!chain.id.name.empty() && chain.id.name.find("KeenPbr") != std::string::npos) {
        for (const auto &rule : chain.rules) {
          CHECK_MESSAGE(!rule_has_unknown(rule),
                        "Chain " << chain.id.name << " has unknown");
        }
      }
    }
  }

  SUBCASE("system PREROUTING has exactly one rule (jump to KeenPbrRaw)") {
    auto prerouting = find_system_chain(set, "PREROUTING", Table::raw, Fam::ipv6);
    REQUIRE_MESSAGE(prerouting != nullptr, "PREROUTING chain not found");
    REQUIRE(prerouting->rules.size() == 1);
    REQUIRE(prerouting->rules[0].statements.size() == 1);
    auto jump = std::get_if<JumpStmt>(&prerouting->rules[0].statements[0]);
    REQUIRE(jump != nullptr);
    CHECK(jump->target.name == "KeenPbrRaw");
  }

  SUBCASE("all keen-pbr rules have no key") {
    for (const auto &chain : set.chains) {
      if (!chain.id.name.empty() && chain.id.name.find("KeenPbr") != std::string::npos) {
        for (const auto &rule : chain.rules) {
          CHECK_MESSAGE(!rule.key.has_value(),
                        "KeenPbr rule has key (unexpected)");
        }
      }
    }
  }
}

}  // namespace
}  // namespace keen_pbr3
