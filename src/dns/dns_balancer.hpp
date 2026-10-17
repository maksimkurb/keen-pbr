#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace keen_pbr3 {

// The built-in DNS balancer listens on this loopback address, one port per
// group of upstreams (UDP and TCP). Ports are kDnsBalancerBasePort + the
// position of the group, so the range is
// [kDnsBalancerBasePort, kDnsBalancerBasePort + kDnsBalancerMaxGroups).
constexpr const char* kDnsBalancerAddress = "127.0.0.89";
constexpr std::uint16_t kDnsBalancerBasePort = 5380;
constexpr std::size_t kDnsBalancerMaxGroups = 32;

struct DnsBalancerUpstream {
    std::string ip;           // IPv4 literal
    std::uint16_t port{53};
};

struct DnsBalancerGroup {
    std::uint16_t port{0};    // listen port on the balancer address
    std::vector<DnsBalancerUpstream> upstreams;
};

inline bool operator==(const DnsBalancerUpstream& a, const DnsBalancerUpstream& b) {
    return a.ip == b.ip && a.port == b.port;
}
inline bool operator==(const DnsBalancerGroup& a, const DnsBalancerGroup& b) {
    return a.port == b.port && a.upstreams == b.upstreams;
}

struct DnsBalancerOptions {
    std::string listen_ip{kDnsBalancerAddress};
    // How long one upstream gets to answer before the next one is tried.
    std::chrono::milliseconds udp_attempt_timeout{1500};
    std::chrono::milliseconds tcp_attempt_timeout{2500};
    // An upstream that failed is skipped this long (unless all of them failed).
    std::chrono::milliseconds dead_for{10000};
    // A TCP client connection that does nothing is closed after this.
    std::chrono::milliseconds tcp_idle{10000};
};

// A small forwarding DNS balancer for the stub upstreams of the Keenetic
// built-in DNS. Queries of a group go to its upstreams round-robin; an
// upstream that times out, refuses or answers SERVFAIL hands the query to the
// next one, a dead one is skipped for a while. The answer of the upstream is
// relayed unchanged; there is no cache and no record parsing. One thread with
// its own epoll serves every group over UDP and TCP; the slots of in-flight
// queries are preallocated, so a query does not allocate.
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
    std::vector<std::uint16_t> configure(const std::vector<DnsBalancerGroup>& groups);

    // The set applied last.
    std::vector<DnsBalancerGroup> groups() const;

    // Closes every listener and joins the thread. Idempotent.
    void stop();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace keen_pbr3
