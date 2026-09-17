#include <doctest/doctest.h>

#include "../src/firewall/firewall_snapshot.hpp"

#include <netinet/in.h>
#include <nlohmann/json.hpp>

namespace keen_pbr3 {
namespace {

FirewallPlan mark_plan(const FirewallRuleKey& key,
                       uint32_t fwmark_mask = 0xFFFFFFFFu) {
    FirewallPlan plan;
    plan.fwmark_mask = fwmark_mask;
    FirewallRuleInstance rule;
    rule.key = key;
    rule.family = FirewallFamily::ipv4;
    rule.criteria.dst_set_name = "kpbr4_list";
    rule.action = MarkAction{0x10000u, fwmark_mask};
    FirewallRuleRegistrar registrar(plan);
    registrar.register_rule(std::move(rule));
    registrar.finish();
    return plan;
}

FirewallPlan balance_plan(const FirewallRuleKey& key,
                          uint32_t fwmark_mask = 0x00FF0000u) {
    FirewallPlan plan;
    plan.fwmark_mask = fwmark_mask;
    FirewallRuleInstance rule;
    rule.key = key;
    rule.family = FirewallFamily::ipv4;
    rule.criteria.dst_set_name = "kpbr4_list";
    rule.action = BalanceAction{
        0x30000u, {{0x10000u, true, false}, {0x20000u, true, false}}};
    FirewallRuleRegistrar registrar(plan);
    registrar.register_rule(std::move(rule));
    registrar.finish();
    return plan;
}

FirewallPlan port_plan(const FirewallRuleKey& key) {
    FirewallPlan plan;
    FirewallRuleInstance rule;
    rule.key = key;
    rule.family = FirewallFamily::ipv4;
    rule.criteria.proto = L4Proto::Tcp;
    rule.criteria.dst_port = "443";
    rule.action = MarkAction{0x10000u};
    FirewallRuleRegistrar registrar(plan);
    registrar.register_rule(std::move(rule));
    registrar.finish();
    return plan;
}

FirewallPlan prefilter_plan(const FirewallRuleKey& key,
                            FirewallRuleAction action) {
    FirewallPlan plan;
    plan.fwmark_mask = 0x00FF0000u;
    FirewallRuleInstance rule;
    rule.key = key;
    rule.stage = FirewallRuleStage::restore_conntrack;
    rule.family = FirewallFamily::any;
    rule.action = std::move(action);
    FirewallRuleRegistrar registrar(plan);
    registrar.register_rule(std::move(rule));
    registrar.finish();
    return plan;
}

std::vector<FirewallHook> nft_prefilter_hooks(const FirewallRuleAction& action) {
    if (std::holds_alternative<SkipEstablishedOrDnatAction>(action) ||
        std::holds_alternative<InboundInterfaceFilterAction>(action)) {
        return {FirewallHook::prerouting};
    }
    return {FirewallHook::prerouting, FirewallHook::output};
}

ObservedFirewallRule observed_prefilter(const FirewallRuleInstance& expected,
                                         FirewallHook hook) {
    ObservedFirewallRule observed;
    observed.key = expected.key;
    observed.hook = hook;
    observed.family = FirewallFamily::any;
    observed.action = expected.action;
    return observed;
}

nlohmann::json balance_document() {
    nlohmann::json document;
    auto& entries = document["nftables"];
    entries = nlohmann::json::array();
    entries.push_back({{"table", {{"family", "inet"}, {"name", "KeenPbrTable"}}}});
    entries.push_back({{"chain", {{"family", "inet"}, {"table", "KeenPbrTable"},
                                   {"name", "prerouting"}, {"type", "filter"},
                                   {"hook", "prerouting"}}}});
    for (const auto& name : {"setmark_00010000", "setmark_00020000"}) {
        entries.push_back({{"chain", {{"family", "inet"}, {"table", "KeenPbrTable"},
                                       {"name", name}}}});
    }

    const auto setter_value = [](uint32_t mark, const char* source) {
        nlohmann::json value;
        value["|"] = nlohmann::json::array();
        nlohmann::json mark_source;
        mark_source[source] = {{"key", "mark"}};
        value["|"].push_back({{"&", nlohmann::json::array({
            mark_source, 0xFF00FFFFu})}});
        value["|"].push_back(mark);
        return value;
    };
    for (const auto& [chain, mark] : {std::pair<const char*, uint32_t>{
                                           "setmark_00010000", 0x10000u},
                                       {"setmark_00020000", 0x20000u}}) {
        nlohmann::json rule;
        rule["family"] = "inet";
        rule["table"] = "KeenPbrTable";
        rule["chain"] = chain;
        rule["expr"] = nlohmann::json::array();
        rule["expr"].push_back({{"mangle", {{"key", {{"meta", {{"key", "mark"}}}}},
                                             {"value", setter_value(mark, "meta")}}}});
        rule["expr"].push_back({{"mangle", {{"key", {{"ct", {{"key", "mark"}}}}},
                                             {"value", setter_value(mark, "ct")}}}});
        rule["expr"].push_back({{"accept", nullptr}});
        entries.push_back({{"rule", std::move(rule)}});
    }

    nlohmann::json policy;
    policy["family"] = "inet";
    policy["table"] = "KeenPbrTable";
    policy["chain"] = "prerouting";
    policy["comment"] = "kpbr:v1:route.balance:one";
    policy["expr"] = nlohmann::json::array();
    policy["expr"].push_back({{"match", {{"op", "=="},
                                            {"left", {{"payload", {{"protocol", "ip"},
                                                                       {"field", "daddr"}}}}},
                                            {"right", "@kpbr4_list"}}}});
    policy["expr"].push_back({{"match", {{"op", "=="},
                                            {"left", {{"&", nlohmann::json::array({
                                                {{"meta", {{"key", "mark"}}}},
                                                0x00FF0000u})}}},
                                            {"right", 0}}}});
    policy["expr"].push_back({{"counter", nullptr}});
    nlohmann::json vmap;
    vmap["key"]["numgen"]["mode"] = "inc";
    vmap["key"]["numgen"]["mod"] = 2;
    vmap["data"]["set"] = nlohmann::json::array({
        nlohmann::json::array({0, {{"jump", {{"target", "setmark_00010000"}}}}}),
        nlohmann::json::array({1, {{"jump", {{"target", "setmark_00020000"}}}}})});
    policy["expr"].push_back({{"vmap", std::move(vmap)}});
    policy["expr"].push_back({{"accept", nullptr}});
    entries.push_back({{"rule", std::move(policy)}});
    return document;
}

CommandResult command_result(std::string output, int status = 0) {
    return CommandResult{std::move(output), status, false};
}

} // namespace

TEST_CASE("iptables snapshot parses one active logical bundle") {
    const auto key = FirewallRuleKey{"route.mark", "one"};
    const std::string rules =
        "-N KeenPbrTable\n"
        "-N KeenPbrTable_A\n"
        "-A PREROUTING -j KeenPbrTable\n"
        "-A KeenPbrTable -j KeenPbrTable_A\n"
        "-N KeenPbrTable_OUTPUT\n"
        "-A OUTPUT -j KeenPbrTable_OUTPUT\n"
        "-A KeenPbrTable_OUTPUT -j KeenPbrTable_A\n"
        "-A KeenPbrTable_A -m set --match-set kpbr4s_list dst "
        "-m comment --comment kpbr:v1:route.mark:one "
        "-j MARK --set-xmark 0x10000/0xffffffff\n"
        "-A KeenPbrTable_A -m set --match-set kpbr4s_list dst "
        "-m comment --comment kpbr:v1:route.mark:one -j RETURN\n"
        "-N KeenPbrTable_B\n"
        "-A KeenPbrTable_B -m set --match-set kpbr4s_list dst "
        "-m comment --comment kpbr:v1:route.mark:stale -j MARK --set-mark 1\n";
    int calls = 0;
    const auto snapshot = inspect_iptables_snapshot(CommandRunner(
        [&](const std::vector<std::string>& args) {
            ++calls;
            if (args[0] == "iptables") return command_result(rules);
            if (args[0] == "ip6tables") return command_result("");
            return command_result("create kpbr4s_list hash:net family inet\n");
        }));

    REQUIRE(snapshot.available);
    CHECK(calls == 3);
    REQUIRE(snapshot.rules.size() == 4);
    CHECK(snapshot.rules[0].key == std::optional<FirewallRuleKey>{key});
    CHECK(snapshot.rules[0].hook == FirewallHook::prerouting);
    CHECK(snapshot.rules[0].family == FirewallFamily::ipv4);
    CHECK(snapshot.rules[0].criteria.dst_set_name ==
          std::optional<std::string>{"kpbr4s_list"});
    CHECK(snapshot.rules[1].hook == FirewallHook::output);
    CHECK(snapshot.sets[0].family == FirewallFamily::ipv4);
    CHECK(verify_firewall_plan(mark_plan(key), snapshot)[0].status == CheckStatus::ok);
}

TEST_CASE("iptables keyed RETURN is only a matching MARK companion") {
    const auto key = FirewallRuleKey{"route.mark", "one"};
    const std::string rules =
        "-N KeenPbrTable\n"
        "-N KeenPbrTable_A\n"
        "-A PREROUTING -j KeenPbrTable\n"
        "-A KeenPbrTable -j KeenPbrTable_A\n"
        "-A KeenPbrTable_A -m set --match-set kpbr4s_list dst "
        "-m comment --comment kpbr:v1:route.mark:one "
        "-j MARK --set-xmark 0x10000/0xffffffff\n"
        "-A KeenPbrTable_A -m set --match-set kpbr4s_list dst "
        "-m comment --comment kpbr:v1:route.mark:one "
        "-j CONNMARK --save-mark --mask 0xffffffff\n"
        "-A KeenPbrTable_A -p tcp -m set --match-set kpbr4s_list dst "
        "-m comment --comment kpbr:v1:route.mark:one -j RETURN\n";
    const auto snapshot = inspect_iptables_snapshot(CommandRunner(
        [&](const std::vector<std::string>& args) {
            if (args[0] == "iptables") return command_result(rules);
            if (args[0] == "ip6tables") return command_result("");
            return command_result("create kpbr4s_list hash:net family inet\n");
        }));

    REQUIRE(snapshot.available);
    REQUIRE(snapshot.rules.size() == 2);
    const auto checks = verify_firewall_plan(mark_plan(key), snapshot);
    REQUIRE(checks.size() == 1);
    CHECK(checks[0].status == CheckStatus::mismatch);
    CHECK(checks[0].detail.find("key=route.mark:one") != std::string::npos);
    CHECK(checks[0].detail.find("duplicate") != std::string::npos);
}

TEST_CASE("nft snapshot parses the same canonical rule and reads once") {
    const auto key = FirewallRuleKey{"route.mark", "one"};
    const std::string json = R"({"nftables":[
      {"table":{"family":"inet","name":"KeenPbrTable"}},
      {"chain":{"family":"inet","table":"KeenPbrTable","name":"prerouting",
                  "type":"filter","hook":"prerouting"}},
      {"rule":{"family":"inet","table":"KeenPbrTable","chain":"prerouting",
                "comment":"kpbr:v1:route.mark:one","expr":[
                  {"match":{"op":"==","left":{"payload":{"protocol":"ip","field":"daddr"}},"right":"@kpbr4_list"}},
                  {"mangle":{"key":{"meta":{"key":"mark"}},"value":65536}},
                  {"accept":null}]}}
    ]})";
    int calls = 0;
    const auto snapshot = inspect_nftables_snapshot(CommandRunner(
        [&](const std::vector<std::string>&) {
            ++calls;
            return command_result(json);
        }));

    REQUIRE(snapshot.available);
    CHECK(calls == 1);
    REQUIRE(snapshot.rules.size() == 1);
    CHECK(snapshot.rules[0].key == std::optional<FirewallRuleKey>{key});
    CHECK(verify_firewall_plan(mark_plan(key), snapshot)[0].status == CheckStatus::ok);
}

TEST_CASE("nft snapshot recovers masks from conntrack setter chains") {
    const auto key = FirewallRuleKey{"route.mark", "one"};
    const std::string json = R"({"nftables":[
      {"table":{"family":"inet","name":"KeenPbrTable"}},
      {"chain":{"family":"inet","table":"KeenPbrTable","name":"prerouting",
                  "type":"filter","hook":"prerouting"}},
      {"chain":{"family":"inet","table":"KeenPbrTable","name":"setmark_00010000"}},
      {"rule":{"family":"inet","table":"KeenPbrTable","chain":"setmark_00010000",
                "expr":[
                  {"mangle":{"key":{"meta":{"key":"mark"}},"value":{"|":[
                    {"&":[{"meta":{"key":"mark"}},4278255615]},65536]}}},
                  {"mangle":{"key":{"ct":{"key":"mark"}},"value":{"|":[
                    {"&":[{"ct":{"key":"mark"}},4278255615]},65536]}}},
                  {"accept":null}]}},
      {"rule":{"family":"inet","table":"KeenPbrTable","chain":"prerouting",
                "comment":"kpbr:v1:route.mark:one","expr":[
                  {"match":{"op":"==","left":{"payload":{"protocol":"ip","field":"daddr"}},"right":"@kpbr4_list"}},
                  {"jump":{"target":"setmark_00010000"}}]}}
    ]})";
    const auto snapshot = inspect_nftables_snapshot(CommandRunner(
        [&](const std::vector<std::string>&) { return command_result(json); }));

    REQUIRE(snapshot.available);
    REQUIRE(snapshot.rules.size() == 1);
    const auto& mark = std::get<MarkAction>(snapshot.rules[0].action);
    CHECK(mark == MarkAction{0x10000u, 0xff0000u});
    CHECK(verify_firewall_plan(mark_plan(key, 0xff0000u), snapshot)[0].status ==
          CheckStatus::ok);
}

TEST_CASE("nft snapshot keeps explicit meta nfproto family constraints") {
    const auto key = FirewallRuleKey{"route.port", "nfproto"};
    const auto verify_family = [&](nlohmann::json nfproto,
                                   FirewallFamily expected_family,
                                   CheckStatus expected_status) {
        nlohmann::json document;
        auto& entries = document["nftables"];
        entries = nlohmann::json::array({
            {{"table", {{"family", "inet"}, {"name", "KeenPbrTable"}}}},
            {{"chain", {{"family", "inet"}, {"table", "KeenPbrTable"},
                         {"name", "prerouting"}, {"type", "filter"},
                         {"hook", "prerouting"}}}},
        });
        nlohmann::json rule;
        rule["family"] = "inet";
        rule["table"] = "KeenPbrTable";
        rule["chain"] = "prerouting";
        rule["comment"] = "kpbr:v1:route.port:nfproto";
        rule["expr"] = nlohmann::json::array({
            {{"match", {{"op", "=="},
                         {"left", {{"meta", {{"key", "nfproto"}}}}},
                         {"right", std::move(nfproto)}}}},
            {{"match", {{"op", "=="},
                         {"left", {{"payload", {{"protocol", "tcp"},
                                                   {"field", "dport"}}}}},
                         {"right", 443}}}},
        });
        nlohmann::json mangle;
        mangle["key"]["meta"]["key"] = "mark";
        mangle["value"] = 0x10000;
        rule["expr"].push_back({{"mangle", std::move(mangle)}});
        rule["expr"].push_back({{"accept", nullptr}});
        entries.push_back({{"rule", std::move(rule)}});

        const auto snapshot = inspect_nftables_snapshot(CommandRunner(
            [&](const std::vector<std::string>&) {
                return command_result(document.dump());
            }));
        REQUIRE(snapshot.available);
        REQUIRE(snapshot.rules.size() == 1);
        CHECK(snapshot.rules[0].family == expected_family);
        const auto checks = verify_firewall_plan(port_plan(key), snapshot);
        REQUIRE(checks.size() == 1);
        CHECK(checks[0].status == expected_status);
        if (expected_status == CheckStatus::mismatch) {
            CHECK(checks[0].detail.find("family mismatch") != std::string::npos);
        }
    };

    verify_family(2, FirewallFamily::ipv4, CheckStatus::ok);
    verify_family("ipv4", FirewallFamily::ipv4, CheckStatus::ok);
    verify_family(10, FirewallFamily::ipv6, CheckStatus::mismatch);
    verify_family("ipv6", FirewallFamily::ipv6, CheckStatus::mismatch);
}

TEST_CASE("nft balance snapshot rejects altered selector, guard, mapping, and setters") {
    const auto key = FirewallRuleKey{"route.balance", "one"};
    const auto plan = balance_plan(key);

    const auto verify = [&](nlohmann::json document) {
        const auto snapshot = inspect_nftables_snapshot(CommandRunner(
            [&](const std::vector<std::string>&) {
                return command_result(document.dump());
            }));
        REQUIRE(snapshot.available);
        REQUIRE(snapshot.rules.size() == 1);
        const auto checks = verify_firewall_plan(plan, snapshot);
        return checks[0].status;
    };

    CHECK(verify(balance_document()) == CheckStatus::ok);

    auto swapped = balance_document();
    auto& swapped_targets = swapped["nftables"][6]["rule"]["expr"][3]
        ["vmap"]["data"]["set"];
    std::swap(swapped_targets[0], swapped_targets[1]);
    CHECK(verify(std::move(swapped)) == CheckStatus::mismatch);

    auto wrong_modulus = balance_document();
    wrong_modulus["nftables"][6]["rule"]["expr"][3]
        ["vmap"]["key"]["numgen"]["mod"] = 3;
    CHECK(verify(std::move(wrong_modulus)) == CheckStatus::mismatch);

    auto missing_guard = balance_document();
    missing_guard["nftables"][6]["rule"]["expr"].erase(
        missing_guard["nftables"][6]["rule"]["expr"].begin() + 1);
    CHECK(verify(std::move(missing_guard)) == CheckStatus::mismatch);

    auto wrong_guard = balance_document();
    wrong_guard["nftables"][6]["rule"]["expr"][1]["match"]["left"]["&"][1] =
        0xFFFFFFFFu;
    CHECK(verify(std::move(wrong_guard)) == CheckStatus::mismatch);

    auto foreign_bits_setter = balance_document();
    foreign_bits_setter["nftables"][4]["rule"]["expr"][0]["mangle"]
        ["value"]["|"][0]["&"][1] = 0;
    CHECK(verify(std::move(foreign_bits_setter)) == CheckStatus::mismatch);

    auto missing_ct_setter = balance_document();
    missing_ct_setter["nftables"][4]["rule"]["expr"].erase(
        missing_ct_setter["nftables"][4]["rule"]["expr"].begin() + 1);
    CHECK(verify(std::move(missing_ct_setter)) == CheckStatus::mismatch);

    auto wrong_ct_value = balance_document();
    wrong_ct_value["nftables"][4]["rule"]["expr"][1]["mangle"]
        ["value"]["|"][1] = 0x20000u;
    CHECK(verify(std::move(wrong_ct_value)) == CheckStatus::mismatch);

    auto wrong_ct_mask = balance_document();
    wrong_ct_mask["nftables"][4]["rule"]["expr"][1]["mangle"]
        ["value"]["|"][0]["&"][1] = 0;
    CHECK(verify(std::move(wrong_ct_mask)) == CheckStatus::mismatch);

    auto swapped_sources = balance_document();
    swapped_sources["nftables"][4]["rule"]["expr"][0]["mangle"]
        ["value"]["|"][0]["&"][0] = {{"ct", {{"key", "mark"}}}};
    swapped_sources["nftables"][4]["rule"]["expr"][1]["mangle"]
        ["value"]["|"][0]["&"][0] = {{"meta", {{"key", "mark"}}}};
    CHECK(verify(std::move(swapped_sources)) == CheckStatus::mismatch);

    auto trailing_unmasked_overwrite = balance_document();
    trailing_unmasked_overwrite["nftables"][4]["rule"]["expr"].push_back({
        {"mangle", {{"key", {{"meta", {{"key", "mark"}}}}},
                     {"value", 0x10000u}}}});
    CHECK(verify(std::move(trailing_unmasked_overwrite)) == CheckStatus::mismatch);

    auto duplicate_ct_setter = balance_document();
    duplicate_ct_setter["nftables"][4]["rule"]["expr"].push_back(
        duplicate_ct_setter["nftables"][4]["rule"]["expr"][1]);
    duplicate_ct_setter["nftables"][4]["rule"]["expr"].push_back(
        duplicate_ct_setter["nftables"][4]["rule"]["expr"][1]);
    CHECK(verify(std::move(duplicate_ct_setter)) == CheckStatus::mismatch);
}

TEST_CASE("nft balance mismatch reports the matching physical hook") {
    const auto key = FirewallRuleKey{"route.balance", "output"};
    FirewallPlan plan;
    plan.fwmark_mask = 0x00FF0000u;
    FirewallRuleInstance rule;
    rule.key = key;
    rule.hook = FirewallHook::output;
    rule.family = FirewallFamily::ipv4;
    rule.criteria.apply_output = true;
    rule.criteria.default_gateway = DefaultGatewayFamily::Ipv4;
    rule.action = BalanceAction{
        0x30000u, {{0x10000u, true, false}, {0x20000u, true, false}}};
    FirewallRuleRegistrar registrar(plan);
    registrar.register_rule(std::move(rule));
    registrar.finish();

    FirewallSnapshot snapshot;
    snapshot.available = true;
    snapshot.backend = FirewallBackend::nftables;
    ObservedFirewallRule observed;
    observed.key = key;
    observed.hook = FirewallHook::prerouting;
    observed.family = FirewallFamily::ipv4;
    observed.criteria.default_gateway = DefaultGatewayFamily::Ipv4;
    observed.action = MarkAction{0x10000u, plan.fwmark_mask};
    snapshot.rules.push_back(std::move(observed));

    const auto checks = verify_firewall_plan(plan, snapshot);
    REQUIRE(checks.size() == 1);
    CHECK(checks.front().status == CheckStatus::mismatch);
    CHECK(checks.front().detail.find("hook mismatch") == std::string::npos);
    CHECK(checks.front().detail.find(
              "action mismatch: expected balance got mark") != std::string::npos);
}

TEST_CASE("nft balance snapshot rejects family-ambiguous classifiers") {
    const auto key = FirewallRuleKey{"route.balance", "direct"};
    FirewallPlan plan;
    plan.fwmark_mask = 0x00FF0000u;
    FirewallRuleInstance rule;
    rule.key = key;
    rule.family = FirewallFamily::any;
    rule.action = BalanceAction{
        0x30000u, {{0x10000u, true, true}, {0x20000u, true, true}}};
    FirewallRuleRegistrar registrar(plan);
    registrar.register_rule(std::move(rule));
    registrar.finish();

    auto document = balance_document();
    auto& policy = document["nftables"][6]["rule"];
    policy["comment"] = key.comment();
    policy["expr"].erase(policy["expr"].begin());

    auto v6_policy = policy;
    document["nftables"].push_back({{"rule", std::move(v6_policy)}});

    const auto snapshot = inspect_nftables_snapshot(CommandRunner(
        [&](const std::vector<std::string>&) {
            return command_result(document.dump());
        }));
    REQUIRE(snapshot.available);
    REQUIRE(snapshot.rules.size() == 2);
    const auto checks = verify_firewall_plan(plan, snapshot);
    REQUIRE(checks.size() == 1);
    CHECK(checks.front().status == CheckStatus::mismatch);
}

TEST_CASE("prefilter health canonicalization detects missing mismatch and extra") {
    const std::vector<std::pair<const char*, FirewallRuleAction>> cases = {
        {"prefilter.restore_conntrack_mark", RestoreConntrackMarkAction{0x00FF0000u}},
        {"prefilter.skip_established_or_dnat", SkipEstablishedOrDnatAction{}},
        {"prefilter.skip_marked_packets", SkipMarkedPacketsAction{}},
        {"prefilter.inbound_interface", InboundInterfaceFilterAction{{"br-lan"}}},
    };

    for (const auto& [module_id, action] : cases) {
        const FirewallRuleKey key{module_id, "one"};
        const auto plan = prefilter_plan(key, action);
        REQUIRE(plan.rules.size() == 1);
        const auto& expected = plan.rules.front();
        const auto hooks = nft_prefilter_hooks(expected.action);

        FirewallSnapshot active;
        active.backend = FirewallBackend::nftables;
        active.available = true;
        for (const auto hook : hooks) {
            active.rules.push_back(observed_prefilter(expected, hook));
        }
        auto checks = verify_firewall_plan(plan, active);
        REQUIRE(checks.size() == 1);
        CHECK(checks.front().status == CheckStatus::ok);

        auto missing = active;
        missing.rules.pop_back();
        checks = verify_firewall_plan(plan, missing);
        REQUIRE(checks.size() == 1);
        CHECK(checks.front().status ==
              (hooks.size() == 1U ? CheckStatus::missing : CheckStatus::mismatch));

        auto mismatch = active;
        if (auto* restore =
                std::get_if<RestoreConntrackMarkAction>(&mismatch.rules.front().action)) {
            restore->mask = 0xFFFFFFFFu;
        } else if (auto* inbound =
                       std::get_if<InboundInterfaceFilterAction>(
                           &mismatch.rules.front().action)) {
            inbound->interfaces = {"wan0"};
        } else {
            mismatch.rules.front().action =
                std::holds_alternative<SkipMarkedPacketsAction>(expected.action)
                    ? FirewallRuleAction{SkipEstablishedOrDnatAction{}}
                    : FirewallRuleAction{SkipMarkedPacketsAction{}};
        }
        checks = verify_firewall_plan(plan, mismatch);
        REQUIRE(checks.size() == 1);
        CHECK(checks.front().status == CheckStatus::mismatch);

        auto extra = active;
        auto extra_rule = extra.rules.front();
        extra_rule.key = FirewallRuleKey{module_id, "extra"};
        extra.rules.push_back(std::move(extra_rule));
        checks = verify_firewall_plan(plan, extra);
        REQUIRE(checks.size() == 2);
        CHECK(checks.front().status == CheckStatus::ok);
        CHECK(checks.back().status == CheckStatus::mismatch);
        CHECK(checks.back().detail.find("extra owned") != std::string::npos);
    }
}

TEST_CASE("multi-interface inbound is empty when no iptables route rule materializes") {
    const FirewallRuleKey inbound_key{"prefilter.inbound_interface", "one"};
    auto plan = prefilter_plan(
        inbound_key, InboundInterfaceFilterAction{{"br-lan", "wg0"}});
    FirewallSnapshot empty;
    empty.backend = FirewallBackend::iptables;
    empty.available = true;
    auto checks = verify_firewall_plan(plan, empty);
    REQUIRE(checks.size() == 1);
    CHECK(checks.front().status == CheckStatus::ok);

    FirewallSnapshot with_route = empty;
    const FirewallRuleKey route_key{"route.mark", "one"};
    FirewallRuleInstance route;
    route.key = route_key;
    route.family = FirewallFamily::ipv4;
    route.criteria.dst_addr = {"192.0.2.0/24"};
    route.action = MarkAction{0x10000u, 0x00FF0000u};
    route.source_rule_index = 0;
    FirewallRuleRegistrar registrar(plan);
    registrar.register_rule(std::move(route));
    registrar.finish();
    for (const auto& interface : {"br-lan", "wg0"}) {
        ObservedFirewallRule observed_route;
        observed_route.key = route_key;
        observed_route.family = FirewallFamily::ipv4;
        observed_route.criteria.dst_addr = {"192.0.2.0/24"};
        observed_route.action = MarkAction{0x10000u, 0x00FF0000u};
        observed_route.raw = "-A KeenPbrTable_A -i " + std::string(interface) +
                             " -d 192.0.2.0/24";
        with_route.rules.push_back(std::move(observed_route));
    }
    checks = verify_firewall_plan(plan, with_route);
    REQUIRE(checks.size() == 2);
    CHECK(checks[0].status == CheckStatus::ok);
    CHECK(checks[1].status == CheckStatus::ok);

    with_route.rules.pop_back();
    checks = verify_firewall_plan(plan, with_route);
    REQUIRE(checks.size() == 2);
    CHECK(checks[0].status == CheckStatus::missing);
    CHECK(checks[1].status == CheckStatus::mismatch);
}

TEST_CASE("firewall plan verification distinguishes missing mismatch duplicate and extra") {
    const auto key = FirewallRuleKey{"route.mark", "one"};
    const auto plan = mark_plan(key);

    FirewallSnapshot missing;
    missing.available = true;
    missing.backend = FirewallBackend::nftables;
    auto checks = verify_firewall_plan(plan, missing);
    REQUIRE(checks.size() == 1);
    CHECK(checks[0].status == CheckStatus::missing);

    FirewallSnapshot mismatch = missing;
    ObservedFirewallRule wrong;
    wrong.key = key;
    wrong.family = FirewallFamily::ipv4;
    wrong.criteria.dst_set_name = "kpbr4_list";
    wrong.action = MarkAction{0x20000u, 0xFFFFFFFFu};
    mismatch.rules.push_back(wrong);
    checks = verify_firewall_plan(plan, mismatch);
    CHECK(checks[0].status == CheckStatus::mismatch);
    CHECK(checks[0].detail.find("key=route.mark:one") != std::string::npos);
    CHECK(checks[0].detail.find("action mismatch") != std::string::npos);

    FirewallSnapshot duplicate = mismatch;
    duplicate.rules[0].action = MarkAction{0x10000u, 0xFFFFFFFFu};
    duplicate.rules.push_back(duplicate.rules[0]);
    checks = verify_firewall_plan(plan, duplicate);
    CHECK(checks[0].status == CheckStatus::mismatch);
    CHECK(checks[0].detail.find("duplicate") != std::string::npos);

    FirewallSnapshot extra = missing;
    extra.rules.push_back(duplicate.rules[0]);
    extra.rules.back().key = FirewallRuleKey{"route.mark", "extra"};
    checks = verify_firewall_plan(plan, extra);
    REQUIRE(checks.size() == 2);
    CHECK(checks.back().detail.find("extra owned") != std::string::npos);
}

TEST_CASE("foreign, unknown-version and no-comment rules obey ownership fallback") {
    const auto key = FirewallRuleKey{"route.mark", "one"};
    const auto plan = mark_plan(key);
    FirewallSnapshot snapshot;
    snapshot.available = true;
    snapshot.backend = FirewallBackend::nftables;

    ObservedFirewallRule foreign;
    foreign.key = FirewallRuleKey{"foreign", "one"};
    foreign.family = FirewallFamily::ipv4;
    foreign.criteria.dst_set_name = "kpbr4_list";
    foreign.action = MarkAction{0x10000u, 0xFFFFFFFFu};
    snapshot.rules.push_back(foreign);
    auto checks = verify_firewall_plan(plan, snapshot);
    REQUIRE(checks.size() == 1);
    CHECK(checks[0].status == CheckStatus::missing);

    snapshot.rules.clear();
    ObservedFirewallRule unknown = foreign;
    unknown.key.reset();
    unknown.comment = "kpbr:v2:route.mark:one";
    snapshot.rules.push_back(unknown);
    checks = verify_firewall_plan(plan, snapshot);
    CHECK(checks.size() == 1);
    CHECK(checks[0].status == CheckStatus::missing);

    snapshot.rules.clear();
    ObservedFirewallRule legacy = unknown;
    legacy.comment.reset();
    legacy.legacy = true;
    snapshot.rules.push_back(legacy);
    checks = verify_firewall_plan(plan, snapshot);
    CHECK(checks[0].status == CheckStatus::ok);
}

} // namespace keen_pbr3
