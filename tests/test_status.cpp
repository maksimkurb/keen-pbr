#include <doctest/doctest.h>

#include "../src/cmd/status.hpp"

#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <utility>

using namespace keen_pbr3;

namespace {

nlohmann::json health_response(const std::string& backend,
                               bool chain_present,
                               bool hook_present,
                               nlohmann::json rules = nlohmann::json::array(),
                               std::string detail = "ok") {
    bool rules_ok = true;
    for (const auto& rule : rules) {
        if (rule.value("status", "missing") != "ok") rules_ok = false;
    }
    return {
        {"overall", chain_present && hook_present && rules_ok ? "ok" : "degraded"},
        {"firewall_backend", backend},
        {"firewall", {{"chain_present", chain_present},
                       {"prerouting_hook_present", hook_present},
                       {"detail", std::move(detail)}}},
        {"firewall_rules", std::move(rules)},
        {"route_tables", nlohmann::json::array()},
        {"policy_rules", nlohmann::json::array()},
    };
}

std::pair<int, std::string> render(const nlohmann::json& health) {
    std::ostringstream output;
    auto* const previous = std::cout.rdbuf(output.rdbuf());
    const int result = run_status_command(Config{}, "/tmp/status-test.json", health);
    std::cout.rdbuf(previous);
    return {result, output.str()};
}

} // namespace

TEST_CASE("status renders daemon canonical direct-rule mismatch") {
    const auto [result, output] = render(health_response(
        "nftables", true, true,
        {{{"set_name", "<direct>"},
          {"action", "mark"},
          {"expected_fwmark", "0x00010000"},
          {"status", "mismatch"},
          {"detail", "key=route.mark:direct criteria mismatch"}}}));

    CHECK(result == 1);
    CHECK(output.find("MISMATCH") != std::string::npos);
    CHECK(output.find("key=route.mark:direct criteria mismatch") != std::string::npos);
}

TEST_CASE("status preserves a healthy canonical nft report") {
    const auto [result, output] = render(health_response(
        "nftables", true, true,
        {{{"set_name", "kpbr4_balance"},
          {"action", "mark"},
          {"expected_fwmark", "0x00010000"},
          {"actual_fwmark", "0x00010000"},
          {"status", "ok"}}}));

    CHECK(result == 0);
    CHECK(output.find("Firewall backend: nftables") != std::string::npos);
    CHECK(output.find("Overall: OK") != std::string::npos);
}

TEST_CASE("status does not downgrade a missing output rule half to healthy") {
    const auto [result, output] = render(health_response(
        "nftables", true, true,
        {{{"set_name", "<direct>"},
          {"action", "mark"},
          {"status", "missing"},
          {"detail", "key=route.mark:output OUTPUT half is missing"}}}));

    CHECK(result == 1);
    CHECK(output.find("MISSING") != std::string::npos);
    CHECK(output.find("OUTPUT half is missing") != std::string::npos);
}

TEST_CASE("status reports a cold canonical health refresh as unavailable") {
    const auto [result, output] = render(health_response(
        "iptables", false, false, nlohmann::json::array(),
        "canonical routing health refresh is pending"));

    CHECK(result == 1);
    CHECK(output.find("UNAVAILABLE") != std::string::npos);
    CHECK(output.find("refresh is pending") != std::string::npos);
}

TEST_CASE("status reports unavailable when legacy daemon omits health") {
    std::ostringstream output;
    auto* const previous = std::cout.rdbuf(output.rdbuf());
    const int result = run_status_command(
        Config{}, "/tmp/status-test.json", nullptr);
    std::cout.rdbuf(previous);

    CHECK(result == 1);
    CHECK(output.str().find("UNAVAILABLE") != std::string::npos);
    CHECK(output.str().find("active firewall plan unavailable") != std::string::npos);
}
