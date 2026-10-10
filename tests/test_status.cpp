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
                               std::string detail = "ok",
                               std::string verification_state = "") {
    if (verification_state.empty()) {
        verification_state = chain_present && hook_present ? "verified" : "failed";
    }
    bool rules_ok = true;
    for (const auto& rule : rules) {
        if (rule.value("status", "missing") != "ok") rules_ok = false;
    }
    return {
        {"overall", chain_present && hook_present && rules_ok ? "ok" : "degraded"},
        {"firewall_backend", backend},
        {"firewall", {{"chain_present", chain_present},
                       {"prerouting_hook_present", hook_present},
                       {"detail", std::move(detail)},
                       {"verification_state", verification_state}}},
        {"firewall_rules", std::move(rules)},
        {"route_tables", nlohmann::json::array()},
        {"policy_rules", nlohmann::json::array()},
    };
}

// Restores std::cout even when the command throws; a dangling rdbuf would
// crash the test reporter.
struct CoutCapture {
    std::ostringstream output;
    std::streambuf* previous{std::cout.rdbuf(output.rdbuf())};
    ~CoutCapture() { std::cout.rdbuf(previous); }
};

std::pair<int, std::string> render(const nlohmann::json& health) {
    CoutCapture capture;
    const int result = run_status_command(Config{}, "/tmp/status-test.json", health);
    return {result, capture.output.str()};
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
        "canonical routing health refresh is pending", "unavailable"));

    CHECK(result == 1);
    CHECK(output.find("UNAVAILABLE") != std::string::npos);
    CHECK(output.find("refresh is pending") != std::string::npos);
}

TEST_CASE("status reports a config apply in progress as unavailable, not failed") {
    const auto [result, output] = render(health_response(
        "iptables", false, false, nlohmann::json::array(),
        "routing runtime configuration is being applied", "unavailable"));

    CHECK(output.find("UNAVAILABLE") != std::string::npos);
    CHECK(output.find("ERROR") == std::string::npos);
}

TEST_CASE("status reports unavailable when legacy daemon omits health") {
    CoutCapture capture;
    const int result = run_status_command(
        Config{}, "/tmp/status-test.json", nullptr);
    const std::string output = capture.output.str();

    CHECK(result == 1);
    CHECK(output.find("UNAVAILABLE") != std::string::npos);
    CHECK(output.find("active firewall plan unavailable") != std::string::npos);
}
