#include <doctest/doctest.h>

#include "../src/firewall/iptables_verifier.hpp"
#include "../src/firewall/nftables_verifier.hpp"

#include <netinet/in.h>

using namespace keen_pbr3;

TEST_CASE("iptables parser recognizes owned chain and mark rule") {
    const auto state = parse_iptables_s(
        "-N KeenPbrTable\n"
        "-A PREROUTING -j KeenPbrTable\n"
        "-A KeenPbrTable -m set --match-set kpbr4_web dst -p tcp "
        "--dport 443 -m comment --comment kpbr:v1:route.mark:web "
        "-j MARK --set-xmark 0x10000/0xffff0000\n");

    REQUIRE(state.has_keen_pbr_chain);
    REQUIRE(state.has_prerouting_jump);
    REQUIRE(state.rules.size() == 1);
    CHECK(state.rules.front().set_name == "kpbr4_web");
    CHECK(state.rules.front().is_mark);
    CHECK(state.rules.front().fwmark == 0x10000u);
    CHECK(state.rules.front().xmark_mask == 0xffff0000u);
    CHECK(state.rules.front().comment == "kpbr:v1:route.mark:web");
    CHECK(state.rules.front().criteria.proto == L4Proto::Tcp);
}

TEST_CASE("iptables parser keeps rule order and action kind") {
    const auto state = parse_iptables_s(
        "-N KeenPbrTable\n"
        "-A KeenPbrTable -m set --match-set allow dst -j RETURN\n"
        "-A KeenPbrTable -m set --match-set deny dst -j DROP\n");

    REQUIRE(state.rules.size() == 2);
    CHECK(state.rules[0].set_name == "allow");
    CHECK(state.rules[0].is_pass);
    CHECK(state.rules[0].order == 1);
    CHECK(state.rules[1].set_name == "deny");
    CHECK(state.rules[1].is_drop);
    CHECK(state.rules[1].order == 2);
}

TEST_CASE("iptables family parser records output ownership") {
    const auto state = parse_iptables_s_family(
        "-N KeenPbrTable\n"
        "-N KeenPbrOutput\n"
        "-A OUTPUT -j KeenPbrOutput\n"
        "-A KeenPbrOutput -m set --match-set kpbr4_web dst -j RETURN\n",
        false);

    CHECK(state.has_keen_pbr_chain);
    CHECK(state.has_output_chain);
    CHECK(state.has_output_jump);
    REQUIRE(state.rules.size() == 1);
    CHECK(state.rules.front().hook == FirewallHook::output);
}

TEST_CASE("iptables parser ignores empty and foreign input") {
    CHECK(parse_iptables_s("").rules.empty());
    const auto state = parse_iptables_s(
        "-N Foreign\n"
        "-A Foreign -j ACCEPT\n");
    CHECK_FALSE(state.has_keen_pbr_chain);
    CHECK(state.rules.empty());
}

TEST_CASE("ipset parser keeps reserved schemas and ignores foreign sets") {
    const auto sets = parse_ipset_save(
        "create kpbr4_web hash:net family inet hashsize 1024\n"
        "create kpbr6d_dns hash:net family inet6 timeout 300\n"
        "create foreign hash:net family inet timeout 5\n");

    REQUIRE(sets.size() == 2);
    CHECK(sets[0].name == "kpbr4_web");
    CHECK(sets[0].family == AF_INET);
    CHECK(sets[1].name == "kpbr6d_dns");
    CHECK(sets[1].family == AF_INET6);
    CHECK(sets[1].timeout_seconds == 300);
}

TEST_CASE("nft parser recognizes table chain and hook") {
    const auto state = parse_nft_json(R"({"nftables":[
      {"table":{"family":"inet","name":"KeenPbrTable"}},
      {"chain":{"family":"inet","table":"KeenPbrTable","name":"prerouting",
                  "type":"filter","hook":"prerouting"}}
    ]})");

    CHECK(state.has_table);
    CHECK(state.has_prerouting_chain);
    CHECK(state.has_prerouting_hook);
}

TEST_CASE("nft parser recognizes set match and mark action") {
    const auto state = parse_nft_json(R"({"nftables":[
      {"chain":{"family":"inet","table":"KeenPbrTable","name":"prerouting",
                  "type":"filter","hook":"prerouting"}},
      {"set":{"family":"inet","table":"KeenPbrTable","name":"kpbr4_web",
               "type":"ipv4_addr"}},
      {"rule":{"family":"inet","table":"KeenPbrTable","chain":"prerouting",
        "expr":[
          {"match":{"op":"==","left":{"payload":{"protocol":"ip","field":"daddr"}},
                     "right":"@kpbr4_web"}},
          {"mangle":{"key":{"meta":{"key":"mark"}},"value":65536}},
          {"accept":null}
        ]}}
    ]})");

    REQUIRE(state.sets.size() == 1);
    CHECK(state.sets.front().name == "kpbr4_web");
    REQUIRE(state.rules.size() == 1);
    CHECK(state.rules.front().set_name == "kpbr4_web");
    CHECK(state.rules.front().is_mark);
    CHECK(state.rules.front().is_pass);
    CHECK(state.rules.front().fwmark == 65536u);
}

TEST_CASE("nft parser recognizes drop and invalid input") {
    const auto state = parse_nft_json(R"({"nftables":[
      {"rule":{"family":"inet","table":"KeenPbrTable","chain":"prerouting",
        "expr":[
          {"match":{"op":"==","left":{"payload":{"protocol":"ip","field":"daddr"}},
                     "right":"@kpbr4_deny"}},
          {"drop":null}
        ]}}
    ]})");

    REQUIRE(state.rules.size() == 1);
    CHECK(state.rules.front().is_drop);
    CHECK_FALSE(state.rules.front().is_mark);
    CHECK_FALSE(parse_nft_json("not json").has_table);
}
