// The inspector reads the kernel into a PhysicalRuleset and nothing else:
// parsing and diffing are covered by test_firewall_physical.cpp and
// test_firewall_verifier.cpp.  These tests pin which commands are run (the
// health check runs them periodically) and how failures surface.

#include <doctest/doctest.h>

#include "../src/firewall/firewall_plan_verifier.hpp"
#include "../src/firewall/firewall_snapshot.hpp"
#include "firewall_fixtures.hpp"

#include <algorithm>

namespace keen_pbr3 {
namespace {

using Calls = std::vector<std::vector<std::string>>;

CommandRunner recording_runner(Calls& calls, std::string output,
                               int exit_code = 0, bool truncated = false) {
  return [&calls, output = std::move(output), exit_code,
          truncated](const std::vector<std::string>& args) {
    calls.push_back(args);
    return CommandResult{output, exit_code, truncated};
  };
}

bool called(const Calls& calls, const std::vector<std::string>& args) {
  return std::find(calls.begin(), calls.end(), args) != calls.end();
}

const std::string kEmptyMangle =
    "-P PREROUTING ACCEPT\n-P OUTPUT ACCEPT\n";

} // namespace

TEST_CASE("iptables snapshot reads only the mangle table of each enabled family") {
  Calls calls;
  const auto snapshot =
      inspect_iptables_snapshot(recording_runner(calls, kEmptyMangle), {}, true);
  CHECK(snapshot.available);
  CHECK(snapshot.backend == FirewallBackend::iptables);
  REQUIRE(calls.size() == 2);
  CHECK(called(calls, {"iptables", "-t", "mangle", "-S"}));
  CHECK(called(calls, {"ip6tables", "-t", "mangle", "-S"}));

  calls.clear();
  CHECK(inspect_iptables_snapshot(recording_runner(calls, kEmptyMangle), {},
                                  false)
            .available);
  REQUIRE(calls.size() == 1);
  CHECK(calls[0] == std::vector<std::string>{"iptables", "-t", "mangle", "-S"});
}

TEST_CASE("iptables snapshot adds the raw table per RAW PREROUTING family") {
  Calls calls;
  CHECK(inspect_iptables_snapshot(recording_runner(calls, kEmptyMangle),
                                  RawPreroutingMode{true, false}, true)
            .available);
  CHECK(calls.size() == 3);
  CHECK(called(calls, {"iptables", "-t", "raw", "-S"}));
  CHECK(called(calls, {"iptables", "-t", "mangle", "-S"}));
  CHECK(called(calls, {"ip6tables", "-t", "mangle", "-S"}));
  CHECK_FALSE(called(calls, {"ip6tables", "-t", "raw", "-S"}));

  calls.clear();
  CHECK(inspect_iptables_snapshot(recording_runner(calls, kEmptyMangle),
                                  RawPreroutingMode{true, true}, true)
            .available);
  CHECK(calls.size() == 4);
}

TEST_CASE("iptables snapshot parses the real kernel dump into chains") {
  Calls calls;
  const auto snapshot = inspect_iptables_snapshot(
      recording_runner(calls, read_fixture("iptables_mangle_v4.rules")), {},
      false);
  REQUIRE(snapshot.available);
  const auto* generation = snapshot.ruleset.find(
      {PhysicalChainRole::prerouting_generation, PhysicalTable::mangle,
       FirewallFamily::ipv4, PhysicalGeneration::a, 0, "KeenPbrTable_A"});
  REQUIRE(generation != nullptr);
  CHECK(generation->rules.size() > 20);
}

TEST_CASE("iptables snapshot reports unreadable or truncated output") {
  for (const auto& [exit_code, truncated] :
       {std::pair<int, bool>{1, false}, std::pair<int, bool>{0, true}}) {
    Calls calls;
    const auto snapshot = inspect_iptables_snapshot(
        recording_runner(calls, "-N KeenPbrTable\n", exit_code, truncated), {},
        true);
    CHECK_FALSE(snapshot.available);
    CHECK(snapshot.error == "failed to inspect iptables firewall state");
    CHECK(snapshot.ruleset.chains.empty());
  }
}

TEST_CASE("nft snapshot reads the table once, without set elements") {
  Calls calls;
  const auto snapshot = inspect_nftables_snapshot(
      recording_runner(calls, read_fixture("nft_balance.json")));
  CHECK(snapshot.available);
  CHECK(snapshot.backend == FirewallBackend::nftables);
  REQUIRE(calls.size() == 1);
  CHECK(calls[0] == std::vector<std::string>{"nft", "-t", "-j", "list", "table",
                                             "inet", "KeenPbrTable"});
  CHECK(snapshot.ruleset.find(
            {PhysicalChainRole::nft_prerouting, PhysicalTable::nft_inet,
             FirewallFamily::any, PhysicalGeneration::none, 0, "prerouting"}) !=
        nullptr);
}

TEST_CASE("nft snapshot reports failures, bad JSON and an absent table") {
  Calls calls;
  auto snapshot = inspect_nftables_snapshot(recording_runner(calls, "", 1));
  CHECK_FALSE(snapshot.available);
  CHECK(snapshot.error == "failed to inspect nftables firewall state");

  snapshot = inspect_nftables_snapshot(
      recording_runner(calls, read_fixture("nft_balance.json"), 0, true));
  CHECK_FALSE(snapshot.available);

  snapshot = inspect_nftables_snapshot(recording_runner(calls, "not json"));
  CHECK_FALSE(snapshot.available);
  CHECK(snapshot.error.find("failed to parse nftables") == 0);

  snapshot = inspect_nftables_snapshot(
      recording_runner(calls, R"({"nftables":[]})"));
  CHECK_FALSE(snapshot.available);
  CHECK(snapshot.error == "KeenPbrTable table not found in nftables");
}

TEST_CASE("the inspector factory refuses RAW PREROUTING with nftables") {
  CHECK_THROWS_AS(create_firewall_snapshot_inspector(
                      FirewallBackend::nftables, RawPreroutingMode{true, false}),
                  FirewallError);
  Calls calls;
  const auto inspector = create_firewall_snapshot_inspector(
      FirewallBackend::iptables, {}, recording_runner(calls, kEmptyMangle));
  CHECK(inspector->inspect(false).available);
  CHECK(calls.size() == 1);
}

TEST_CASE("IPv6 chains in the expected ruleset enable the IPv6 inspection") {
  PhysicalRuleset expected;
  CHECK_FALSE(firewall_expected_uses_ipv6(expected));
  PhysicalChain v4;
  v4.id.table = PhysicalTable::mangle;
  v4.id.family = FirewallFamily::ipv4;
  expected.chains.push_back(v4);
  CHECK_FALSE(firewall_expected_uses_ipv6(expected));
  PhysicalChain v6 = v4;
  v6.id.family = FirewallFamily::ipv6;
  expected.chains.push_back(v6);
  CHECK(firewall_expected_uses_ipv6(expected));

  // nft chains are family-less however the table is addressed.
  PhysicalRuleset nft;
  PhysicalChain chain;
  chain.id.table = PhysicalTable::nft_inet;
  chain.id.family = FirewallFamily::ipv6;
  nft.chains.push_back(chain);
  CHECK_FALSE(firewall_expected_uses_ipv6(nft));
}

} // namespace keen_pbr3
