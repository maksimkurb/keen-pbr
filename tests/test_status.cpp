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

TEST_CASE("status shows daemon issues, kernel checks and service state") {
    const nlohmann::json state = {
        {"service",
         {{"status", "running"},
          {"runtime_state", "running"},
          {"runtime_state_reason", "startup complete"},
          {"version", "3.4.0"},
          {"build", "1"},
          {"build_variant", "generic"},
          {"os_type", "linux"},
          {"os_version", "1"},
          {"config_is_draft", false},
          {"rollback_available", false}}},
        {"issues",
         {{"issues",
           {{{"key", "outbound-vpn"},
             {"severity", "error"},
             {"code", "outbound_unavailable"},
             {"params", {{"tag", "vpn"}}},
             {"title", "vpn is unavailable"},
             {"detail", "interface down"}},
            {{"key", "host-warning-nat_missing-eth1"},
             {"severity", "warning"},
             {"code", "host_warning"},
             {"params", {{"warning", "nat_missing"}, {"interface", "eth1"}}},
             {"title", "eth1: no NAT (MASQUERADE/SNAT) for this WAN"}}}},
          {"kernel_checks",
           {{{"key", "probe:set_write"},
             {"kind", "probe"},
             {"feature", "set_write"},
             {"status", "ok"},
             {"advisory", false}}}}}}};

    CoutCapture capture;
    const int result = run_status_command(
        Config{}, "/tmp/status-test.json", health_response("nftables", true, true), state);
    const std::string output = capture.output.str();

    CHECK(result == 1);
    CHECK(output.find("Service: running (runtime running: startup complete)") !=
          std::string::npos);
    CHECK(output.find("probe      set_write") != std::string::npos);
    CHECK(output.find("ERROR  vpn is unavailable") != std::string::npos);
    CHECK(output.find("interface down") != std::string::npos);
    CHECK(output.find("WARN   eth1: no NAT") != std::string::npos);
    CHECK(output.find("Overall: DEGRADED (1 error(s), 1 warning(s))") != std::string::npos);
}

TEST_CASE("status --json prints the REST bodies and keeps the exit code") {
    const nlohmann::json state = {
        {"runtime_generation", 7},
        {"issues",
         {{"issues",
           {{{"key", "outbound-vpn"},
             {"severity", "error"},
             {"code", "outbound_unavailable"},
             {"params", {{"tag", "vpn"}}},
             {"title", "vpn is unavailable"}}}},
          {"kernel_checks", nlohmann::json::array()}}}};
    const auto health = health_response("nftables", true, true);

    int result = 0;
    std::string output;
    {
        CoutCapture capture;
        result = run_status_json_command(Config{}, health, state);
        output = capture.output.str();
    }
    const auto printed = nlohmann::json::parse(output);

    CHECK(result == 1);
    CHECK(printed.at("routing_health") == health);
    CHECK(printed.at("issues") == state.at("issues"));
    // Only members that have a REST equivalent are printed.
    CHECK_FALSE(printed.contains("runtime_generation"));
    CHECK_FALSE(printed.contains("service"));
    CHECK(output.find("Overall:") == std::string::npos);

    int healthy = 1;
    {
        CoutCapture capture;
        healthy = run_status_json_command(Config{}, health, nlohmann::json::object());
    }
    CHECK(healthy == 0);
}
