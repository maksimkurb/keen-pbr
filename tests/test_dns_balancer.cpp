#include <doctest/doctest.h>

#include "../src/dns/dns_balancer.hpp"
#include "../src/dns/dns_client.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <cstring>
#include <thread>
#include <vector>

using namespace keen_pbr3;

namespace {

using Bytes = std::vector<std::uint8_t>;

sockaddr_in loopback(std::uint16_t port, const char* ip = "127.0.0.1") {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    ::inet_pton(AF_INET, ip, &addr.sin_addr);
    return addr;
}

// A DNS server on 127.0.0.1 (UDP and TCP) that answers, stays silent or
// answers SERVFAIL, and counts the queries it got.
class FakeDns {
public:
    enum class Mode { Answer, Silent, Servfail, Nxdomain };

    FakeDns(std::uint16_t port, Mode mode) : mode_(mode) {
        const sockaddr_in addr = loopback(port);
        udp_ = ::socket(AF_INET, SOCK_DGRAM, 0);
        tcp_ = ::socket(AF_INET, SOCK_STREAM, 0);
        const int one = 1;
        ::setsockopt(tcp_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        REQUIRE(::bind(udp_, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) == 0);
        REQUIRE(::bind(tcp_, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) == 0);
        REQUIRE(::listen(tcp_, 4) == 0);
        thread_ = std::thread([this] { run(); });
    }

    ~FakeDns() {
        stop_.store(true);
        thread_.join();
        ::close(udp_);
        ::close(tcp_);
    }

    int udp_hits() const { return udp_hits_.load(); }
    int tcp_hits() const { return tcp_hits_.load(); }

private:
    Bytes answer_for(const std::uint8_t* query, std::size_t len) const {
        Bytes out(query, query + len);
        out[2] = static_cast<std::uint8_t>(out[2] | 0x80);
        const int rcode = mode_ == Mode::Servfail ? 2 : (mode_ == Mode::Nxdomain ? 3 : 0);
        out[3] = static_cast<std::uint8_t>((out[3] & 0xF0) | 0x80 | rcode);
        return out;
    }

    void run() {
        while (!stop_.load()) {
            pollfd fds[2] = {{udp_, POLLIN, 0}, {tcp_, POLLIN, 0}};
            if (::poll(fds, 2, 20) <= 0) continue;
            if ((fds[0].revents & POLLIN) != 0) {
                std::uint8_t buf[2048];
                sockaddr_in from{};
                socklen_t from_len = sizeof(from);
                const ssize_t n = ::recvfrom(udp_, buf, sizeof(buf), 0,
                                             reinterpret_cast<sockaddr*>(&from), &from_len);
                if (n >= 12) {
                    ++udp_hits_;
                    if (mode_ != Mode::Silent) {
                        const Bytes answer = answer_for(buf, static_cast<std::size_t>(n));
                        ::sendto(udp_, answer.data(), answer.size(), 0,
                                 reinterpret_cast<const sockaddr*>(&from), from_len);
                    }
                }
            }
            if ((fds[1].revents & POLLIN) != 0) {
                const int conn = ::accept(tcp_, nullptr, nullptr);
                if (conn >= 0) {
                    serve_tcp(conn);
                    ::close(conn);
                }
            }
        }
    }

    void serve_tcp(int conn) {
        timeval timeout{1, 0};
        ::setsockopt(conn, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        std::uint8_t head[2];
        if (::recv(conn, head, 2, MSG_WAITALL) != 2) return;
        const std::size_t len = static_cast<std::size_t>((head[0] << 8) | head[1]);
        Bytes query(len);
        if (::recv(conn, query.data(), len, MSG_WAITALL) != static_cast<ssize_t>(len)) return;
        ++tcp_hits_;
        if (mode_ == Mode::Silent) {
            std::this_thread::sleep_for(std::chrono::milliseconds(600));
            return;
        }
        const Bytes answer = answer_for(query.data(), len);
        Bytes framed{static_cast<std::uint8_t>(answer.size() >> 8),
                     static_cast<std::uint8_t>(answer.size() & 0xFF)};
        framed.insert(framed.end(), answer.begin(), answer.end());
        ::send(conn, framed.data(), framed.size(), MSG_NOSIGNAL);
    }

    Mode mode_;
    int udp_{-1};
    int tcp_{-1};
    std::atomic<bool> stop_{false};
    std::atomic<int> udp_hits_{0};
    std::atomic<int> tcp_hits_{0};
    std::thread thread_;
};

DnsBalancerOptions fast_options() {
    DnsBalancerOptions options;
    options.listen_ip = "127.0.0.1";
    options.udp_attempt_timeout = std::chrono::milliseconds(150);
    options.tcp_attempt_timeout = std::chrono::milliseconds(250);
    options.dead_for = std::chrono::seconds(30);
    return options;
}

DnsBalancerGroup group_of(std::uint16_t port, std::initializer_list<std::uint16_t> upstream_ports) {
    DnsBalancerGroup group;
    group.port = port;
    for (const auto up : upstream_ports) group.upstreams.push_back(DnsBalancerUpstream::plain("127.0.0.1", up));
    return group;
}

Bytes query(std::uint16_t id) {
    return build_dns_query(id, "example.com", kDnsTypeA, kDnsClassIN, false);
}

// Sends over UDP and waits for the answer; empty on timeout.
Bytes udp_ask(std::uint16_t port, std::uint16_t id, int wait_ms = 3000) {
    const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    const sockaddr_in to = loopback(port);
    const Bytes q = query(id);
    ::sendto(fd, q.data(), q.size(), 0, reinterpret_cast<const sockaddr*>(&to), sizeof(to));
    Bytes out;
    pollfd pfd{fd, POLLIN, 0};
    if (::poll(&pfd, 1, wait_ms) > 0) {
        out.resize(2048);
        const ssize_t n = ::recv(fd, out.data(), out.size(), 0);
        out.resize(n > 0 ? static_cast<std::size_t>(n) : 0);
    }
    ::close(fd);
    return out;
}

int tcp_connect(std::uint16_t port) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    const sockaddr_in to = loopback(port);
    if (::connect(fd, reinterpret_cast<const sockaddr*>(&to), sizeof(to)) != 0) {
        ::close(fd);
        return -1;
    }
    timeval timeout{3, 0};
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    return fd;
}

Bytes tcp_ask(int fd, std::uint16_t id) {
    const Bytes q = query(id);
    Bytes framed{static_cast<std::uint8_t>(q.size() >> 8), static_cast<std::uint8_t>(q.size() & 0xFF)};
    framed.insert(framed.end(), q.begin(), q.end());
    ::send(fd, framed.data(), framed.size(), MSG_NOSIGNAL);
    std::uint8_t head[2];
    if (::recv(fd, head, 2, MSG_WAITALL) != 2) return {};
    Bytes out(static_cast<std::size_t>((head[0] << 8) | head[1]));
    if (::recv(fd, out.data(), out.size(), MSG_WAITALL) != static_cast<ssize_t>(out.size())) return {};
    return out;
}

int rcode_of(const Bytes& answer) { return answer.size() < 4 ? -1 : (answer[3] & 0x0F); }
std::uint16_t id_of(const Bytes& answer) {
    return static_cast<std::uint16_t>((answer[0] << 8) | answer[1]);
}

constexpr std::uint16_t kListen = 29380;

} // namespace

TEST_CASE("dns balancer: UDP queries go round-robin and the answer is relayed unchanged") {
    FakeDns a(29401, FakeDns::Mode::Answer);
    FakeDns b(29402, FakeDns::Mode::Answer);
    DnsBalancer balancer(fast_options());
    CHECK(balancer.configure({group_of(kListen, {29401, 29402})}).empty());

    for (std::uint16_t i = 1; i <= 10; ++i) {
        const Bytes answer = udp_ask(kListen, i);
        REQUIRE(answer.size() > 12);
        CHECK(id_of(answer) == i);
        CHECK(rcode_of(answer) == 0);
        CHECK((answer[2] & 0x80) != 0);
    }
    CHECK(a.udp_hits() == 5);
    CHECK(b.udp_hits() == 5);
}

TEST_CASE("dns balancer: a silent UDP upstream fails over and is skipped afterwards") {
    FakeDns silent(29411, FakeDns::Mode::Silent);
    FakeDns alive(29412, FakeDns::Mode::Answer);
    DnsBalancer balancer(fast_options());
    CHECK(balancer.configure({group_of(kListen, {29411, 29412})}).empty());

    for (std::uint16_t i = 1; i <= 6; ++i) {
        const Bytes answer = udp_ask(kListen, i);
        REQUIRE(answer.size() > 12);
        CHECK(rcode_of(answer) == 0);
    }
    CHECK(silent.udp_hits() == 1);  // marked dead after its timeout
    CHECK(alive.udp_hits() == 6);
}

TEST_CASE("dns balancer: a refused UDP upstream fails over") {
    FakeDns alive(29422, FakeDns::Mode::Answer);
    DnsBalancer balancer(fast_options());
    // Nothing listens on 29421.
    CHECK(balancer.configure({group_of(kListen, {29421, 29422})}).empty());

    for (std::uint16_t i = 1; i <= 4; ++i) {
        const Bytes answer = udp_ask(kListen, i);
        REQUIRE(answer.size() > 12);
        CHECK(rcode_of(answer) == 0);
    }
    CHECK(alive.udp_hits() == 4);
}

TEST_CASE("dns balancer: a SERVFAIL from one upstream goes to the next") {
    FakeDns failing(29431, FakeDns::Mode::Servfail);
    FakeDns alive(29432, FakeDns::Mode::Answer);
    DnsBalancer balancer(fast_options());
    CHECK(balancer.configure({group_of(kListen, {29431, 29432})}).empty());

    for (std::uint16_t i = 1; i <= 4; ++i) {
        CHECK(rcode_of(udp_ask(kListen, i)) == 0);
    }
    CHECK(failing.udp_hits() == 2);  // asked when it was first in line
    CHECK(alive.udp_hits() == 4);
}

TEST_CASE("dns balancer: when every upstream fails the client gets SERVFAIL") {
    DnsBalancer balancer(fast_options());
    CHECK(balancer.configure({group_of(kListen, {29441, 29442})}).empty());

    const Bytes answer = udp_ask(kListen, 77);
    REQUIRE(answer.size() >= 12);
    CHECK(id_of(answer) == 77);
    CHECK(rcode_of(answer) == 2);

    // The dead upstreams are tried anyway when nothing else is left.
    CHECK(rcode_of(udp_ask(kListen, 78)) == 2);

    const int fd = tcp_connect(kListen);
    REQUIRE(fd >= 0);
    CHECK(rcode_of(tcp_ask(fd, 79)) == 2);
    ::close(fd);
}

TEST_CASE("dns balancer: TCP queries go round-robin over one client connection") {
    FakeDns a(29451, FakeDns::Mode::Answer);
    FakeDns b(29452, FakeDns::Mode::Answer);
    DnsBalancer balancer(fast_options());
    CHECK(balancer.configure({group_of(kListen, {29451, 29452})}).empty());

    const int fd = tcp_connect(kListen);
    REQUIRE(fd >= 0);
    for (std::uint16_t i = 1; i <= 6; ++i) {
        const Bytes answer = tcp_ask(fd, i);
        REQUIRE(answer.size() > 12);
        CHECK(id_of(answer) == i);
        CHECK(rcode_of(answer) == 0);
    }
    ::close(fd);
    CHECK(a.tcp_hits() == 3);
    CHECK(b.tcp_hits() == 3);
}

TEST_CASE("dns balancer: TCP fails over from a refused and from a silent upstream") {
    FakeDns silent(29461, FakeDns::Mode::Silent);
    FakeDns alive(29463, FakeDns::Mode::Answer);
    DnsBalancer balancer(fast_options());
    // 29462 refuses connections.
    CHECK(balancer.configure({group_of(kListen, {29461, 29462, 29463})}).empty());

    const int fd = tcp_connect(kListen);
    REQUIRE(fd >= 0);
    for (std::uint16_t i = 1; i <= 4; ++i) {
        const Bytes answer = tcp_ask(fd, i);
        REQUIRE(answer.size() > 12);
        CHECK(rcode_of(answer) == 0);
    }
    ::close(fd);
    CHECK(alive.tcp_hits() == 4);
    CHECK(silent.tcp_hits() == 1);
}

TEST_CASE("dns balancer: groups are independent and can be reconfigured") {
    FakeDns a(29471, FakeDns::Mode::Answer);
    FakeDns b(29472, FakeDns::Mode::Answer);
    DnsBalancer balancer(fast_options());
    CHECK(balancer.configure({group_of(kListen, {29471}), group_of(kListen + 1, {29472})}).empty());

    CHECK(rcode_of(udp_ask(kListen, 1)) == 0);
    CHECK(rcode_of(udp_ask(kListen + 1, 2)) == 0);
    CHECK(a.udp_hits() == 1);
    CHECK(b.udp_hits() == 1);

    // The second group moves to the first upstream; the other port closes.
    CHECK(balancer.configure({group_of(kListen, {29472})}).empty());
    CHECK(rcode_of(udp_ask(kListen, 3)) == 0);
    CHECK(b.udp_hits() == 2);
    CHECK(tcp_connect(kListen + 1) < 0);
    CHECK(balancer.groups().size() == 1);

    // An empty set closes everything and a later set starts again.
    CHECK(balancer.configure({}).empty());
    CHECK(tcp_connect(kListen) < 0);
    CHECK(balancer.configure({group_of(kListen, {29471})}).empty());
    CHECK(rcode_of(udp_ask(kListen, 4)) == 0);
    CHECK(a.udp_hits() == 2);
}

TEST_CASE("dns balancer: a port that cannot be bound is reported") {
    DnsBalancer first(fast_options());
    DnsBalancer second(fast_options());
    CHECK(first.configure({group_of(kListen, {29481})}).empty());
    const auto failed = second.configure({group_of(kListen, {29481}), group_of(kListen + 1, {29481})});
    REQUIRE(failed.size() == 1);
    CHECK(failed[0] == kListen);
}

TEST_CASE("dns balancer: stop is clean and idempotent") {
    DnsBalancer balancer(fast_options());
    balancer.stop();  // never started
    CHECK(balancer.configure({group_of(kListen, {29491})}).empty());
    balancer.stop();
    balancer.stop();
    CHECK(tcp_connect(kListen) < 0);
}

TEST_CASE("dns balancer: the default listen address is the loopback address 127.0.0.89") {
    CHECK(DnsBalancerOptions{}.listen_ip == "127.0.0.89");
    CHECK(config_defaults::dns_proxy_port_start + kDnsBalancerMaxGroups <= 65536);
}

TEST_CASE("dns balancer: a new listen address reopens the listeners there") {
    const auto connects = [](const char* ip, std::uint16_t port) {
        const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in to{};
        to.sin_family = AF_INET;
        to.sin_port = htons(port);
        ::inet_pton(AF_INET, ip, &to.sin_addr);
        const bool ok = ::connect(fd, reinterpret_cast<const sockaddr*>(&to), sizeof(to)) == 0;
        ::close(fd);
        return ok;
    };
    DnsBalancer balancer(fast_options());
    CHECK(balancer.configure({group_of(kListen, {29495})}).empty());
    CHECK(connects("127.0.0.1", kListen));
    DnsBalancerSettings moved;
    moved.listen_ip = "127.0.0.2";
    CHECK(balancer.configure({group_of(kListen, {29495})}, moved).empty());
    CHECK(connects("127.0.0.2", kListen));
    CHECK_FALSE(connects("127.0.0.1", kListen));
}

// --- statistics ---------------------------------------------------------------

namespace {

const DnsListenerStats* listener_stats(const DnsBalancerStats& stats, std::uint16_t port) {
    for (const auto& listener : stats.listeners) {
        if (listener.port == port) return &listener;
    }
    return nullptr;
}

const DnsUpstreamStats& upstream_stats(const DnsBalancerStats& stats, std::uint16_t port,
                                       std::size_t index) {
    const DnsListenerStats* listener = listener_stats(stats, port);
    REQUIRE(listener != nullptr);
    REQUIRE(index < listener->upstreams.size());
    return listener->upstreams[index];
}

std::uint64_t count_of(const DnsUpstreamStats& upstream, DnsUpstreamOutcome outcome) {
    return upstream.results[static_cast<std::size_t>(outcome)];
}

std::uint64_t latency_samples(const DnsUpstreamStats& upstream) {
    std::uint64_t total = 0;
    for (const auto count : upstream.latency_buckets) total += count;
    return total;
}

bool wait_until(const std::function<bool()>& done, int wait_ms = 3000) {
    const auto limit = std::chrono::steady_clock::now() + std::chrono::milliseconds(wait_ms);
    while (std::chrono::steady_clock::now() < limit) {
        if (done()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return done();
}

} // namespace

TEST_CASE("dns balancer stats: nothing before the thread runs, nothing after stop") {
    DnsBalancer balancer(fast_options());
    CHECK(balancer.stats().listeners.empty());
    CHECK(balancer.configure({group_of(kListen, {29501})}).empty());
    CHECK(balancer.stats().listeners.size() == 1);
    balancer.stop();
    CHECK(balancer.stats().listeners.empty());
}

TEST_CASE("dns balancer stats: answers, rcodes and timing are counted per upstream") {
    FakeDns ok(29511, FakeDns::Mode::Answer);
    FakeDns missing(29512, FakeDns::Mode::Nxdomain);
    DnsBalancer balancer(fast_options());
    auto group = group_of(kListen, {29511});
    group.upstreams[0].name = "main,backup";
    auto other = group_of(kListen + 1, {29512});
    CHECK(balancer.configure({group, other}).empty());

    for (std::uint16_t i = 1; i <= 3; ++i) CHECK(rcode_of(udp_ask(kListen, i)) == 0);
    const int fd = tcp_connect(kListen);
    REQUIRE(fd >= 0);
    CHECK(rcode_of(tcp_ask(fd, 10)) == 0);
    ::close(fd);
    for (std::uint16_t i = 1; i <= 2; ++i) CHECK(rcode_of(udp_ask(kListen + 1, i)) == 3);

    const auto stats = balancer.stats();
    const auto* first = listener_stats(stats, kListen);
    REQUIRE(first != nullptr);
    CHECK(first->queries_udp == 3);
    CHECK(first->queries_tcp == 1);
    CHECK(first->responses_upstream == 4);
    CHECK(first->responses_servfail == 0);
    const auto& up = upstream_stats(stats, kListen, 0);
    CHECK(up.upstream.name == "main,backup");
    CHECK(up.usable);
    CHECK(up.up);
    CHECK(count_of(up, DnsUpstreamOutcome::Ok) == 4);
    CHECK(latency_samples(up) == 4);
    CHECK(up.consecutive_failures == 0);

    // NXDOMAIN is an answer: it is timed and does not count as a failure.
    const auto& nx = upstream_stats(stats, kListen + 1, 0);
    CHECK(count_of(nx, DnsUpstreamOutcome::Nxdomain) == 2);
    CHECK(latency_samples(nx) == 2);
    CHECK(nx.consecutive_failures == 0);
    CHECK(nx.up);
}

TEST_CASE("dns balancer stats: failures are told apart") {
    FakeDns silent(29521, FakeDns::Mode::Silent);
    FakeDns servfail(29523, FakeDns::Mode::Servfail);
    FakeDns alive(29524, FakeDns::Mode::Answer);
    DnsBalancer balancer(fast_options());
    // 29522 refuses connections.
    CHECK(balancer.configure({group_of(kListen, {29521, 29522, 29523, 29524})}).empty());

    // The round-robin start moves on with every query; a few rounds visit every upstream.
    for (std::uint16_t i = 1; i <= 8; ++i) CHECK(rcode_of(udp_ask(kListen, i)) == 0);

    const auto stats = balancer.stats();
    const auto& timed_out = upstream_stats(stats, kListen, 0);
    CHECK(count_of(timed_out, DnsUpstreamOutcome::Timeout) == 1);
    CHECK_FALSE(timed_out.up);
    CHECK(timed_out.consecutive_failures == 1);
    CHECK(latency_samples(timed_out) == 0);
    const auto& refused = upstream_stats(stats, kListen, 1);
    CHECK(count_of(refused, DnsUpstreamOutcome::ConnectionRefused) == 1);
    CHECK_FALSE(refused.up);
    CHECK(refused.last_error.size() > 0);
    // SERVFAIL hands the query on, is counted, and is a failure for the streak.
    const auto& failing = upstream_stats(stats, kListen, 2);
    CHECK(count_of(failing, DnsUpstreamOutcome::Servfail) >= 1);
    CHECK(failing.consecutive_failures >= 1);
    CHECK(failing.last_failure == DnsUpstreamOutcome::Servfail);
    CHECK(failing.up);  // a SERVFAIL does not put the server aside
    CHECK(upstream_stats(stats, kListen, 3).consecutive_failures == 0);
}

TEST_CASE("dns balancer stats: TCP failures are counted too") {
    FakeDns silent(29531, FakeDns::Mode::Silent);
    FakeDns alive(29533, FakeDns::Mode::Answer);
    DnsBalancer balancer(fast_options());
    CHECK(balancer.configure({group_of(kListen, {29531, 29532, 29533})}).empty());

    const int fd = tcp_connect(kListen);
    REQUIRE(fd >= 0);
    for (std::uint16_t i = 1; i <= 4; ++i) CHECK(rcode_of(tcp_ask(fd, i)) == 0);
    ::close(fd);

    const auto stats = balancer.stats();
    CHECK(count_of(upstream_stats(stats, kListen, 0), DnsUpstreamOutcome::Timeout) == 1);
    CHECK(count_of(upstream_stats(stats, kListen, 1), DnsUpstreamOutcome::ConnectionRefused) == 1);
    CHECK(count_of(upstream_stats(stats, kListen, 2), DnsUpstreamOutcome::Ok) == 4);
}

TEST_CASE("dns balancer stats: the proxy's own SERVFAIL is counted") {
    DnsBalancer balancer(fast_options());
    CHECK(balancer.configure({group_of(kListen, {29541, 29542})}).empty());
    CHECK(rcode_of(udp_ask(kListen, 1)) == 2);
    const int fd = tcp_connect(kListen);
    REQUIRE(fd >= 0);
    CHECK(rcode_of(tcp_ask(fd, 2)) == 2);
    ::close(fd);

    const auto stats = balancer.stats();
    const auto* listener = listener_stats(stats, kListen);
    REQUIRE(listener != nullptr);
    CHECK(listener->responses_servfail == 2);
    CHECK(listener->responses_upstream == 0);
}

TEST_CASE("dns balancer stats: queries that cannot be served are dropped and counted") {
    FakeDns silent(29551, FakeDns::Mode::Silent);
    DnsBalancerOptions options = fast_options();
    options.udp_attempt_timeout = std::chrono::milliseconds(600);
    DnsBalancer balancer(options);
    CHECK(balancer.configure({group_of(kListen, {29551})}).empty());

    const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    const sockaddr_in to = loopback(kListen);
    const std::uint8_t junk[5] = {1, 2, 3, 4, 5};
    ::sendto(fd, junk, sizeof(junk), 0, reinterpret_cast<const sockaddr*>(&to), sizeof(to));
    const Bytes q = query(1);
    constexpr int kSent = 140;  // more than the slots for in-flight queries
    for (int i = 0; i < kSent; ++i) {
        ::sendto(fd, q.data(), q.size(), 0, reinterpret_cast<const sockaddr*>(&to), sizeof(to));
    }
    std::uint32_t inflight = 0;
    CHECK(wait_until([&] {
        const auto snapshot = balancer.stats();
        inflight = std::max(inflight, snapshot.inflight_udp);
        const auto* l = listener_stats(snapshot, kListen);
        return l != nullptr && l->queries_udp == kSent + 1;
    }));
    ::close(fd);

    const auto* listener = listener_stats(balancer.stats(), kListen);
    REQUIRE(listener != nullptr);
    CHECK(listener->dropped_malformed_udp == 1);
    CHECK(listener->dropped_overload_udp > 0);
    CHECK(inflight > 0);
}

TEST_CASE("dns balancer stats: counters survive a reconfigure of the same upstream") {
    FakeDns a(29561, FakeDns::Mode::Answer);
    FakeDns b(29562, FakeDns::Mode::Answer);
    DnsBalancer balancer(fast_options());
    CHECK(balancer.configure({group_of(kListen, {29561, 29562})}).empty());
    for (std::uint16_t i = 1; i <= 4; ++i) CHECK(rcode_of(udp_ask(kListen, i)) == 0);

    // The same upstreams under another name keep their counters; the new one starts at zero.
    auto renamed = group_of(kListen, {29562, 29563});
    renamed.upstreams[0].name = "renamed";
    CHECK(balancer.configure({renamed}).empty());
    auto stats = balancer.stats();
    CHECK(count_of(upstream_stats(stats, kListen, 0), DnsUpstreamOutcome::Ok) == 2);
    CHECK(upstream_stats(stats, kListen, 0).upstream.name == "renamed");
    CHECK(count_of(upstream_stats(stats, kListen, 1), DnsUpstreamOutcome::Ok) == 0);
    CHECK(listener_stats(stats, kListen)->queries_udp == 4);

    // A group that goes away takes its series with it.
    CHECK(balancer.configure({group_of(kListen + 1, {29562})}).empty());
    stats = balancer.stats();
    CHECK(listener_stats(stats, kListen) == nullptr);
    CHECK(listener_stats(stats, kListen + 1)->queries_udp == 0);
}

TEST_CASE("dns balancer stats: ten failures in a row make a server failing, an answer ends it") {
    std::atomic<int> changes{0};
    DnsBalancerOptions options = fast_options();
    options.on_health_change = [&] { ++changes; };
    DnsBalancer balancer(options);
    // Nothing listens on 29571 yet.
    CHECK(balancer.configure({group_of(kListen, {29571})}).empty());

    for (std::uint16_t i = 1; i <= 9; ++i) CHECK(rcode_of(udp_ask(kListen, i)) == 2);
    CHECK(upstream_stats(balancer.stats(), kListen, 0).consecutive_failures == 9);
    CHECK(changes.load() == 0);
    CHECK(rcode_of(udp_ask(kListen, 10)) == 2);
    CHECK(wait_until([&] { return changes.load() == 1; }));
    const auto failing = upstream_stats(balancer.stats(), kListen, 0);
    CHECK(failing.consecutive_failures == 10);
    CHECK(failing.last_failure == DnsUpstreamOutcome::ConnectionRefused);

    // More failures change nothing.
    CHECK(rcode_of(udp_ask(kListen, 11)) == 2);
    CHECK(changes.load() == 1);

    // The server comes back: its first answer ends the streak.
    FakeDns revived(29571, FakeDns::Mode::Answer);
    CHECK(rcode_of(udp_ask(kListen, 12)) == 0);
    CHECK(wait_until([&] { return changes.load() == 2; }));
    CHECK(upstream_stats(balancer.stats(), kListen, 0).consecutive_failures == 0);
}

TEST_CASE("dns balancer stats: SERVFAIL answers count as failures of the server") {
    FakeDns bad(29581, FakeDns::Mode::Servfail);
    std::atomic<int> changes{0};
    DnsBalancerOptions options = fast_options();
    options.on_health_change = [&] { ++changes; };
    DnsBalancer balancer(options);
    CHECK(balancer.configure({group_of(kListen, {29581})}).empty());
    for (std::uint16_t i = 1; i <= 10; ++i) CHECK(rcode_of(udp_ask(kListen, i)) == 2);
    CHECK(wait_until([&] { return changes.load() == 1; }));
    CHECK(upstream_stats(balancer.stats(), kListen, 0).last_failure == DnsUpstreamOutcome::Servfail);
}

TEST_CASE("dns balancer stats: the streak of a server is shared by the groups it is in") {
    DnsBalancer balancer(fast_options());
    CHECK(balancer.configure({group_of(kListen, {29591}), group_of(kListen + 1, {29591})}).empty());
    for (std::uint16_t i = 1; i <= 3; ++i) CHECK(rcode_of(udp_ask(kListen, i)) == 2);
    for (std::uint16_t i = 1; i <= 2; ++i) CHECK(rcode_of(udp_ask(kListen + 1, i)) == 2);
    const auto stats = balancer.stats();
    CHECK(upstream_stats(stats, kListen, 0).consecutive_failures == 5);
    CHECK(upstream_stats(stats, kListen + 1, 0).consecutive_failures == 5);
}

// --- probes ---------------------------------------------------------------------

namespace {

DnsProbeRequest probe_request(std::uint16_t port, std::uint16_t id) {
    DnsProbeRequest request;
    request.upstream = DnsBalancerUpstream::plain("127.0.0.1", port);
    request.query = query(id);
    return request;
}

} // namespace

TEST_CASE("dns balancer probe: answers, rcodes and silence of plain servers, all at once") {
    FakeDns ok(29601, FakeDns::Mode::Answer);
    FakeDns missing(29602, FakeDns::Mode::Nxdomain);
    FakeDns failing(29603, FakeDns::Mode::Servfail);
    FakeDns silent(29604, FakeDns::Mode::Silent);
    // Nothing listens on 29605.
    DnsBalancer balancer(fast_options());  // no groups: the probe starts the thread

    const auto started = std::chrono::steady_clock::now();
    const auto results = balancer.probe({probe_request(29601, 11), probe_request(29602, 12),
                                         probe_request(29603, 13), probe_request(29604, 14),
                                         probe_request(29605, 15)},
                                        std::chrono::seconds(5), {});
    const auto took = std::chrono::steady_clock::now() - started;
    REQUIRE(results.size() == 5);
    CHECK(results[0].outcome == DnsUpstreamOutcome::Ok);
    REQUIRE(results[0].answer.size() > 12);
    CHECK(id_of(results[0].answer) == 11);
    CHECK(results[0].usable);
    CHECK(results[0].duration.count() > 0);
    CHECK(results[1].outcome == DnsUpstreamOutcome::Nxdomain);
    CHECK(rcode_of(results[1].answer) == 3);
    // A SERVFAIL is reported as it is, not retried elsewhere.
    CHECK(results[2].outcome == DnsUpstreamOutcome::Servfail);
    CHECK(results[3].outcome == DnsUpstreamOutcome::Timeout);
    CHECK(results[3].answer.empty());
    CHECK(results[4].outcome == DnsUpstreamOutcome::ConnectionRefused);
    CHECK_FALSE(results[4].error.empty());
    // They ran side by side: about one attempt timeout, not the sum.
    CHECK(took < std::chrono::milliseconds(1000));
}

TEST_CASE("dns balancer probe: the deadline cuts a slow server short") {
    FakeDns silent(29611, FakeDns::Mode::Silent);
    DnsBalancerOptions options = fast_options();
    options.udp_attempt_timeout = std::chrono::seconds(5);
    DnsBalancer balancer(options);
    const auto started = std::chrono::steady_clock::now();
    const auto results = balancer.probe({probe_request(29611, 1)}, std::chrono::milliseconds(200), {});
    const auto took = std::chrono::steady_clock::now() - started;
    REQUIRE(results.size() == 1);
    CHECK(results[0].outcome == DnsUpstreamOutcome::Timeout);
    CHECK(took < std::chrono::milliseconds(1500));
}

TEST_CASE("dns balancer probe: more questions than slots are all answered") {
    FakeDns ok(29621, FakeDns::Mode::Answer);
    DnsBalancer balancer(fast_options());
    std::vector<DnsProbeRequest> requests;
    for (std::uint16_t i = 1; i <= 150; ++i) requests.push_back(probe_request(29621, i));
    const auto results = balancer.probe(requests, std::chrono::seconds(5), {});
    REQUIRE(results.size() == 150);
    for (std::size_t i = 0; i < results.size(); ++i) {
        CAPTURE(i);
        CHECK(results[i].outcome == DnsUpstreamOutcome::Ok);
    }
    CHECK(balancer.probe({}, std::chrono::seconds(1), {}).empty());
}

TEST_CASE("dns balancer probe: no effect on counters, skipped servers or the streak") {
    FakeDns silent(29631, FakeDns::Mode::Silent);
    FakeDns alive(29632, FakeDns::Mode::Answer);
    std::atomic<int> changes{0};
    DnsBalancerOptions options = fast_options();
    options.on_health_change = [&] { ++changes; };
    DnsBalancer balancer(options);
    CHECK(balancer.configure({group_of(kListen, {29631, 29632})}).empty());
    const auto before = balancer.stats();

    for (int round = 0; round < 12; ++round) {
        const auto results =
            balancer.probe({probe_request(29631, 1), probe_request(29633, 2), probe_request(29632, 3)},
                           std::chrono::seconds(3), {});
        REQUIRE(results.size() == 3);
        CHECK(results[0].outcome == DnsUpstreamOutcome::Timeout);
    }
    const auto after = balancer.stats();
    for (std::size_t i = 0; i < 2; ++i) {
        const auto& was = upstream_stats(before, kListen, i);
        const auto& now = upstream_stats(after, kListen, i);
        CHECK(now.results == was.results);
        CHECK(now.consecutive_failures == was.consecutive_failures);
        CHECK(now.up == was.up);
    }
    CHECK(changes.load() == 0);
    // The group keeps working next to the probes.
    CHECK(rcode_of(udp_ask(kListen, 7)) == 0);
}

TEST_CASE("dns balancer probe: DoH is unusable without settings for it") {
    DnsBalancer balancer(fast_options());
    DnsProbeRequest request;
    request.upstream = DnsBalancerUpstream::doh("https://dns.example/dns-query");
    request.query = query(1);
    const auto results = balancer.probe({request}, std::chrono::seconds(1), {});
    REQUIRE(results.size() == 1);
    CHECK_FALSE(results[0].usable);
    CHECK_FALSE(results[0].error.empty());
}

TEST_CASE("dns balancer probe: stop with nothing pending, probe again after it") {
    FakeDns ok(29641, FakeDns::Mode::Answer);
    DnsBalancer balancer(fast_options());
    CHECK(balancer.probe({probe_request(29641, 1)}, std::chrono::seconds(2), {})[0].outcome ==
          DnsUpstreamOutcome::Ok);
    balancer.stop();
    CHECK(balancer.probe({probe_request(29641, 2)}, std::chrono::seconds(2), {})[0].outcome ==
          DnsUpstreamOutcome::Ok);
}
