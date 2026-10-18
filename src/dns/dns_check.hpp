#pragma once

#include "../config/config.hpp"
#include "dns_balancer.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <stdexcept>
#include <utility>
#include <string>
#include <vector>

namespace keen_pbr3 {

// The check of the DNS servers of dns.servers: every server is asked for a few
// well known domains, and the answers are told apart into "works", "does not
// answer" and "answers, but looks tampered with" (a blocked domain reported as
// not existing, an address of a block page).

constexpr std::size_t kDnsCheckMaxDomains = 5;
// The whole check gives up after this long.
constexpr std::chrono::milliseconds kDnsCheckDeadline{6000};

class DnsCheckRequestError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct DnsCheckRequest {
    // Domains known to exist: NXDOMAIN for them is suspicious.
    std::vector<std::string> domains;
    // Domains of the user: a lack of records just says so, NXDOMAIN is still
    // a warning, as the user may know the domain exists.
    std::vector<std::string> custom_domains;
    // Tags of dns.servers to check; empty: all.
    std::vector<std::string> servers;
};

// Reads the body of POST /api/dns/servers/check. Throws DnsCheckRequestError.
DnsCheckRequest parse_dns_check_request(const nlohmann::json& body);

// A host name made of letters, digits and hyphens in labels of up to 63
// characters, at most 253 in all, without a trailing dot.
bool is_valid_dns_check_domain(const std::string& domain);

enum class DnsCheckSeverity : std::uint8_t { Ok, Warning, Error };
const char* dns_check_severity_name(DnsCheckSeverity severity);

// An address a DNS server should not hand out for a public site: unspecified,
// loopback, private, link-local, shared (CGNAT). `family` is 4 or 16 bytes
// of `addr` are used for 6. With `null_route_only` just the ones that mean
// "blocked": 0.0.0.0/8, 127.0.0.0/8, :: and ::1.
bool is_dns_check_stub_address(std::uint8_t family, const std::uint8_t* addr,
                               bool null_route_only = false);

struct DnsCheckResult {
    std::string domain;
    // ok, nxdomain, nodata, stub_address, servfail, refused, rcode, or why
    // there was no answer: timeout, connection_refused, unreachable,
    // connection_closed, tls_error, http_error, bad_answer, not_resolved,
    // network_error.
    std::string status;
    DnsCheckSeverity severity{DnsCheckSeverity::Ok};
    std::optional<int> rcode;
    std::vector<std::string> addresses;
    double duration_ms{0};
    std::optional<std::string> error;
};

// Judges the reply to the question for `domain`. `custom`: a domain of the
// user, which the check cannot know to exist.
DnsCheckResult classify_dns_check_answer(const std::string& domain, bool custom,
                                         const DnsProbeResult& probe);


struct DnsCheckEndpoint {
    std::string address;  // "ip", "ip:port", "[ipv6]:port" or the DoH URL
    std::string status{"ok"};  // ok, skipped, unavailable
    std::optional<std::string> reason_code;
    std::optional<std::string> reason;  // for skipped and unavailable
    std::vector<std::string> ips;  // DoH: the fixed addresses it was asked at
    std::vector<DnsCheckResult> results;
};

struct DnsCheckServer {
    std::string tag;
    std::string type;  // static, doh, keenetic
    std::optional<std::string> detour;
    std::optional<std::string> error_code;
    std::optional<std::string> error;  // the server's address could not be determined
    std::vector<DnsCheckEndpoint> endpoints;
};

struct DnsCheckReport {
    DnsCheckSeverity overall{DnsCheckSeverity::Ok};
    std::vector<DnsCheckServer> servers;
};

// ok unless something warns; error when a server does not answer.
DnsCheckSeverity dns_check_overall(const std::vector<DnsCheckServer>& servers);

nlohmann::json dns_check_report_to_json(const DnsCheckReport& report);

struct DnsCheckInput {
    DnsCheckRequest request;
    std::vector<DnsServer> servers;  // dns.servers of the applied config
    std::vector<std::string> bootstrap;  // dns.bootstrap of the applied config
    bool ipv6_enabled{true};
    // Set when DoH cannot be used at all; the code goes to the reason_code.
    std::optional<std::pair<std::string, std::string>> doh_unavailable;
    // The addresses behind a `keenetic` server; throws when it cannot say.
    std::function<std::vector<std::string>()> keenetic_addresses;
    // What the probes need (CA certificates, marks of the detours, ...).
    DnsBalancerSettings settings;
};

using DnsCheckProbeFn = std::function<std::vector<DnsProbeResult>(
    const std::vector<DnsProbeRequest>&, std::chrono::milliseconds, const DnsBalancerSettings&)>;

// Throws DnsCheckRequestError for a server tag that is not in dns.servers.
DnsCheckReport run_dns_check(const DnsCheckInput& input, const DnsCheckProbeFn& probe);

} // namespace keen_pbr3
