#include <doctest/doctest.h>

#include "../src/health/health_issues.hpp"

#include <algorithm>
#include <string>
#include <vector>

using namespace keen_pbr3;

namespace {

api::InterceptHealthClass intercept_health(bool nfqueue = true, bool nflog = true,
                                           bool connbytes = true) {
    api::InterceptHealthClass health{};
    health.enabled = true;
    health.running = true;
    health.capabilities.nfqueue = nfqueue;
    health.capabilities.nflog = nflog;
    health.capabilities.connbytes = connbytes;
    health.capabilities.addrtype = true;
    return health;
}

api::XtExtensionHealthElement extension(const std::string& name, bool available,
                                        bool required) {
    api::XtExtensionHealthElement ext{};
    ext.name = name;
    ext.kind = api::Kind::MATCH;
    ext.kernel_module = "xt_" + name;
    ext.available = available;
    ext.required = required;
    return ext;
}

api::InterceptProbeFeatureElement probe(const std::string& feature,
                                        api::InterceptProbeFeatureStatus status,
                                        std::optional<std::string> reason = std::nullopt) {
    api::InterceptProbeFeatureElement element;
    element.feature = feature;
    element.status = status;
    element.reason = std::move(reason);
    return element;
}

std::vector<std::string> keys(const std::vector<api::KernelCheckElement>& checks) {
    std::vector<std::string> out;
    for (const auto& check : checks) out.push_back(check.key);
    return out;
}

const api::HealthIssue* find_issue(const api::HealthIssuesResponse& response,
                                   const std::string& key) {
    const auto it = std::find_if(response.issues.begin(), response.issues.end(),
                                 [&](const auto& issue) { return issue.key == key; });
    return it == response.issues.end() ? nullptr : &*it;
}

} // namespace

TEST_CASE("kernel checks keep only the ones the requested features need") {
    auto health = intercept_health(true, false, true);
    health.probes = std::vector<api::InterceptProbeFeatureElement>{
        probe("nfqueue", api::InterceptProbeFeatureStatus::OK),
        probe("nflog", api::InterceptProbeFeatureStatus::UNSUPPORTED, "no module"),
        probe("fail_open", api::InterceptProbeFeatureStatus::SKIPPED),
    };

    const auto checks = collect_kernel_checks(health, /*dns=*/true, /*l7=*/false);
    CHECK(keys(checks) == std::vector<std::string>{
                              "capability:nfqueue", "capability:addrtype", "probe:nfqueue",
                              "probe:fail_open"});

    // nflog is not requested, so its failed probe is not an issue.
    HealthIssueInputs inputs;
    inputs.intercept = &health;
    inputs.requested_dns = true;
    CHECK(collect_health_issues(inputs).issues.empty());
}

TEST_CASE("addrtype only warns and the extension list replaces it") {
    auto health = intercept_health();
    health.capabilities.addrtype = false;
    HealthIssueInputs inputs;
    inputs.intercept = &health;
    auto response = collect_health_issues(inputs);
    REQUIRE(response.issues.size() == 1);
    CHECK(response.issues[0].key == "capability:addrtype");
    CHECK(response.issues[0].severity == api::Severity::WARNING);
    CHECK(response.issues[0].detail_code == api::DetailCode::CAPABILITY_FALLBACK);

    health.capabilities.xt_extensions =
        std::vector<api::XtExtensionHealthElement>{extension("addrtype", true, false)};
    CHECK(keys(collect_kernel_checks(health, false, false)) ==
          std::vector<std::string>{"extension:match:addrtype"});
}

TEST_CASE("a missing extension the rules use is an error, an unused one warns") {
    auto health = intercept_health();
    health.capabilities.xt_extensions = std::vector<api::XtExtensionHealthElement>{
        extension("multiport", false, true), extension("dscp", false, false),
        extension("addrtype", false, false), extension("set", true, true)};
    HealthIssueInputs inputs;
    inputs.intercept = &health;
    const auto response = collect_health_issues(inputs);

    REQUIRE(response.issues.size() == 3);
    const auto* multiport = find_issue(response, "extension:match:multiport");
    REQUIRE(multiport);
    CHECK(multiport->severity == api::Severity::ERROR);
    CHECK(multiport->detail_code == api::DetailCode::XT_EXTENSION_REQUIRED);
    CHECK(multiport->title == "iptables match multiport (xt_multiport)");
    CHECK(multiport->params.at("module") == "xt_multiport");
    CHECK(find_issue(response, "extension:match:dscp")->detail_code ==
          api::DetailCode::XT_EXTENSION_UNUSED);
    CHECK(find_issue(response, "extension:match:addrtype")->detail_code ==
          api::DetailCode::CAPABILITY_FALLBACK);
}

TEST_CASE("a stopped service reports only kernel issues") {
    auto health = intercept_health(false);
    health.reasons = {"dns hold disabled"};
    RoutingHealthReport routing;
    routing.error = "boom";
    HealthIssueInputs inputs;
    inputs.intercept = &health;
    inputs.requested_dns = true;
    inputs.routing_health = &routing;
    const auto response = collect_health_issues(inputs);
    REQUIRE(response.issues.size() == 1);
    CHECK(response.issues[0].code == api::Code::KERNEL_CAPABILITY);
    CHECK(response.issues[0].severity == api::Severity::ERROR);
}

TEST_CASE("routing health issues list the failing items") {
    RoutingHealthReport routing;
    routing.firewall_chain.chain_present = true;
    routing.firewall_chain.prerouting_hook_present = true;
    routing.firewall_chain.verification_state = VerificationState::verified;
    FirewallRuleCheck rule;
    rule.set_name = "kpbr4_vpn";
    rule.status = CheckStatus::missing;
    routing.firewall_rules = {rule};
    RouteTableCheck table;
    table.outbound_tag = "vpn";
    table.table_id = 402;
    table.table_exists = true;
    table.default_route_present = false;
    table.interface_matches = true;
    table.gateway_matches = true;
    table.status = CheckStatus::mismatch;
    routing.route_tables = {table};
    HealthWarning warning;
    warning.code = HealthWarningCode::nat_missing;
    warning.interface = "eth1";
    warning.message = "no NAT";
    routing.warnings = {warning};

    HealthIssueInputs inputs;
    inputs.service_running = true;
    inputs.routing_health = &routing;
    const auto response = collect_health_issues(inputs);

    const auto* rules = find_issue(response, "firewall-rules");
    REQUIRE(rules);
    CHECK(rules->severity == api::Severity::ERROR);
    CHECK(rules->title == "Firewall rules do not match: 1");
    CHECK(rules->entries == std::vector<std::string>{"kpbr4_vpn: missing"});
    const auto* routes = find_issue(response, "route-tables");
    REQUIRE(routes);
    CHECK(routes->entries ==
          std::vector<std::string>{"vpn (table 402): default route missing"});
    const auto* nat = find_issue(response, "host-warning-nat_missing-eth1");
    REQUIRE(nat);
    CHECK(nat->severity == api::Severity::WARNING);
    CHECK(nat->params.at("warning") == "nat_missing");
}

TEST_CASE("an unavailable firewall verification is not a missing chain") {
    RoutingHealthReport routing;
    routing.firewall_chain.verification_state = VerificationState::unavailable;
    HealthIssueInputs inputs;
    inputs.service_running = true;
    inputs.routing_health = &routing;
    CHECK(collect_health_issues(inputs).issues.empty());
}

TEST_CASE("outbound issues: unavailable is an error, failing group members warn") {
    api::RuntimeOutboundStateElement down{};
    down.tag = "vpn";
    down.type = api::OutboundType::INTERFACE;
    down.status = api::RuntimeOutboundStatusEnum::UNAVAILABLE;

    api::RuntimeInterfaceState member{};
    member.outbound_tag = "wan2";
    member.status = api::RuntimeInterfaceStatusEnum::DEGRADED;
    member.detail = "probe timeout";
    api::RuntimeOutboundStateElement group{};
    group.tag = "balance";
    group.type = api::OutboundType::URLTEST;
    group.status = api::RuntimeOutboundStatusEnum::HEALTHY;
    group.interfaces = {member};

    const std::vector<api::RuntimeOutboundStateElement> outbounds{down, group};
    HealthIssueInputs inputs;
    inputs.service_running = true;
    inputs.outbounds = &outbounds;
    const auto response = collect_health_issues(inputs);

    const auto* vpn = find_issue(response, "outbound-vpn");
    REQUIRE(vpn);
    CHECK(vpn->severity == api::Severity::ERROR);
    CHECK(vpn->outbound == std::optional<std::string>("vpn"));
    const auto* members = find_issue(response, "outbound-balance-members");
    REQUIRE(members);
    CHECK(members->severity == api::Severity::WARNING);
    CHECK(members->entries ==
          std::vector<std::string>{"wan2 · degraded · probe timeout"});
}

TEST_CASE("a dead dnsmasq without an error message gets a fixed detail") {
    api::DnsmasqHealth dnsmasq{};
    dnsmasq.mode = api::ResolverIntegration::DNSMASQ;
    dnsmasq.state = api::State::ERROR;
    dnsmasq.dnsmasq_alive = api::DnsmasqAlive::DEAD;
    HealthIssueInputs inputs;
    inputs.service_running = true;
    inputs.dnsmasq = &dnsmasq;
    const auto response = collect_health_issues(inputs);
    REQUIRE(response.issues.size() == 1);
    CHECK(response.issues[0].code == api::Code::DNSMASQ_ERROR);
    CHECK(response.issues[0].detail_code == api::DetailCode::DNSMASQ_DEAD);
}

TEST_CASE("a missing dnsmasq points at the install guide of the OS") {
    api::DnsmasqHealth dnsmasq{};
    dnsmasq.mode = api::ResolverIntegration::DNSMASQ;
    dnsmasq.state = api::State::ERROR;
    dnsmasq.error_code = "DNSMASQ_NOT_INSTALLED";
    dnsmasq.last_error = "dnsmasq is not installed";
    HealthIssueInputs inputs;
    inputs.service_running = true;
    inputs.dnsmasq = &dnsmasq;
    inputs.dnsmasq_setup_docs_url = dnsmasq_setup_docs_url("keenetic");
    const auto response = collect_health_issues(inputs);
    REQUIRE(response.issues.size() == 1);
    CHECK(response.issues[0].detail ==
          std::optional<std::string>(
              "dnsmasq is not installed, see https://keen-pbr.fyi/docs/"
              "getting-started/installation/keenetic/#dnsmasq-install"));
}

TEST_CASE("dnsmasq install guide link follows the OS") {
    CHECK(dnsmasq_setup_docs_url("openwrt") ==
          "https://keen-pbr.fyi/docs/getting-started/installation/openwrt/#dnsmasq-install");
    CHECK(dnsmasq_setup_docs_url("debian") ==
          "https://keen-pbr.fyi/docs/getting-started/installation/debian/#dnsmasq-install");
    CHECK(dnsmasq_setup_docs_url("linux") ==
          "https://keen-pbr.fyi/docs/configuration/dns/#do-i-need-dnsmasq");
}

TEST_CASE("an undownloaded list is a warning with its name, url, detour and error") {
    HealthIssueInputs inputs;
    inputs.service_running = true;
    inputs.undownloaded_lists.push_back(
        UndownloadedListInfo{"blocked", "https://example.com/l.txt", "vpn", "HTTP 404"});
    const auto response = collect_health_issues(inputs);
    REQUIRE(response.issues.size() == 1);
    const auto& issue = response.issues[0];
    CHECK(issue.code == api::Code::LIST_NOT_DOWNLOADED);
    CHECK(issue.severity == api::Severity::WARNING);
    CHECK(issue.title == "List 'blocked' is not downloaded yet");
    CHECK(issue.params.at("list") == "blocked");
    CHECK(issue.params.at("detour") == "vpn");
    REQUIRE(issue.detail.has_value());
    CHECK(issue.detail->find("HTTP 404") != std::string::npos);
    CHECK(issue.detail->find("https://example.com/l.txt") != std::string::npos);
    CHECK(issue.detail->find("vpn") != std::string::npos);

    inputs.undownloaded_lists.clear();
    CHECK(collect_health_issues(inputs).issues.empty());
}

namespace {

DnsUpstreamStats dns_upstream(const std::string& ip, const std::string& name,
                              std::uint32_t failures, bool usable = true) {
    DnsUpstreamStats stats;
    stats.upstream = DnsBalancerUpstream::plain(ip);
    stats.upstream.name = name;
    stats.usable = usable;
    stats.consecutive_failures = failures;
    stats.last_failure = DnsUpstreamOutcome::Timeout;
    stats.last_error = "no answer in time";
    return stats;
}

DnsListenerStats dns_listener(std::uint16_t port, std::vector<DnsUpstreamStats> upstreams) {
    DnsListenerStats listener;
    listener.port = port;
    listener.upstreams = std::move(upstreams);
    return listener;
}

} // namespace

TEST_CASE("a DNS server that failed ten times in a row is a warning") {
    DnsBalancerStats stats;
    stats.listeners.push_back(dns_listener(
        52100, {dns_upstream("192.0.2.1", "dns_a", kDnsFailingStreak),
                dns_upstream("192.0.2.2", "dns_b", kDnsFailingStreak - 1)}));
    const auto failures = collect_dns_server_failures(stats, {{52100, {"blocked"}}});
    REQUIRE(failures.size() == 1);
    CHECK(failures[0].tag == "dns_a");
    CHECK(failures[0].reason == "timeout");
    CHECK_FALSE(failures[0].group_down);
    CHECK(failures[0].entries.empty());

    HealthIssueInputs inputs;
    inputs.service_running = true;
    inputs.dns_server_failures = failures;
    const auto response = collect_health_issues(inputs);
    REQUIRE(response.issues.size() == 1);
    const auto& issue = response.issues[0];
    CHECK(issue.code == api::Code::DNS_SERVER_FAILING);
    CHECK(issue.severity == api::Severity::WARNING);
    CHECK(issue.title == "DNS server 'dns_a' is not answering");
    CHECK(issue.params.at("tag") == "dns_a");
    CHECK(issue.params.at("address") == "192.0.2.1:53");
    CHECK(issue.params.at("reason") == "timeout");
    CHECK(issue.params.at("count") == "10");
    CHECK_FALSE(issue.entries.has_value());
    REQUIRE(issue.detail.has_value());
    CHECK(issue.detail->find("no answer in time") != std::string::npos);
}

TEST_CASE("when every server of a group fails the issue is an error naming the lists") {
    DnsBalancerStats stats;
    stats.listeners.push_back(dns_listener(
        52100, {dns_upstream("192.0.2.1", "dns_a", 12), dns_upstream("192.0.2.2", "dns_b", 10)}));
    // A server in two groups is reported once; the second group still has a working one.
    stats.listeners.push_back(
        dns_listener(52101, {dns_upstream("192.0.2.1", "dns_a", 12), dns_upstream("192.0.2.3", "dns_c", 0)}));
    const DnsProxyUsers users{{52100, {"blocked", "video"}}, {52101, {kDnsDefaultServersEntry}}};
    const auto failures = collect_dns_server_failures(stats, users);
    REQUIRE(failures.size() == 2);
    for (const auto& failure : failures) {
        CHECK(failure.group_down);
        CHECK(failure.entries == std::vector<std::string>{"blocked", "video"});
    }

    HealthIssueInputs inputs;
    inputs.service_running = true;
    inputs.dns_server_failures = failures;
    const auto response = collect_health_issues(inputs);
    REQUIRE(response.issues.size() == 2);
    for (const auto& issue : response.issues) {
        CHECK(issue.severity == api::Severity::ERROR);
        REQUIRE(issue.entries.has_value());
        CHECK(*issue.entries == std::vector<std::string>{"blocked", "video"});
    }
}

TEST_CASE("DNS servers that cannot be used are not reported as failing") {
    DnsBalancerStats stats;
    stats.listeners.push_back(dns_listener(
        52100, {dns_upstream("192.0.2.1", "dns_a", 20, false), dns_upstream("192.0.2.2", "dns_b", 20)}));
    const auto failures = collect_dns_server_failures(stats, {});
    REQUIRE(failures.size() == 1);
    CHECK(failures[0].tag == "dns_b");
    // The unusable one does not count when deciding that the whole group is down.
    CHECK(failures[0].group_down);

    DnsBalancerStats only_unusable;
    only_unusable.listeners.push_back(dns_listener(52100, {dns_upstream("192.0.2.1", "dns_a", 20, false)}));
    CHECK(collect_dns_server_failures(only_unusable, {}).empty());
}

TEST_CASE("several tags behind one DNS server are shown together, a detour is named") {
    DnsServerFailure failure;
    failure.tag = "dns_a,dns_b";
    failure.address = "192.0.2.1:53";
    failure.detour = "vpn";
    failure.reason = "connection_refused";
    failure.error = "Connection refused";
    failure.count = 11;
    HealthIssueInputs inputs;
    inputs.service_running = true;
    inputs.dns_server_failures = {failure};
    const auto response = collect_health_issues(inputs);
    REQUIRE(response.issues.size() == 1);
    const auto& issue = response.issues[0];
    CHECK(issue.title == "DNS server 'dns_a, dns_b' is not answering");
    CHECK(issue.outbound == std::optional<std::string>("vpn"));
    CHECK(issue.params.at("outbound") == "vpn");
    CHECK(issue.detail->find("via vpn") != std::string::npos);

    inputs.service_running = false;
    CHECK(collect_health_issues(inputs).issues.empty());
}
