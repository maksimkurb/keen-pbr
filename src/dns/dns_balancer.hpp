#pragma once

#include "../config/config_defaults.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace keen_pbr3 {

// The local DNS forwarder listens on one address (dns.proxy_address), one port
// per set of upstreams (UDP and TCP). Ports are dns.proxy_port_start + the
// position of the set, at most kDnsBalancerMaxGroups of them.
constexpr std::size_t kDnsBalancerMaxGroups = 32;

enum class DnsBalancerUpstreamKind : std::uint8_t {
    Plain,  // UDP, with a TCP retry for truncated answers
    Doh,    // DNS-over-HTTPS (RFC 8484, POST)
};

struct DnsBalancerUpstream {
    DnsBalancerUpstreamKind kind{DnsBalancerUpstreamKind::Plain};
    // Plain: the server address (IPv4 or IPv6 literal) and port.
    std::string ip;
    std::uint16_t port{53};
    // DoH: the URL; `detour` is the outbound whose firewall mark the
    // connection carries ("" for none).
    std::string url;
    std::string detour;
    // DoH: fixed addresses of the server (no bootstrap lookup); empty: the host
    // of the URL is looked up.
    std::vector<std::string> ips;
    // The tags of the dns.servers entries behind this address, comma-joined;
    // only for labels. It is not part of the identity of the upstream: not
    // compared by operator== and not in the key of a set of upstreams.
    std::string name;

    static DnsBalancerUpstream plain(std::string ip, std::uint16_t port = 53) {
        DnsBalancerUpstream upstream;
        upstream.ip = std::move(ip);
        upstream.port = port;
        return upstream;
    }
    static DnsBalancerUpstream doh(std::string url, std::string detour = {},
                                   std::vector<std::string> ips = {}) {
        DnsBalancerUpstream upstream;
        upstream.kind = DnsBalancerUpstreamKind::Doh;
        upstream.url = std::move(url);
        upstream.detour = std::move(detour);
        upstream.ips = std::move(ips);
        return upstream;
    }
};

struct DnsBalancerGroup {
    std::uint16_t port{0};    // listen port on the forwarder address
    std::vector<DnsBalancerUpstream> upstreams;  // in the order they are tried
};

// What the groups resolve for, by port: the names of the lists, or this entry
// for the default servers.
constexpr const char* kDnsDefaultServersEntry = "default servers";
using DnsProxyUsers = std::map<std::uint16_t, std::vector<std::string>>;

// `name` is left out on purpose, see the field.
inline bool operator==(const DnsBalancerUpstream& a, const DnsBalancerUpstream& b) {
    return a.kind == b.kind && a.ip == b.ip && a.port == b.port && a.url == b.url &&
           a.detour == b.detour && a.ips == b.ips;
}
inline bool operator==(const DnsBalancerGroup& a, const DnsBalancerGroup& b) {
    return a.port == b.port && a.upstreams == b.upstreams;
}

// Where the forwarder gets what its upstreams cannot know on their own.
struct DnsBalancerSettings {
    // Firewall mark of a DoH connection, by the outbound tag of `detour`.
    std::map<std::string, std::uint32_t> detour_marks;
    // The servers that resolve the host names of DoH URLs, asked in order;
    // empty: the system resolver. None may be the forwarder itself.
    std::vector<DnsBalancerUpstream> bootstrap;
    // False: DoH upstreams are not used (no DoH in this build, no CA
    // certificates); a group of only such upstreams answers SERVFAIL.
    bool doh_enabled{false};
    // Trust anchors for DoH: a file with certificates or a directory of them.
    std::string ca_path;
    bool ca_is_directory{false};
    // Address of the listeners; empty: DnsBalancerOptions::listen_ip. A new
    // address reopens every listener.
    std::string listen_ip;
};

inline bool operator==(const DnsBalancerSettings& a, const DnsBalancerSettings& b) {
    return a.listen_ip == b.listen_ip && a.detour_marks == b.detour_marks && a.bootstrap == b.bootstrap &&
           a.doh_enabled == b.doh_enabled && a.ca_path == b.ca_path &&
           a.ca_is_directory == b.ca_is_directory;
}

struct DnsBalancerOptions {
    std::string listen_ip{config_defaults::dns_proxy_address};
    // How long one upstream gets to answer before the next one is tried.
    std::chrono::milliseconds udp_attempt_timeout{1500};
    std::chrono::milliseconds tcp_attempt_timeout{2500};
    // A DoH query may need a TLS handshake first.
    std::chrono::milliseconds doh_attempt_timeout{4000};
    // An upstream that failed is skipped this long (unless all of them failed).
    std::chrono::milliseconds dead_for{10000};
    // A TCP client connection that does nothing is closed after this.
    std::chrono::milliseconds tcp_idle{10000};
    // Tests only: also accept http:// DoH URLs.
    bool allow_plain_http{false};
    // Called from the forwarder thread when the set of upstreams that keep
    // failing (see kDnsFailingStreak) changes. Must not block and must not
    // call back into the DnsBalancer.
    std::function<void()> on_health_change;
};

// What one attempt to get an answer from an upstream came to.
enum class DnsUpstreamOutcome : std::uint8_t {
    Ok,                // NOERROR (with or without records)
    Nxdomain,
    Servfail,
    Refused,
    OtherRcode,
    Timeout,
    ConnectionRefused, // including ICMP port unreachable on UDP
    Unreachable,       // no route, or the firewall refused to send
    ConnectionClosed,  // the TCP/TLS connection ended before the answer
    TlsError,
    HttpError,         // DoH: an HTTP status other than 200
    BadAnswer,         // not a DNS message, truncated, too big
    NotResolved,       // DoH: the host name has no address
    NetworkError,      // anything else
};
constexpr std::size_t kDnsUpstreamOutcomeCount = 14;

const char* dns_upstream_outcome_name(DnsUpstreamOutcome outcome);

// A DNS message came back (whatever its rcode).
inline bool dns_upstream_outcome_answered(DnsUpstreamOutcome outcome) {
    return outcome <= DnsUpstreamOutcome::OtherRcode;
}
// Counts towards an upstream "not answering": no DNS message, SERVFAIL or
// REFUSED. Any other answer (NXDOMAIN included) ends such a streak.
inline bool dns_upstream_outcome_failed(DnsUpstreamOutcome outcome) {
    return !dns_upstream_outcome_answered(outcome) || outcome == DnsUpstreamOutcome::Servfail ||
           outcome == DnsUpstreamOutcome::Refused;
}

// This many failed attempts in a row make an upstream "failing".
constexpr std::uint32_t kDnsFailingStreak = 10;

// Upper bounds of the answer time histogram (milliseconds); the counts have
// one more bucket for everything slower.
constexpr std::array<std::uint32_t, 10> kDnsLatencyBoundsMs{5, 10, 25, 50, 100, 250, 500, 1000, 2500, 5000};

struct DnsUpstreamStats {
    DnsBalancerUpstream upstream;
    bool usable{true};   // false: never tried (no DoH support, no mark for the detour)
    bool up{true};       // false: skipped at the moment after a failure
    std::array<std::uint64_t, kDnsUpstreamOutcomeCount> results{};
    // Time to the answer of the attempts that got one; not cumulative.
    std::array<std::uint64_t, kDnsLatencyBoundsMs.size() + 1> latency_buckets{};
    std::uint64_t latency_sum_us{0};
    // Failed attempts in a row, shared by every group the upstream is in.
    std::uint32_t consecutive_failures{0};
    DnsUpstreamOutcome last_failure{DnsUpstreamOutcome::Ok};
    std::string last_error;  // text of the last failure
};

struct DnsListenerStats {
    std::uint16_t port{0};
    std::uint64_t queries_udp{0};
    std::uint64_t queries_tcp{0};
    std::uint64_t dropped_overload_udp{0};
    std::uint64_t dropped_overload_tcp{0};
    std::uint64_t dropped_malformed_udp{0};
    std::uint64_t dropped_malformed_tcp{0};
    std::uint64_t responses_upstream{0};  // an upstream's answer went to the client
    std::uint64_t responses_servfail{0};  // no upstream answered: the forwarder's SERVFAIL
    std::vector<DnsUpstreamStats> upstreams;
};

struct DnsBalancerStats {
    std::vector<DnsListenerStats> listeners;
    std::uint32_t inflight_udp{0};
    std::uint32_t inflight_tcp{0};
};

// One query to one DNS server, outside the groups (see DnsBalancer::probe).
struct DnsProbeRequest {
    DnsBalancerUpstream upstream;
    std::vector<std::uint8_t> query;  // a whole DNS message with the id to expect back
};

struct DnsProbeResult {
    DnsUpstreamOutcome outcome{DnsUpstreamOutcome::NetworkError};
    // False: the server cannot be asked at all (DoH not supported or without
    // CA certificates, a detour without a routing mark); `error` says why.
    bool usable{true};
    std::vector<std::uint8_t> answer;  // the DNS message, when one came back
    std::string error;                 // text for the user when there is no answer
    std::chrono::microseconds duration{0};
};

// A small forwarding DNS server for the lists that use several upstreams or a
// DoH one. Queries of a group go to its upstreams round-robin; an upstream
// that times out, refuses or answers SERVFAIL hands the query to the next one,
// a dead one is skipped for a while. The answer of the upstream is relayed
// unchanged; there is no cache and no record parsing. One thread with its own
// epoll serves every group over UDP and TCP and drives the DoH transfers; the
// slots of in-flight queries are preallocated, so a query does not allocate.
class DnsBalancer {
public:
    explicit DnsBalancer(DnsBalancerOptions options = {});
    ~DnsBalancer();
    DnsBalancer(const DnsBalancer&) = delete;
    DnsBalancer& operator=(const DnsBalancer&) = delete;

    // Makes `groups` the set of listeners: new ports are bound, missing ones
    // closed (their queries dropped), the upstreams of the rest replaced.
    // Blocks until applied (the thread starts with the first non-empty set)
    // and returns the ports that could not be bound. Thread-safe.
    std::vector<std::uint16_t> configure(const std::vector<DnsBalancerGroup>& groups,
                                         const DnsBalancerSettings& settings = {});

    // The set applied last.
    std::vector<DnsBalancerGroup> groups() const;

    // Sends each query to its server and waits for all the answers, but at
    // most `deadline`. The queries run on the forwarder thread (which starts
    // for the purpose when no group needs it) next to the real traffic and
    // have no effect on the statistics, on which servers are skipped, or on
    // the health verdict. Plain servers get UDP; the answer is judged by its
    // header, a truncated one is not retried over TCP. `settings` say what
    // DoH needs (CA certificates, marks of the detours, the server that looks
    // the DoH hosts up); what the groups were configured with stays in
    // effect. One call at a time, others wait. Thread-safe.
    std::vector<DnsProbeResult> probe(const std::vector<DnsProbeRequest>& requests,
                                      std::chrono::milliseconds deadline,
                                      const DnsBalancerSettings& settings);

    // A snapshot of the counters, taken by the forwarder thread (the counters
    // belong to it). Waits for it at most 500 ms, then returns the previous
    // snapshot. Thread-safe.
    DnsBalancerStats stats();

    // Closes every listener and joins the thread. Idempotent.
    void stop();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace keen_pbr3
