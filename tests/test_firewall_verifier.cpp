#include <doctest/doctest.h>

#include "../src/firewall/iptables_verifier.hpp"
#include "../src/firewall/nftables_verifier.hpp"
#include "../src/util/safe_exec.hpp"

using namespace keen_pbr3;

namespace {

CommandResult command_result(std::string stdout_output = {},
                             int exit_code = 0,
                             bool truncated = false) {
    return CommandResult{
        .stdout_output = std::move(stdout_output),
        .exit_code = exit_code,
        .truncated = truncated,
    };
}

bool matches_args(const std::vector<std::string>& actual,
                  std::initializer_list<const char*> expected) {
    if (actual.size() != expected.size()) return false;

    size_t i = 0;
    for (const char* part : expected) {
        if (actual[i] != part) return false;
        ++i;
    }
    return true;
}

} // namespace

// =============================================================================
// parse_iptables_save tests
// =============================================================================

TEST_CASE("parse_iptables_save: empty string") {
    auto state = parse_iptables_save("");
    CHECK_FALSE(state.has_keen_pbr_chain);
    CHECK_FALSE(state.has_prerouting_jump);
    CHECK(state.rules.empty());
}

TEST_CASE("parse_iptables_save: chain declaration only, no jump") {
    const std::string input =
        "*mangle\n"
        ":KeenPbrTable - [0:0]\n"
        "COMMIT\n";
    auto state = parse_iptables_save(input);
    CHECK(state.has_keen_pbr_chain);
    CHECK_FALSE(state.has_prerouting_jump);
    CHECK(state.rules.empty());
}

TEST_CASE("parse_iptables_save: prerouting jump present") {
    const std::string input =
        ":KeenPbrTable - [0:0]\n"
        "-A PREROUTING -j KeenPbrTable\n";
    auto state = parse_iptables_save(input);
    CHECK(state.has_keen_pbr_chain);
    CHECK(state.has_prerouting_jump);
    CHECK(state.rules.empty());
}

TEST_CASE("parse_iptables_save: mark rule with decimal fwmark") {
    const std::string input =
        ":KeenPbrTable - [0:0]\n"
        "-A PREROUTING -j KeenPbrTable\n"
        "-A KeenPbrTable -m set --match-set myset dst -j MARK --set-mark 65536\n";
    auto state = parse_iptables_save(input);
    CHECK(state.has_keen_pbr_chain);
    CHECK(state.has_prerouting_jump);
    REQUIRE(state.rules.size() == 1);
    CHECK(state.rules[0].set_name == "myset");
    CHECK(state.rules[0].is_mark);
    CHECK_FALSE(state.rules[0].is_drop);
    CHECK(state.rules[0].fwmark == 65536u);
}

TEST_CASE("parse_iptables_save: mark rule with hex fwmark") {
    const std::string input =
        ":KeenPbrTable - [0:0]\n"
        "-A KeenPbrTable -m set --match-set myset dst -j MARK --set-mark 0x10000\n";
    auto state = parse_iptables_save(input);
    REQUIRE(state.rules.size() == 1);
    CHECK(state.rules[0].is_mark);
    CHECK(state.rules[0].fwmark == 0x10000u);
}

TEST_CASE("parse_iptables_save: drop rule") {
    const std::string input =
        ":KeenPbrTable - [0:0]\n"
        "-A KeenPbrTable -m set --match-set blacklist dst -j DROP\n";
    auto state = parse_iptables_save(input);
    REQUIRE(state.rules.size() == 1);
    CHECK(state.rules[0].set_name == "blacklist");
    CHECK(state.rules[0].is_drop);
    CHECK_FALSE(state.rules[0].is_mark);
}

TEST_CASE("parse_iptables_save: multiple rules parsed in order") {
    const std::string input =
        ":KeenPbrTable - [0:0]\n"
        "-A PREROUTING -j KeenPbrTable\n"
        "-A KeenPbrTable -m set --match-set set1 dst -j MARK --set-mark 1\n"
        "-A KeenPbrTable -m set --match-set set2 dst -j DROP\n"
        "-A KeenPbrTable -m set --match-set set3 dst -j MARK --set-mark 2\n";
    auto state = parse_iptables_save(input);
    CHECK(state.has_keen_pbr_chain);
    CHECK(state.has_prerouting_jump);
    REQUIRE(state.rules.size() == 3);
    CHECK(state.rules[0].set_name == "set1");
    CHECK(state.rules[0].is_mark);
    CHECK(state.rules[0].fwmark == 1u);
    CHECK(state.rules[1].set_name == "set2");
    CHECK(state.rules[1].is_drop);
    CHECK(state.rules[2].set_name == "set3");
    CHECK(state.rules[2].is_mark);
    CHECK(state.rules[2].fwmark == 2u);
}

TEST_CASE("parse_iptables_save: ipv6=true produces same result") {
    const std::string input =
        ":KeenPbrTable - [0:0]\n"
        "-A PREROUTING -j KeenPbrTable\n"
        "-A KeenPbrTable -m set --match-set myset dst -j MARK --set-mark 0x20000\n";
    auto state_v4 = parse_iptables_save(input, false);
    auto state_v6 = parse_iptables_save(input, true);
    CHECK(state_v4.has_keen_pbr_chain == state_v6.has_keen_pbr_chain);
    CHECK(state_v4.has_prerouting_jump == state_v6.has_prerouting_jump);
    CHECK(state_v4.rules.size() == state_v6.rules.size());
}

// =============================================================================
// parse_nft_json tests
// =============================================================================

TEST_CASE("parse_nft_json: invalid JSON returns empty state") {
    auto state = parse_nft_json("not json at all");
    CHECK_FALSE(state.has_table);
    CHECK_FALSE(state.has_prerouting_chain);
    CHECK_FALSE(state.has_prerouting_hook);
    CHECK(state.rules.empty());
}

TEST_CASE("parse_nft_json: empty string returns empty state") {
    auto state = parse_nft_json("");
    CHECK_FALSE(state.has_table);
    CHECK(state.rules.empty());
}

TEST_CASE("parse_nft_json: valid JSON with no KeenPbrTable") {
    const std::string input = R"({
        "nftables": [
            {"table": {"family": "inet", "name": "SomeOtherTable"}}
        ]
    })";
    auto state = parse_nft_json(input);
    CHECK_FALSE(state.has_table);
    CHECK_FALSE(state.has_prerouting_chain);
}

TEST_CASE("parse_nft_json: table present, no chain") {
    const std::string input = R"({
        "nftables": [
            {"table": {"family": "inet", "name": "KeenPbrTable"}}
        ]
    })";
    auto state = parse_nft_json(input);
    CHECK(state.has_table);
    CHECK_FALSE(state.has_prerouting_chain);
    CHECK_FALSE(state.has_prerouting_hook);
}

TEST_CASE("parse_nft_json: chain present, no hook") {
    const std::string input = R"({
        "nftables": [
            {"chain": {"family": "inet", "table": "KeenPbrTable", "name": "prerouting"}}
        ]
    })";
    auto state = parse_nft_json(input);
    CHECK_FALSE(state.has_table);
    CHECK(state.has_prerouting_chain);
    CHECK_FALSE(state.has_prerouting_hook);
}

TEST_CASE("parse_nft_json: chain-only output with prerouting hook") {
    const std::string input = R"({
        "nftables": [
            {"chain": {"family": "inet", "table": "KeenPbrTable", "name": "prerouting",
                       "type": "filter", "hook": "prerouting", "prio": -150}}
        ]
    })";
    auto state = parse_nft_json(input);
    CHECK_FALSE(state.has_table);
    CHECK(state.has_prerouting_chain);
    CHECK(state.has_prerouting_hook);
    CHECK(state.rules.empty());
}

TEST_CASE("parse_nft_json: mark rule with fwmark") {
    const std::string input = R"({
        "nftables": [
            {"chain": {"family": "inet", "table": "KeenPbrTable", "name": "prerouting",
                       "type": "filter", "hook": "prerouting"}},
            {"rule": {
                "family": "inet",
                "table": "KeenPbrTable",
                "chain": "prerouting",
                "expr": [
                    {"match": {"op": "==", "left": {"payload": {"protocol": "ip", "field": "daddr"}},
                               "right": "@myset"}},
                    {"mangle": {"key": {"meta": {"key": "mark"}}, "value": 65536}}
                ]
            }}
        ]
    })";
    auto state = parse_nft_json(input);
    REQUIRE(state.rules.size() == 1);
    CHECK(state.rules[0].set_name == "myset");
    CHECK(state.rules[0].is_mark);
    CHECK_FALSE(state.rules[0].is_drop);
    CHECK(state.rules[0].fwmark == 65536u);
}

TEST_CASE("parse_nft_json: drop rule") {
    const std::string input = R"({
        "nftables": [
            {"chain": {"family": "inet", "table": "KeenPbrTable", "name": "prerouting",
                       "type": "filter", "hook": "prerouting"}},
            {"rule": {
                "family": "inet",
                "table": "KeenPbrTable",
                "chain": "prerouting",
                "expr": [
                    {"match": {"op": "==",
                               "left": {"payload": {"protocol": "ip", "field": "daddr"}},
                               "right": "@blacklist"}},
                    {"drop": null}
                ]
            }}
        ]
    })";
    auto state = parse_nft_json(input);
    REQUIRE(state.rules.size() == 1);
    CHECK(state.rules[0].set_name == "blacklist");
    CHECK(state.rules[0].is_drop);
    CHECK_FALSE(state.rules[0].is_mark);
}

TEST_CASE("parse_nft_json: wrong table name returns empty state") {
    const std::string input = R"({
        "nftables": [
            {"table": {"family": "inet", "name": "NotKeenPbrTable"}},
            {"chain": {"family": "inet", "table": "NotKeenPbrTable", "name": "prerouting",
                       "hook": "prerouting"}}
        ]
    })";
    auto state = parse_nft_json(input);
    CHECK_FALSE(state.has_table);
    CHECK_FALSE(state.has_prerouting_chain);
}

// =============================================================================
// IptablesFirewallVerifier::verify_rules with injected CommandRunner
// =============================================================================

TEST_CASE("IptablesFirewallVerifier::verify_rules: mark rule ok") {
    const std::string canned =
        ":KeenPbrTable - [0:0]\n"
        "-A PREROUTING -j KeenPbrTable\n"
        "-A KeenPbrTable -m set --match-set set1 dst -j MARK --set-mark 65536\n";

    auto runner = [&canned](const std::vector<std::string>&) -> CommandResult {
        return command_result(canned);
    };
    IptablesFirewallVerifier verifier(runner);

    RuleState rs;
    rs.rule_index = 0;
    rs.list_names = {"mylist"};
    rs.set_names = {"set1"};
    rs.outbound_tag = "wan1";
    rs.action_type = RuleActionType::Mark;
    rs.fwmark = 65536u;

    auto checks = verifier.verify_rules({rs});
    REQUIRE(checks.size() == 1);
    CHECK(checks[0].status == CheckStatus::ok);
    CHECK(checks[0].set_name == "set1");
    CHECK(checks[0].actual_fwmark.has_value());
    CHECK(*checks[0].actual_fwmark == 65536u);
}

TEST_CASE("IptablesFirewallVerifier::verify_rules: mark rule missing") {
    const std::string canned =
        ":KeenPbrTable - [0:0]\n"
        "-A PREROUTING -j KeenPbrTable\n";

    auto runner = [&canned](const std::vector<std::string>&) -> CommandResult {
        return command_result(canned);
    };
    IptablesFirewallVerifier verifier(runner);

    RuleState rs;
    rs.rule_index = 0;
    rs.set_names = {"missing_set"};
    rs.action_type = RuleActionType::Mark;
    rs.fwmark = 1u;

    auto checks = verifier.verify_rules({rs});
    REQUIRE(checks.size() == 1);
    CHECK(checks[0].status == CheckStatus::missing);
}

TEST_CASE("IptablesFirewallVerifier::verify_rules: fwmark mismatch") {
    const std::string canned =
        ":KeenPbrTable - [0:0]\n"
        "-A KeenPbrTable -m set --match-set set1 dst -j MARK --set-mark 65536\n";

    auto runner = [&canned](const std::vector<std::string>&) -> CommandResult {
        return command_result(canned);
    };
    IptablesFirewallVerifier verifier(runner);

    RuleState rs;
    rs.rule_index = 0;
    rs.set_names = {"set1"};
    rs.action_type = RuleActionType::Mark;
    rs.fwmark = 99999u; // different from actual 65536

    auto checks = verifier.verify_rules({rs});
    REQUIRE(checks.size() == 1);
    CHECK(checks[0].status == CheckStatus::mismatch);
    CHECK(checks[0].actual_fwmark.has_value());
    CHECK(*checks[0].actual_fwmark == 65536u);
}

TEST_CASE("IptablesFirewallVerifier::verify_rules: drop rule ok") {
    const std::string canned =
        ":KeenPbrTable - [0:0]\n"
        "-A KeenPbrTable -m set --match-set blacklist dst -j DROP\n";

    auto runner = [&canned](const std::vector<std::string>&) -> CommandResult {
        return command_result(canned);
    };
    IptablesFirewallVerifier verifier(runner);

    RuleState rs;
    rs.rule_index = 0;
    rs.set_names = {"blacklist"};
    rs.action_type = RuleActionType::Drop;

    auto checks = verifier.verify_rules({rs});
    REQUIRE(checks.size() == 1);
    CHECK(checks[0].status == CheckStatus::ok);
}

TEST_CASE("IptablesFirewallVerifier::verify_rules: skip rule produces no check") {
    const std::string canned = "";
    auto runner = [&canned](const std::vector<std::string>&) -> CommandResult {
        return command_result(canned);
    };
    IptablesFirewallVerifier verifier(runner);

    RuleState rs;
    rs.rule_index = 0;
    rs.set_names = {"skipped_set"};
    rs.action_type = RuleActionType::Skip;

    auto checks = verifier.verify_rules({rs});
    CHECK(checks.empty());
}

TEST_CASE("IptablesFirewallVerifier::verify_chain: scaffold without rules is healthy") {
    const std::string canned =
        "*mangle\n"
        ":KeenPbrTable - [0:0]\n"
        "-A PREROUTING -j KeenPbrTable\n"
        "COMMIT\n";

    auto runner = [&canned](const std::vector<std::string>&) -> CommandResult {
        return command_result(canned);
    };
    IptablesFirewallVerifier verifier(runner);

    const auto check = verifier.verify_chain();
    CHECK(check.chain_present);
    CHECK(check.prerouting_hook_present);
    CHECK(check.detail == "ok");
}

// =============================================================================
// NftablesFirewallVerifier::verify_rules with injected CommandRunner
// =============================================================================

TEST_CASE("NftablesFirewallVerifier::verify_rules: mark rule ok") {
    const std::string canned = R"({
        "nftables": [
            {"chain": {"family": "inet", "table": "KeenPbrTable", "name": "prerouting",
                       "type": "filter", "hook": "prerouting"}},
            {"rule": {
                "family": "inet", "table": "KeenPbrTable", "chain": "prerouting",
                "expr": [
                    {"match": {"op": "==",
                               "left": {"payload": {"protocol": "ip", "field": "daddr"}},
                               "right": "@set1"}},
                    {"mangle": {"key": {"meta": {"key": "mark"}}, "value": 131072}}
                ]
            }}
        ]
    })";

    auto runner = [&canned](const std::vector<std::string>& args) -> CommandResult {
        if (matches_args(args, {"nft", "-j", "list", "chain", "inet", "KeenPbrTable",
                                "prerouting"})) {
            return command_result(canned);
        }
        return command_result({}, 1);
    };
    NftablesFirewallVerifier verifier(runner);

    RuleState rs;
    rs.rule_index = 0;
    rs.set_names = {"set1"};
    rs.action_type = RuleActionType::Mark;
    rs.fwmark = 131072u;

    auto checks = verifier.verify_rules({rs});
    REQUIRE(checks.size() == 1);
    CHECK(checks[0].status == CheckStatus::ok);
    CHECK(checks[0].actual_fwmark.has_value());
    CHECK(*checks[0].actual_fwmark == 131072u);
}

TEST_CASE("NftablesFirewallVerifier::verify_rules: mark rule missing") {
    const std::string canned = R"({
        "nftables": [
            {"chain": {"family": "inet", "table": "KeenPbrTable", "name": "prerouting",
                       "type": "filter", "hook": "prerouting"}}
        ]
    })";

    auto runner = [&canned](const std::vector<std::string>& args) -> CommandResult {
        if (matches_args(args, {"nft", "-j", "list", "chain", "inet", "KeenPbrTable",
                                "prerouting"})) {
            return command_result(canned);
        }
        return command_result({}, 1);
    };
    NftablesFirewallVerifier verifier(runner);

    RuleState rs;
    rs.rule_index = 0;
    rs.set_names = {"not_there"};
    rs.action_type = RuleActionType::Mark;
    rs.fwmark = 1u;

    auto checks = verifier.verify_rules({rs});
    REQUIRE(checks.size() == 1);
    CHECK(checks[0].status == CheckStatus::missing);
}

TEST_CASE("NftablesFirewallVerifier::verify_rules: drop rule ok") {
    const std::string canned = R"({
        "nftables": [
            {"chain": {"family": "inet", "table": "KeenPbrTable", "name": "prerouting",
                       "type": "filter", "hook": "prerouting"}},
            {"rule": {
                "family": "inet", "table": "KeenPbrTable", "chain": "prerouting",
                "expr": [
                    {"match": {"op": "==",
                               "left": {"payload": {"protocol": "ip", "field": "daddr"}},
                               "right": "@dropset"}},
                    {"drop": null}
                ]
            }}
        ]
    })";

    auto runner = [&canned](const std::vector<std::string>& args) -> CommandResult {
        if (matches_args(args, {"nft", "-j", "list", "chain", "inet", "KeenPbrTable",
                                "prerouting"})) {
            return command_result(canned);
        }
        return command_result({}, 1);
    };
    NftablesFirewallVerifier verifier(runner);

    RuleState rs;
    rs.rule_index = 0;
    rs.set_names = {"dropset"};
    rs.action_type = RuleActionType::Drop;

    auto checks = verifier.verify_rules({rs});
    REQUIRE(checks.size() == 1);
    CHECK(checks[0].status == CheckStatus::ok);
}

TEST_CASE("NftablesFirewallVerifier::verify_rules: fwmark mismatch") {
    const std::string canned = R"({
        "nftables": [
            {"chain": {"family": "inet", "table": "KeenPbrTable", "name": "prerouting",
                       "type": "filter", "hook": "prerouting"}},
            {"rule": {
                "family": "inet", "table": "KeenPbrTable", "chain": "prerouting",
                "expr": [
                    {"match": {"op": "==",
                               "left": {"payload": {"protocol": "ip", "field": "daddr"}},
                               "right": "@myset"}},
                    {"mangle": {"key": {"meta": {"key": "mark"}}, "value": 100}}
                ]
            }}
        ]
    })";

    auto runner = [&canned](const std::vector<std::string>& args) -> CommandResult {
        if (matches_args(args, {"nft", "-j", "list", "chain", "inet", "KeenPbrTable",
                                "prerouting"})) {
            return command_result(canned);
        }
        return command_result({}, 1);
    };
    NftablesFirewallVerifier verifier(runner);

    RuleState rs;
    rs.rule_index = 0;
    rs.set_names = {"myset"};
    rs.action_type = RuleActionType::Mark;
    rs.fwmark = 200u; // different from actual 100

    auto checks = verifier.verify_rules({rs});
    REQUIRE(checks.size() == 1);
    CHECK(checks[0].status == CheckStatus::mismatch);
}

TEST_CASE("NftablesFirewallVerifier::verify_chain: missing chain with table present") {
    const std::string table_json = R"({
        "nftables": [
            {"table": {"family": "inet", "name": "KeenPbrTable"}}
        ]
    })";

    auto runner = [&table_json](const std::vector<std::string>& args) -> CommandResult {
        if (matches_args(args, {"nft", "-j", "list", "chain", "inet", "KeenPbrTable",
                                "prerouting"})) {
            return command_result({}, 1);
        }
        if (matches_args(args, {"nft", "-j", "-t", "list", "table", "inet",
                                "KeenPbrTable"})) {
            return command_result(table_json);
        }
        return command_result({}, 1);
    };

    NftablesFirewallVerifier verifier(runner);
    const auto check = verifier.verify_chain();
    CHECK_FALSE(check.chain_present);
    CHECK_FALSE(check.prerouting_hook_present);
    CHECK(check.detail == "prerouting chain not found in KeenPbrTable table");
}

TEST_CASE("NftablesFirewallVerifier::verify_chain: missing table") {
    auto runner = [](const std::vector<std::string>& args) -> CommandResult {
        if (matches_args(args, {"nft", "-j", "list", "chain", "inet", "KeenPbrTable",
                                "prerouting"})) {
            return command_result({}, 1);
        }
        if (matches_args(args, {"nft", "-j", "-t", "list", "table", "inet",
                                "KeenPbrTable"})) {
            return command_result({}, 1);
        }
        return command_result({}, 1);
    };

    NftablesFirewallVerifier verifier(runner);
    const auto check = verifier.verify_chain();
    CHECK_FALSE(check.chain_present);
    CHECK_FALSE(check.prerouting_hook_present);
    CHECK(check.detail == "KeenPbrTable table not found in nftables");
}

TEST_CASE("NftablesFirewallVerifier::verify_chain: truncated chain output reports read failure") {
    auto runner = [](const std::vector<std::string>& args) -> CommandResult {
        if (matches_args(args, {"nft", "-j", "list", "chain", "inet", "KeenPbrTable",
                                "prerouting"})) {
            return command_result("{", -1, true);
        }
        return command_result({}, 1);
    };

    NftablesFirewallVerifier verifier(runner);
    const auto check = verifier.verify_chain();
    CHECK_FALSE(check.chain_present);
    CHECK(check.detail ==
          "nft verification output exceeded capture limit while reading prerouting chain");
}

TEST_CASE("NftablesFirewallVerifier::verify_chain: invalid chain JSON reports parse failure") {
    auto runner = [](const std::vector<std::string>& args) -> CommandResult {
        if (matches_args(args, {"nft", "-j", "list", "chain", "inet", "KeenPbrTable",
                                "prerouting"})) {
            return command_result("not json");
        }
        return command_result({}, 1);
    };

    NftablesFirewallVerifier verifier(runner);
    const auto check = verifier.verify_chain();
    CHECK_FALSE(check.chain_present);
    CHECK(check.detail == "failed to parse nftables JSON while reading prerouting chain");
}

TEST_CASE("NftablesFirewallVerifier::verify_rules: read failure is surfaced in rule detail") {
    auto runner = [](const std::vector<std::string>& args) -> CommandResult {
        if (matches_args(args, {"nft", "-j", "list", "chain", "inet", "KeenPbrTable",
                                "prerouting"})) {
            return command_result("{", -1, true);
        }
        return command_result({}, 1);
    };

    NftablesFirewallVerifier verifier(runner);

    RuleState rs;
    rs.rule_index = 0;
    rs.set_names = {"set1"};
    rs.action_type = RuleActionType::Mark;
    rs.fwmark = 1u;

    const auto checks = verifier.verify_rules({rs});
    REQUIRE(checks.size() == 1);
    CHECK(checks[0].status == CheckStatus::missing);
    CHECK(checks[0].detail ==
          "nft verification output exceeded capture limit while reading prerouting chain");
}

TEST_CASE("safe_exec_capture: max_bytes overflow sets truncated") {
    const auto result = safe_exec_capture({"head", "-c", "4096", "/dev/zero"},
                                          /*suppress_stderr=*/true,
                                          /*max_bytes=*/64);
    CHECK(result.truncated);
    CHECK(result.stdout_output.size() > 64);
}

TEST_CASE("safe_exec_capture: nonzero exit code is preserved") {
    const auto result = safe_exec_capture({"false"});
    CHECK_FALSE(result.truncated);
    CHECK(result.exit_code == 1);
}
