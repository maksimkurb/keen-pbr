#include <doctest/doctest.h>

#include "../src/dns/dns_balancer.hpp"
#include "../src/dns/dns_client.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
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
    enum class Mode { Answer, Silent, Servfail };

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
        out[3] = static_cast<std::uint8_t>((out[3] & 0xF0) | 0x80 |
                                           (mode_ == Mode::Servfail ? 2 : 0));
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
