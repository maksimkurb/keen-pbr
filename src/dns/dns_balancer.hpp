#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace keen_pbr3 {

// The local DNS forwarder listens on this loopback address, one port per set
// of upstreams (UDP and TCP). Ports are kDnsBalancerBasePort + the position of
// the set, so the range is
// [kDnsBalancerBasePort, kDnsBalancerBasePort + kDnsBalancerMaxGroups).
constexpr const char* kDnsBalancerAddress = "127.0.0.89";
constexpr std::uint16_t kDnsBalancerBasePort = 5380;
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

    static DnsBalancerUpstream plain(std::string ip, std::uint16_t port = 53) {
        DnsBalancerUpstream upstream;
        upstream.ip = std::move(ip);
        upstream.port = port;
        return upstream;
    }
    static DnsBalancerUpstream doh(std::string url, std::string detour = {}) {
        DnsBalancerUpstream upstream;
        upstream.kind = DnsBalancerUpstreamKind::Doh;
        upstream.url = std::move(url);
        upstream.detour = std::move(detour);
        return upstream;
    }
};

struct DnsBalancerGroup {
    std::uint16_t port{0};    // listen port on the forwarder address
    std::vector<DnsBalancerUpstream> upstreams;  // in the order they are tried
};

inline bool operator==(const DnsBalancerUpstream& a, const DnsBalancerUpstream& b) {
    return a.kind == b.kind && a.ip == b.ip && a.port == b.port && a.url == b.url &&
           a.detour == b.detour;
}
inline bool operator==(const DnsBalancerGroup& a, const DnsBalancerGroup& b) {
    return a.port == b.port && a.upstreams == b.upstreams;
}

// Where the forwarder gets what its upstreams cannot know on their own.
struct DnsBalancerSettings {
    // Firewall mark of a DoH connection, by the outbound tag of `detour`.
    std::map<std::string, std::uint32_t> detour_marks;
    // The server that resolves the host names of DoH URLs; unset: the system
    // resolver. It must never be the forwarder itself.
    std::optional<DnsBalancerUpstream> bootstrap;
    // False: DoH upstreams are not used (no DoH in this build, no CA
    // certificates); a group of only such upstreams answers SERVFAIL.
    bool doh_enabled{false};
    // Trust anchors for DoH: a file with certificates or a directory of them.
    std::string ca_path;
    bool ca_is_directory{false};
};

inline bool operator==(const DnsBalancerSettings& a, const DnsBalancerSettings& b) {
    return a.detour_marks == b.detour_marks && a.bootstrap == b.bootstrap &&
           a.doh_enabled == b.doh_enabled && a.ca_path == b.ca_path &&
           a.ca_is_directory == b.ca_is_directory;
}

struct DnsBalancerOptions {
    std::string listen_ip{kDnsBalancerAddress};
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

    // Closes every listener and joins the thread. Idempotent.
    void stop();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace keen_pbr3
