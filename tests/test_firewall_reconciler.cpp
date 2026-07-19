#include <doctest/doctest.h>

#include "../src/firewall/firewall_reconciler.hpp"
#include "../src/firewall/iptables_verifier.hpp"
#include "../src/firewall/nftables_verifier.hpp"

#include <netinet/in.h>
#include <stdexcept>

namespace keen_pbr3 {
namespace {

class FakeFirewallBackend final : public FirewallReconcilerBackend {
public:
    bool probe(std::string& error) override { error = probe_error; return probe_ok; }
    FirewallActualState inspect() override { return actual; }
    std::vector<FirewallOperation> plan(const FirewallDesiredState&, const FirewallActualState&) override {
        ++plans;
        return operations;
    }
    bool verify(const FirewallDesiredState&, const FirewallActualState&, std::string& error) override {
        error = verify_error;
        return verify_ok;
    }
    void cleanup() override { ++cleanups; }

    bool probe_ok{true};
    bool verify_ok{true};
    std::string probe_error;
    std::string verify_error;
    FirewallActualState actual;
    std::vector<FirewallOperation> operations;
    int plans{0};
    int cleanups{0};
};

} // namespace

TEST_CASE("FirewallReconciler uses an attempt-scoped plan") {
    FakeFirewallBackend backend;
    int applied = 0;
    backend.operations = {{"apply", [&] { ++applied; throw std::runtime_error("injected"); }}};
    FirewallReconciler reconciler(backend);

    const auto failed = reconciler.reconcile({});
    CHECK_FALSE(failed.committed);
    CHECK(failed.error == "injected");
    CHECK(applied == 1);

    backend.operations = {{"apply", [&] { ++applied; }}};
    const auto recovered = reconciler.reconcile({});
    CHECK(recovered.committed);
    CHECK(applied == 2);
    CHECK(backend.plans == 2);
}

TEST_CASE("FirewallReconciler reports post-apply ordered state drift") {
    FakeFirewallBackend backend;
    backend.verify_ok = false;
    backend.verify_error = "rule order mismatch";
    FirewallReconciler reconciler(backend);

    const auto result = reconciler.reconcile({});
    CHECK_FALSE(result.committed);
    CHECK(result.drift_detected);
    CHECK(result.error == "rule order mismatch");
}

TEST_CASE("Firewall state diff distinguishes ordered rules and set schemas") {
    FirewallDesiredState desired;
    desired.chains = {"KeenPbrTable"};
    desired.jumps = {"PREROUTING->KeenPbrTable"};
    desired.ordered_rules = {"mark vpn", "pass"};
    desired.sets = {{"kpbr4_static", AF_INET, 0, false},
                    {"kpbr4d_dns", AF_INET, 300, true}};

    FirewallActualState actual = desired;
    actual.chains.push_back("KeenPbrTable_stale");
    std::swap(actual.ordered_rules[0], actual.ordered_rules[1]);
    actual.sets[1].timeout_seconds = 60;
    actual.sets.push_back({"kpbr4_extra", AF_INET, 0, false});

    const auto diff = diff_firewall_state(desired, actual);
    CHECK(diff.extra_chains == std::vector<std::string>{"KeenPbrTable_stale"});
    CHECK(diff.rules_reordered);
    CHECK(diff.schema_mismatches == std::vector<std::string>{"kpbr4d_dns"});
    CHECK(diff.extra_sets == std::vector<std::string>{"kpbr4_extra"});
    CHECK(diff.summary().find("rule order mismatch") != std::string::npos);
}

TEST_CASE("Firewall state diff only treats reserved namespace objects as extras") {
    FirewallDesiredState desired;
    FirewallActualState actual;
    actual.chains = {"KeenPbrTable_stale", "KeenPbrTableLikeForeign"};
    actual.sets = {{"kpbr4_stale", AF_INET, 0, false},
                   {"kpbr_foreign", AF_INET, 0, false}};

    const auto diff = diff_firewall_state(desired, actual);
    CHECK(diff.extra_chains == std::vector<std::string>{"KeenPbrTable_stale"});
    CHECK(diff.extra_sets == std::vector<std::string>{"kpbr4_stale"});
    CHECK(is_keen_pbr_namespace_name("ip4:KeenPbrTable_7"));
    CHECK_FALSE(is_keen_pbr_namespace_name("KeenPbrTableLikeForeign"));
}

TEST_CASE("Firewall inspections retain backend rule order and ownership hooks") {
    ParsedIptablesState ipv4 = parse_iptables_s(
        "-N KeenPbrTable\n-A PREROUTING -j KeenPbrTable\n");
    ipv4.rules.push_back({"", {}, false, false, true, false, 0, true, 0xFFFFFFFFu});
    const auto iptables = inspect_iptables_state(ipv4, {});
    CHECK(iptables.chains == std::vector<std::string>{"ip4:KeenPbrTable"});
    CHECK(iptables.jumps == std::vector<std::string>{"ip4:PREROUTING->KeenPbrTable"});
    CHECK(iptables.ordered_rules == std::vector<std::string>{"ip4::drop"});

    const auto nft = inspect_nftables_state(parse_nft_json(R"({"nftables":[
      {"table":{"family":"inet","name":"KeenPbrTable"}},
      {"chain":{"family":"inet","table":"KeenPbrTable","name":"prerouting","type":"filter","hook":"prerouting"}}
    ]})"));
    CHECK(nft.available);
    CHECK(nft.chains == std::vector<std::string>{"inet:KeenPbrTable", "inet:prerouting"});
    CHECK(nft.jumps == std::vector<std::string>{"inet:prerouting-hook"});
}

} // namespace keen_pbr3
