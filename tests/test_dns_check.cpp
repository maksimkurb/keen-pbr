#include <doctest/doctest.h>

#include "../src/dns/dns_check.hpp"
#include "../src/dns/dns_client.hpp"
#include "fake_dns_server.hpp"

#include <regex>

using namespace keen_pbr3;

namespace {

using Bytes = std::vector<std::uint8_t>;

DnsServer make_server(const std::string& tag, const std::string& address,
                      api::DnsServerType type = api::DnsServerType::STATIC,
                      std::optional<std::string> detour = std::nullopt) {
    DnsServer server;
    server.tag = tag;
    server.type = type;
    if (type != api::DnsServerType::KEENETIC) server.address = address;
    server.detour = std::move(detour);
    return server;
}

// The name asked in a query message.
std::string qname_of(const Bytes& query) {
    std::string name;
    std::size_t pos = 12;
    while (pos < query.size() && query[pos] != 0) {
        if (!name.empty()) name += '.';
        name.append(reinterpret_cast<const char*>(&query[pos + 1]), query[pos]);
        pos += 1u + query[pos];
    }
    return name;
}

DnsProbeResult answered(const Bytes& query, std::uint8_t rcode, const std::vector<fake_dns::Rr>& records = {},
                        DnsUpstreamOutcome outcome = DnsUpstreamOutcome::Ok) {
    DnsProbeResult result;
    result.outcome = outcome;
    result.answer = fake_dns::make_reply(query, rcode, records);
    result.duration = std::chrono::milliseconds(12);
    return result;
}

DnsProbeResult failed(DnsUpstreamOutcome outcome, const std::string& error = "") {
    DnsProbeResult result;
    result.outcome = outcome;
    result.error = error;
    return result;
}

Bytes query_for(const std::string& name) {
    return build_dns_query(1, name, kDnsTypeA, kDnsClassIN, true);
}

DnsCheckResult classify(const std::string& domain, bool custom, const DnsProbeResult& probe) {
    return classify_dns_check_answer(domain, custom, probe);
}

} // namespace

TEST_CASE("dns check request: defaults, limits and validation") {
    const auto none = parse_dns_check_request(nlohmann::json::object());
    CHECK(none.domains == std::vector<std::string>{"yandex.ru"});
    CHECK(none.custom_domains.empty());
    CHECK(none.servers.empty());

    const auto custom_only = parse_dns_check_request({{"custom_domains", {"Example.ORG"}}});
    CHECK(custom_only.domains.empty());
    CHECK(custom_only.custom_domains == std::vector<std::string>{"example.org"});

    const auto both = parse_dns_check_request(
        {{"domains", {"yandex.ru", "yandex.ru", "youtube.com"}},
         {"custom_domains", {"youtube.com", "example.org"}},
         {"servers", {"a"}}});
    CHECK(both.domains == std::vector<std::string>{"yandex.ru", "youtube.com"});
    CHECK(both.custom_domains == std::vector<std::string>{"example.org"});
    CHECK(both.servers == std::vector<std::string>{"a"});

    CHECK_THROWS_AS(parse_dns_check_request({{"domains", {"a.ru", "b.ru", "c.ru", "d.ru"}},
                                             {"custom_domains", {"e.ru", "f.ru"}}}),
                    DnsCheckRequestError);
    CHECK_THROWS_AS(parse_dns_check_request({{"domains", {"not a domain"}}}), DnsCheckRequestError);
    CHECK_THROWS_AS(parse_dns_check_request({{"domains", "yandex.ru"}}), DnsCheckRequestError);
    CHECK_THROWS_AS(parse_dns_check_request({{"domains", nlohmann::json::array()}}), DnsCheckRequestError);
    CHECK_THROWS_AS(parse_dns_check_request(nlohmann::json::array()), DnsCheckRequestError);
}

TEST_CASE("dns check: which names are valid domains") {
    CHECK(is_valid_dns_check_domain("yandex.ru"));
    CHECK(is_valid_dns_check_domain("xn--80ak6aa92e.com"));
    CHECK(is_valid_dns_check_domain("localhost"));
    CHECK_FALSE(is_valid_dns_check_domain(""));
    CHECK_FALSE(is_valid_dns_check_domain("a..b"));
    CHECK_FALSE(is_valid_dns_check_domain("-a.com"));
    CHECK_FALSE(is_valid_dns_check_domain("a-.com"));
    CHECK_FALSE(is_valid_dns_check_domain("example.com."));
    CHECK_FALSE(is_valid_dns_check_domain("exa mple.com"));
    CHECK_FALSE(is_valid_dns_check_domain("a_b.com"));
    CHECK_FALSE(is_valid_dns_check_domain(std::string(64, 'a') + ".com"));
}

TEST_CASE("dns check: which addresses are stubs") {
    const auto v4 = [](std::uint8_t a, std::uint8_t b, std::uint8_t c, std::uint8_t d, bool strict = false) {
        const std::uint8_t bytes[4] = {a, b, c, d};
        return is_dns_check_stub_address(4, bytes, strict);
    };
    CHECK(v4(0, 0, 0, 0));
    CHECK(v4(127, 0, 0, 1));
    CHECK(v4(10, 1, 2, 3));
    CHECK(v4(172, 16, 0, 1));
    CHECK(v4(172, 31, 255, 1));
    CHECK(v4(192, 168, 1, 1));
    CHECK(v4(169, 254, 1, 1));
    CHECK(v4(100, 64, 0, 1));
    CHECK(v4(100, 127, 0, 1));
    CHECK_FALSE(v4(172, 32, 0, 1));
    CHECK_FALSE(v4(100, 128, 0, 1));
    CHECK_FALSE(v4(77, 88, 55, 60));
    CHECK_FALSE(v4(8, 8, 8, 8));
    // For a domain of the user only the addresses that mean "blocked" count.
    CHECK(v4(0, 0, 0, 0, true));
    CHECK(v4(127, 0, 0, 1, true));
    CHECK_FALSE(v4(192, 168, 1, 1, true));

    const auto v6 = [](const char* text, bool strict = false) {
        std::uint8_t bytes[16] = {};
        REQUIRE(::inet_pton(AF_INET6, text, bytes) == 1);
        return is_dns_check_stub_address(6, bytes, strict);
    };
    CHECK(v6("::"));
    CHECK(v6("::1"));
    CHECK(v6("fc00::1"));
    CHECK(v6("fd12:3456::1"));
    CHECK(v6("fe80::1"));
    CHECK_FALSE(v6("2a00:1450:4010:c05::64"));
    CHECK_FALSE(v6("::2"));
    CHECK(v6("::1", true));
    CHECK_FALSE(v6("fd12::1", true));
}

TEST_CASE("dns check: answers are told apart") {
    const Bytes q = query_for("youtube.com");

    auto ok = classify("youtube.com", false, answered(q, 0, {fake_dns::a_rr("142.250.74.14")}));
    CHECK(ok.status == "ok");
    CHECK(ok.severity == DnsCheckSeverity::Ok);
    CHECK(ok.rcode == 0);
    CHECK(ok.addresses == std::vector<std::string>{"142.250.74.14"});
    CHECK(ok.duration_ms == doctest::Approx(12.0));

    auto nxdomain = classify("youtube.com", false, answered(q, 3, {}, DnsUpstreamOutcome::Nxdomain));
    CHECK(nxdomain.status == "nxdomain");
    CHECK(nxdomain.severity == DnsCheckSeverity::Warning);
    // A domain of the user may well not exist.
    auto custom_nx = classify("mine.example", true, answered(q, 3, {}, DnsUpstreamOutcome::Nxdomain));
    CHECK(custom_nx.status == "nxdomain");
    CHECK(custom_nx.severity == DnsCheckSeverity::Ok);

    auto nodata = classify("youtube.com", false, answered(q, 0));
    CHECK(nodata.status == "nodata");
    CHECK(nodata.severity == DnsCheckSeverity::Warning);
    CHECK(classify("mine.example", true, answered(q, 0)).severity == DnsCheckSeverity::Ok);

    auto blocked = classify("youtube.com", false, answered(q, 0, {fake_dns::a_rr("0.0.0.0")}));
    CHECK(blocked.status == "stub_address");
    CHECK(blocked.severity == DnsCheckSeverity::Warning);
    CHECK(classify("youtube.com", false, answered(q, 0, {fake_dns::a_rr("1.2.3.4"), fake_dns::a_rr("10.0.0.1")}))
              .status == "stub_address");
    CHECK(classify("youtube.com", false, answered(q, 0, {fake_dns::aaaa_rr("::")})).status == "stub_address");
    // A private address of a domain of the user is fine, a block page address is not.
    CHECK(classify("nas.example", true, answered(q, 0, {fake_dns::a_rr("192.168.1.5")})).status == "ok");
    CHECK(classify("nas.example", true, answered(q, 0, {fake_dns::a_rr("0.0.0.0")})).status ==
          "stub_address");

    CHECK(classify("youtube.com", false, answered(q, 2, {}, DnsUpstreamOutcome::Servfail)).status == "servfail");
    CHECK(classify("youtube.com", false, answered(q, 5, {}, DnsUpstreamOutcome::Refused)).status == "refused");
    const auto other = classify("youtube.com", false, answered(q, 4, {}, DnsUpstreamOutcome::OtherRcode));
    CHECK(other.status == "rcode");
    CHECK(other.severity == DnsCheckSeverity::Error);
    CHECK(other.rcode == 4);
}

TEST_CASE("dns check: a reply that never came is an error with its reason") {
    const std::vector<std::pair<DnsUpstreamOutcome, const char*>> cases{
        {DnsUpstreamOutcome::Timeout, "timeout"},
        {DnsUpstreamOutcome::ConnectionRefused, "connection_refused"},
        {DnsUpstreamOutcome::Unreachable, "unreachable"},
        {DnsUpstreamOutcome::ConnectionClosed, "connection_closed"},
        {DnsUpstreamOutcome::TlsError, "tls_error"},
        {DnsUpstreamOutcome::HttpError, "http_error"},
        {DnsUpstreamOutcome::BadAnswer, "bad_answer"},
        {DnsUpstreamOutcome::NotResolved, "not_resolved"},
        {DnsUpstreamOutcome::NetworkError, "network_error"},
    };
    for (const auto& item : cases) {
        const auto outcome = item.first;
        const std::string name = item.second;
        CAPTURE(name);
        const auto result = classify("yandex.ru", false, failed(outcome, "boom"));
        CHECK(result.status == name);
        CHECK(result.severity == DnsCheckSeverity::Error);
        CHECK(result.error == std::optional<std::string>("boom"));
        CHECK_FALSE(result.rcode.has_value());
    }
    // An "answer" that is not a DNS message.
    DnsProbeResult garbage;
    garbage.outcome = DnsUpstreamOutcome::Ok;
    garbage.answer = {1, 2, 3};
    CHECK(classify("yandex.ru", false, garbage).status == "bad_answer");
}

TEST_CASE("dns check: a made-up name that resolves means tampered NXDOMAIN answers") {
    const Bytes q = query_for("0123456789abcdef.nxcheck.keen-pbr.fyi");
    CHECK(dns_check_nxdomain_hijacked(answered(q, 3, {}, DnsUpstreamOutcome::Nxdomain)) == std::optional<bool>(false));
    CHECK(dns_check_nxdomain_hijacked(answered(q, 0)) == std::optional<bool>(false));
    CHECK(dns_check_nxdomain_hijacked(answered(q, 0, {fake_dns::a_rr("203.0.113.9")})) == std::optional<bool>(true));
    CHECK_FALSE(dns_check_nxdomain_hijacked(failed(DnsUpstreamOutcome::Timeout)).has_value());
}

namespace {

// A probe that answers by server address and domain through `script`.
using Script = std::function<DnsProbeResult(const DnsBalancerUpstream&, const std::string& name, const Bytes& query)>;

DnsCheckProbeFn probe_with(Script script, std::vector<std::string>* seen = nullptr) {
    return [script, seen](const std::vector<DnsProbeRequest>& requests, std::chrono::milliseconds,
                          const DnsBalancerSettings&) {
        std::vector<DnsProbeResult> results;
        for (const auto& request : requests) {
            const std::string name = qname_of(request.query);
            if (seen != nullptr) seen->push_back(name);
            results.push_back(script(request.upstream, name, request.query));
        }
        return results;
    };
}

DnsCheckInput base_input() {
    DnsCheckInput input;
    input.request.domains = {"yandex.ru", "youtube.com"};
    return input;
}

bool is_canary(const std::string& name) {
    return std::regex_match(name, std::regex("[0-9a-f]{16}\\.nxcheck\\.keen-pbr\\.fyi"));
}

} // namespace

TEST_CASE("dns check: a healthy server is ok and every question is asked once, plus the canary") {
    auto input = base_input();
    input.servers = {make_server("a", "8.8.8.8")};
    std::vector<std::string> seen;
    const auto report = run_dns_check(input, probe_with(
        [](const DnsBalancerUpstream&, const std::string& name, const Bytes& q) {
            if (is_canary(name)) return answered(q, 3, {}, DnsUpstreamOutcome::Nxdomain);
            return answered(q, 0, {fake_dns::a_rr("93.184.216.34")});
        }, &seen));
    CHECK(report.overall == DnsCheckSeverity::Ok);
    REQUIRE(report.servers.size() == 1);
    const auto& server = report.servers[0];
    CHECK(server.tag == "a");
    CHECK(server.type == "static");
    REQUIRE(server.endpoints.size() == 1);
    const auto& endpoint = server.endpoints[0];
    CHECK(endpoint.address == "8.8.8.8");
    CHECK(endpoint.status == "ok");
    CHECK(endpoint.nxdomain_hijack == std::optional<bool>(false));
    REQUIRE(endpoint.results.size() == 2);
    CHECK(endpoint.results[0].domain == "yandex.ru");
    CHECK(endpoint.results[1].domain == "youtube.com");
    CHECK(seen.size() == 3);
    CHECK(is_canary(seen.back()));
}

TEST_CASE("dns check: tampering is a warning, silence is an error") {
    auto input = base_input();
    input.servers = {make_server("honest", "1.1.1.1"), make_server("blocking", "9.9.9.9:5353"),
                     make_server("hijacking", "2.2.2.2"), make_server("dead", "3.3.3.3")};
    const auto report = run_dns_check(input, probe_with(
        [](const DnsBalancerUpstream& upstream, const std::string& name, const Bytes& q) {
            if (upstream.ip == "3.3.3.3") return failed(DnsUpstreamOutcome::Timeout, "no answer in time");
            if (upstream.ip == "9.9.9.9" && name == "youtube.com") {
                return answered(q, 3, {}, DnsUpstreamOutcome::Nxdomain);
            }
            if (upstream.ip == "2.2.2.2" && is_canary(name)) {
                return answered(q, 0, {fake_dns::a_rr("203.0.113.9")});
            }
            if (is_canary(name)) return answered(q, 3, {}, DnsUpstreamOutcome::Nxdomain);
            return answered(q, 0, {fake_dns::a_rr("93.184.216.34")});
        }));
    CHECK(report.overall == DnsCheckSeverity::Error);
    REQUIRE(report.servers.size() == 4);
    CHECK(report.servers[0].endpoints[0].results[1].severity == DnsCheckSeverity::Ok);
    CHECK(report.servers[1].endpoints[0].address == "9.9.9.9:5353");
    CHECK(report.servers[1].endpoints[0].results[1].status == "nxdomain");
    CHECK(report.servers[2].endpoints[0].nxdomain_hijack == std::optional<bool>(true));
    CHECK(report.servers[3].endpoints[0].results[0].status == "timeout");
    // The canary got no answer from the dead server: nothing to say about it.
    CHECK_FALSE(report.servers[3].endpoints[0].nxdomain_hijack.has_value());

    // Without the dead server the worst thing is a warning.
    input.servers.pop_back();
    const auto warning = run_dns_check(input, probe_with(
        [](const DnsBalancerUpstream& upstream, const std::string& name, const Bytes& q) {
            if (upstream.ip == "2.2.2.2" && is_canary(name)) {
                return answered(q, 0, {fake_dns::a_rr("203.0.113.9")});
            }
            if (is_canary(name)) return answered(q, 3, {}, DnsUpstreamOutcome::Nxdomain);
            return answered(q, 0, {fake_dns::a_rr("93.184.216.34")});
        }));
    CHECK(warning.overall == DnsCheckSeverity::Warning);
}

TEST_CASE("dns check: custom domains are judged leniently") {
    auto input = base_input();
    input.request.domains = {"yandex.ru"};
    input.request.custom_domains = {"mine.example"};
    input.servers = {make_server("a", "8.8.8.8")};
    const auto report = run_dns_check(input, probe_with(
        [](const DnsBalancerUpstream&, const std::string& name, const Bytes& q) {
            if (name == "mine.example" || is_canary(name)) {
                return answered(q, 3, {}, DnsUpstreamOutcome::Nxdomain);
            }
            return answered(q, 0, {fake_dns::a_rr("77.88.55.60")});
        }));
    CHECK(report.overall == DnsCheckSeverity::Ok);
    CHECK(report.servers[0].endpoints[0].results[1].domain == "mine.example");
    CHECK(report.servers[0].endpoints[0].results[1].status == "nxdomain");
}

TEST_CASE("dns check: IPv6 servers are skipped while IPv6 is off, DoH is unavailable without support") {
    auto input = base_input();
    input.ipv6_enabled = false;
    input.doh_unavailable = std::make_pair(std::string("doh_no_certificates"), std::string("no CA certificates"));
    input.servers = {make_server("v6", "[2001:db8::1]:5353"),
                     make_server("g", "https://dns.google/dns-query", api::DnsServerType::DOH, "vpn")};
    std::vector<std::string> seen;
    const auto report = run_dns_check(input, probe_with(
        [](const DnsBalancerUpstream&, const std::string&, const Bytes& q) {
            return answered(q, 0, {fake_dns::a_rr("93.184.216.34")});
        }, &seen));
    CHECK(seen.empty());
    REQUIRE(report.servers.size() == 2);
    CHECK(report.servers[0].endpoints[0].address == "[2001:db8::1]:5353");
    CHECK(report.servers[0].endpoints[0].status == "skipped");
    CHECK(report.servers[0].endpoints[0].reason_code == std::optional<std::string>("ipv6_disabled"));
    CHECK(report.servers[1].type == "doh");
    CHECK(report.servers[1].detour == std::optional<std::string>("vpn"));
    CHECK(report.servers[1].endpoints[0].status == "unavailable");
    CHECK(report.servers[1].endpoints[0].reason_code == std::optional<std::string>("doh_no_certificates"));
    CHECK(report.overall == DnsCheckSeverity::Error);
}

TEST_CASE("dns check: a server the probe cannot use is unavailable with the probe's reason") {
    auto input = base_input();
    input.servers = {make_server("g", "https://dns.google/dns-query", api::DnsServerType::DOH, "vpn")};
    const auto report = run_dns_check(input, probe_with(
        [](const DnsBalancerUpstream&, const std::string&, const Bytes&) {
            DnsProbeResult result;
            result.usable = false;
            result.error = "the outbound has no routing mark";
            return result;
        }));
    const auto& endpoint = report.servers[0].endpoints[0];
    CHECK(endpoint.status == "unavailable");
    CHECK(endpoint.reason == std::optional<std::string>("the outbound has no routing mark"));
    CHECK(endpoint.results.empty());
    CHECK(report.overall == DnsCheckSeverity::Error);
}

TEST_CASE("dns check: a keenetic server is checked at every address it reports") {
    auto input = base_input();
    input.servers = {make_server("k", "", api::DnsServerType::KEENETIC)};
    input.keenetic_addresses = [] { return std::vector<std::string>{"192.168.1.1", "192.168.1.2:5353"}; };
    const auto ok = run_dns_check(input, probe_with(
        [](const DnsBalancerUpstream&, const std::string&, const Bytes& q) {
            return answered(q, 0, {fake_dns::a_rr("93.184.216.34")});
        }));
    REQUIRE(ok.servers[0].endpoints.size() == 2);
    CHECK(ok.servers[0].type == "keenetic");
    CHECK(ok.servers[0].endpoints[1].address == "192.168.1.2:5353");

    input.keenetic_addresses = []() -> std::vector<std::string> { throw std::runtime_error("RCI is down"); };
    std::vector<std::string> seen;
    const auto down = run_dns_check(input, probe_with(
        [](const DnsBalancerUpstream&, const std::string&, const Bytes& q) { return answered(q, 0); }, &seen));
    CHECK(seen.empty());
    CHECK(down.servers[0].endpoints.empty());
    CHECK(down.servers[0].error_code == std::optional<std::string>("keenetic_dns_unavailable"));
    CHECK(down.overall == DnsCheckSeverity::Error);
}

TEST_CASE("dns check: only the asked servers are checked, an unknown tag is refused") {
    auto input = base_input();
    input.servers = {make_server("a", "8.8.8.8"), make_server("b", "1.1.1.1")};
    input.request.servers = {"b"};
    const auto report = run_dns_check(input, probe_with(
        [](const DnsBalancerUpstream&, const std::string&, const Bytes& q) {
            return answered(q, 0, {fake_dns::a_rr("93.184.216.34")});
        }));
    REQUIRE(report.servers.size() == 1);
    CHECK(report.servers[0].tag == "b");

    input.request.servers = {"nope"};
    CHECK_THROWS_AS(run_dns_check(input, probe_with([](const DnsBalancerUpstream&, const std::string&,
                                                         const Bytes& q) { return answered(q, 0); })),
                    DnsCheckRequestError);
}

TEST_CASE("dns check: the report as JSON") {
    auto input = base_input();
    input.request.domains = {"yandex.ru"};
    input.servers = {make_server("a", "8.8.8.8", api::DnsServerType::STATIC, "vpn")};
    const auto report = run_dns_check(input, probe_with(
        [](const DnsBalancerUpstream&, const std::string& name, const Bytes& q) {
            if (is_canary(name)) return answered(q, 3, {}, DnsUpstreamOutcome::Nxdomain);
            return answered(q, 0, {fake_dns::a_rr("77.88.55.60")});
        }));
    const auto json = dns_check_report_to_json(report);
    CHECK(json["overall"] == "ok");
    const auto& server = json["servers"][0];
    CHECK(server["tag"] == "a");
    CHECK(server["detour"] == "vpn");
    const auto& endpoint = server["endpoints"][0];
    CHECK(endpoint["address"] == "8.8.8.8");
    CHECK(endpoint["nxdomain_hijack"] == false);
    CHECK_FALSE(endpoint.contains("reason"));
    const auto& result = endpoint["results"][0];
    CHECK(result["domain"] == "yandex.ru");
    CHECK(result["status"] == "ok");
    CHECK(result["severity"] == "ok");
    CHECK(result["rcode"] == 0);
    CHECK(result["addresses"] == nlohmann::json::array({"77.88.55.60"}));
    CHECK(result["duration_ms"].is_number());
}
