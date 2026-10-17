#include <doctest/doctest.h>

#include "../src/dns/dns_balancer.hpp"
#include "../src/dns/dns_client.hpp"
#include "../src/dns/doh_bootstrap.hpp"
#include "../src/dns/doh_client.hpp"
#include "../src/dns/doh_support.hpp"
#include "../src/http/curl_runtime.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

using namespace keen_pbr3;

namespace {

using Bytes = std::vector<std::uint8_t>;
using namespace std::chrono_literals;

// A DoH server over plain HTTP/1.1 on 127.0.0.1 (the client is told to allow
// http:// in tests): POST /dns-query with application/dns-message, keep-alive.
class FakeDohServer {
public:
    enum class Mode { Answer, Error500, ShortBody, Silent, Servfail };

    explicit FakeDohServer(Mode mode = Mode::Answer) : mode_(mode) {
        listen_ = ::socket(AF_INET, SOCK_STREAM, 0);
        const int one = 1;
        ::setsockopt(listen_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        REQUIRE(::bind(listen_, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) == 0);
        socklen_t len = sizeof(addr);
        ::getsockname(listen_, reinterpret_cast<sockaddr*>(&addr), &len);
        port_ = ntohs(addr.sin_port);
        REQUIRE(::listen(listen_, 8) == 0);
        thread_ = std::thread([this] { run(); });
    }

    ~FakeDohServer() {
        stop_.store(true);
        thread_.join();
        for (const int fd : clients_) ::close(fd);
        ::close(listen_);
    }

    std::uint16_t port() const { return port_; }
    std::string url() const { return "http://127.0.0.1:" + std::to_string(port_) + "/dns-query"; }
    int requests() const { return requests_.load(); }
    int connections() const { return connections_.load(); }
    bool saw_zero_id() const { return zero_id_.load(); }
    bool saw_dns_content_type() const { return content_type_ok_.load(); }
    void set_mode(Mode mode) { mode_.store(mode); }

private:
    struct Client {
        int fd;
        std::string buffer;
    };

    void run() {
        std::vector<Client> clients;
        while (!stop_.load()) {
            std::vector<pollfd> fds{{listen_, POLLIN, 0}};
            for (const auto& client : clients) fds.push_back({client.fd, POLLIN, 0});
            if (::poll(fds.data(), fds.size(), 20) <= 0) continue;
            if ((fds[0].revents & POLLIN) != 0) {
                const int fd = ::accept(listen_, nullptr, nullptr);
                if (fd >= 0) {
                    ++connections_;
                    clients.push_back({fd, {}});
                    clients_.push_back(fd);
                }
            }
            for (std::size_t i = 0; i + 1 < fds.size(); ++i) {
                if ((fds[i + 1].revents & (POLLIN | POLLHUP)) == 0) continue;
                char chunk[4096];
                const ssize_t n = ::recv(clients[i].fd, chunk, sizeof(chunk), 0);
                if (n <= 0) {
                    ::close(clients[i].fd);
                    clients_.erase(std::find(clients_.begin(), clients_.end(), clients[i].fd));
                    clients[i].fd = -1;
                    continue;
                }
                clients[i].buffer.append(chunk, static_cast<std::size_t>(n));
                while (handle_one(clients[i])) {
                }
            }
            clients.erase(std::remove_if(clients.begin(), clients.end(),
                                         [](const Client& c) { return c.fd < 0; }),
                          clients.end());
        }
    }

    // Answers one complete request in the buffer; false when none is complete.
    bool handle_one(Client& client) {
        const auto head_end = client.buffer.find("\r\n\r\n");
        if (head_end == std::string::npos) return false;
        const std::string head = client.buffer.substr(0, head_end);
        std::size_t length = 0;
        const auto cl = head.find("ontent-Length: ");
        if (cl != std::string::npos) length = std::stoul(head.substr(cl + 15));
        if (client.buffer.size() < head_end + 4 + length) return false;
        const std::string body = client.buffer.substr(head_end + 4, length);
        client.buffer.erase(0, head_end + 4 + length);

        ++requests_;
        content_type_ok_.store(head.find("application/dns-message") != std::string::npos &&
                               head.rfind("POST /dns-query", 0) == 0);
        zero_id_.store(body.size() >= 12 && body[0] == 0 && body[1] == 0);

        const Mode mode = mode_.load();
        if (mode == Mode::Silent) return true;
        std::string reply_body;
        std::string status = "200 OK";
        if (mode == Mode::Error500) {
            status = "500 Internal Server Error";
        } else if (mode == Mode::ShortBody) {
            reply_body = "xx";
        } else {
            reply_body = body;
            if (reply_body.size() >= 12) {
                reply_body[2] = static_cast<char>(reply_body[2] | 0x80);
                reply_body[3] = static_cast<char>((reply_body[3] & 0xF0) | 0x80 |
                                                  (mode == Mode::Servfail ? 2 : 0));
            }
        }
        const std::string response = "HTTP/1.1 " + status + "\r\nContent-Type: application/dns-message\r\n" +
                                     "Content-Length: " + std::to_string(reply_body.size()) +
                                     "\r\n\r\n" + reply_body;
        ::send(client.fd, response.data(), response.size(), MSG_NOSIGNAL);
        return true;
    }

    std::atomic<Mode> mode_;
    int listen_{-1};
    std::uint16_t port_{0};
    std::vector<int> clients_;
    std::atomic<bool> stop_{false};
    std::atomic<int> requests_{0};
    std::atomic<int> connections_{0};
    std::atomic<bool> zero_id_{false};
    std::atomic<bool> content_type_ok_{false};
    std::thread thread_;
};

// A plain DNS server on 127.0.0.1 (UDP) that answers every query.
class FakeUdpDns {
public:
    explicit FakeUdpDns(bool servfail = false) : servfail_(servfail) {
        fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        REQUIRE(::bind(fd_, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) == 0);
        socklen_t len = sizeof(addr);
        ::getsockname(fd_, reinterpret_cast<sockaddr*>(&addr), &len);
        port_ = ntohs(addr.sin_port);
        thread_ = std::thread([this] {
            while (!stop_.load()) {
                pollfd pfd{fd_, POLLIN, 0};
                if (::poll(&pfd, 1, 20) <= 0) continue;
                std::uint8_t buf[2048];
                sockaddr_in from{};
                socklen_t from_len = sizeof(from);
                const ssize_t n = ::recvfrom(fd_, buf, sizeof(buf), 0,
                                             reinterpret_cast<sockaddr*>(&from), &from_len);
                if (n < 12) continue;
                ++hits_;
                buf[2] = static_cast<std::uint8_t>(buf[2] | 0x80);
                buf[3] = static_cast<std::uint8_t>((buf[3] & 0xF0) | 0x80 | (servfail_ ? 2 : 0));
                ::sendto(fd_, buf, static_cast<std::size_t>(n), 0,
                         reinterpret_cast<const sockaddr*>(&from), from_len);
            }
        });
    }
    ~FakeUdpDns() {
        stop_.store(true);
        thread_.join();
        ::close(fd_);
    }
    std::uint16_t port() const { return port_; }
    int hits() const { return hits_.load(); }

private:
    bool servfail_;
    int fd_{-1};
    std::uint16_t port_{0};
    std::atomic<bool> stop_{false};
    std::atomic<int> hits_{0};
    std::thread thread_;
};

DnsBalancerOptions doh_options() {
    DnsBalancerOptions options;
    options.listen_ip = "127.0.0.1";
    options.udp_attempt_timeout = 200ms;
    options.tcp_attempt_timeout = 300ms;
    options.doh_attempt_timeout = 1000ms;
    options.dead_for = 30s;
    options.allow_plain_http = true;
    return options;
}

DnsBalancerSettings doh_settings() {
    DnsBalancerSettings settings;
    settings.doh_enabled = true;
    return settings;
}

std::uint16_t free_udp_port() {
    const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ::bind(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
    socklen_t len = sizeof(addr);
    ::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len);
    ::close(fd);
    return ntohs(addr.sin_port);
}

Bytes make_query(std::uint16_t id, const char* name = "example.com") {
    return build_dns_query(id, name, kDnsTypeA, kDnsClassIN, false);
}

// Sends `query` to the forwarder over UDP; the answer, or empty on timeout.
Bytes ask_udp(std::uint16_t port, const Bytes& query, int timeout_ms = 3000) {
    const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    ::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    ::sendto(fd, query.data(), query.size(), 0, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
    pollfd pfd{fd, POLLIN, 0};
    Bytes answer;
    if (::poll(&pfd, 1, timeout_ms) > 0) {
        answer.resize(4096);
        const ssize_t n = ::recv(fd, answer.data(), answer.size(), 0);
        answer.resize(n > 0 ? static_cast<std::size_t>(n) : 0);
    }
    ::close(fd);
    return answer;
}

Bytes ask_tcp(std::uint16_t port, const Bytes& query, int timeout_ms = 3000) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    ::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    Bytes answer;
    if (::connect(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) == 0) {
        Bytes framed{static_cast<std::uint8_t>(query.size() >> 8),
                     static_cast<std::uint8_t>(query.size() & 0xFF)};
        framed.insert(framed.end(), query.begin(), query.end());
        ::send(fd, framed.data(), framed.size(), MSG_NOSIGNAL);
        timeval tv{timeout_ms / 1000, (timeout_ms % 1000) * 1000};
        ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        std::uint8_t head[2];
        if (::recv(fd, head, 2, MSG_WAITALL) == 2) {
            answer.resize(static_cast<std::size_t>((head[0] << 8) | head[1]));
            if (::recv(fd, answer.data(), answer.size(), MSG_WAITALL) !=
                static_cast<ssize_t>(answer.size())) {
                answer.clear();
            }
        }
    }
    ::close(fd);
    return answer;
}

std::uint8_t rcode_of(const Bytes& message) {
    return message.size() >= 4 ? static_cast<std::uint8_t>(message[3] & 0x0F) : 0xFF;
}

DnsBalancerGroup doh_group(std::uint16_t port, std::vector<DnsBalancerUpstream> upstreams) {
    DnsBalancerGroup group;
    group.port = port;
    group.upstreams = std::move(upstreams);
    return group;
}

} // namespace

TEST_CASE("doh forwarder: UDP query is answered through DoH with the client's id") {
    CurlRuntime curl;
    FakeDohServer server;
    DnsBalancer balancer(doh_options());
    const auto port = free_udp_port();
    CHECK(balancer.configure({doh_group(port, {DnsBalancerUpstream::doh(server.url())})},
                             doh_settings())
              .empty());

    const Bytes query = make_query(0xBEEF);
    const Bytes answer = ask_udp(port, query);
    REQUIRE(answer.size() == query.size());
    CHECK(answer[0] == 0xBE);
    CHECK(answer[1] == 0xEF);
    CHECK((answer[2] & 0x80) != 0);
    CHECK(rcode_of(answer) == 0);
    CHECK(server.requests() == 1);
    CHECK(server.saw_zero_id());
    CHECK(server.saw_dns_content_type());
}

TEST_CASE("doh forwarder: TCP query is answered through DoH") {
    CurlRuntime curl;
    FakeDohServer server;
    DnsBalancer balancer(doh_options());
    const auto port = free_udp_port();
    balancer.configure({doh_group(port, {DnsBalancerUpstream::doh(server.url())})}, doh_settings());

    const Bytes query = make_query(0x1234);
    const Bytes answer = ask_tcp(port, query);
    REQUIRE(answer.size() == query.size());
    CHECK(answer[0] == 0x12);
    CHECK(answer[1] == 0x34);
    CHECK((answer[2] & 0x80) != 0);
    CHECK(server.requests() == 1);
}

TEST_CASE("doh forwarder: connections are reused") {
    CurlRuntime curl;
    FakeDohServer server;
    DnsBalancer balancer(doh_options());
    const auto port = free_udp_port();
    balancer.configure({doh_group(port, {DnsBalancerUpstream::doh(server.url())})}, doh_settings());

    for (std::uint16_t id = 1; id <= 12; ++id) {
        const Bytes answer = ask_udp(port, make_query(id));
        REQUIRE(answer.size() > 12);
        CHECK(answer[1] == id);
    }
    CHECK(server.requests() == 12);
    CHECK(server.connections() == 1);
}

TEST_CASE("doh forwarder: plain and DoH servers take turns") {
    CurlRuntime curl;
    FakeDohServer doh;
    FakeUdpDns plain;
    DnsBalancer balancer(doh_options());
    const auto port = free_udp_port();
    balancer.configure({doh_group(port, {DnsBalancerUpstream::plain("127.0.0.1", plain.port()),
                                         DnsBalancerUpstream::doh(doh.url())})},
                       doh_settings());
    for (std::uint16_t id = 1; id <= 10; ++id) {
        REQUIRE(ask_udp(port, make_query(id)).size() > 12);
    }
    CHECK(plain.hits() == 5);
    CHECK(doh.requests() == 5);
}

TEST_CASE("doh forwarder: a failing DoH server hands the query to the next one") {
    CurlRuntime curl;
    for (const auto mode : {FakeDohServer::Mode::Error500, FakeDohServer::Mode::ShortBody,
                            FakeDohServer::Mode::Servfail, FakeDohServer::Mode::Silent}) {
        CAPTURE(static_cast<int>(mode));
        FakeDohServer doh(mode);
        FakeUdpDns plain;
        DnsBalancer balancer(doh_options());
        const auto port = free_udp_port();
        balancer.configure({doh_group(port, {DnsBalancerUpstream::doh(doh.url()),
                                             DnsBalancerUpstream::plain("127.0.0.1", plain.port())})},
                           doh_settings());
        for (std::uint16_t id = 1; id <= 4; ++id) {
            const Bytes answer = ask_udp(port, make_query(id), 4000);
            REQUIRE(answer.size() > 12);
            CHECK(rcode_of(answer) == 0);
        }
        CHECK(plain.hits() == 4);
        // A server that failed is skipped afterwards (a SERVFAIL answer is not a failure of the server).
        if (mode != FakeDohServer::Mode::Servfail) {
            CHECK(doh.requests() <= 2);
        }
    }
}

TEST_CASE("doh forwarder: when every server fails the client gets SERVFAIL") {
    CurlRuntime curl;
    FakeDohServer doh(FakeDohServer::Mode::Error500);
    DnsBalancer balancer(doh_options());
    const auto port = free_udp_port();
    balancer.configure({doh_group(port, {DnsBalancerUpstream::doh(doh.url())})}, doh_settings());
    const Bytes answer = ask_udp(port, make_query(7));
    REQUIRE(answer.size() >= 12);
    CHECK(answer[1] == 7);
    CHECK(rcode_of(answer) == 2);
}

TEST_CASE("doh forwarder: without DoH support a DoH-only group answers SERVFAIL, a mixed one uses the plain server") {
    CurlRuntime curl;
    FakeDohServer doh;
    FakeUdpDns plain;
    DnsBalancer balancer(doh_options());
    const auto only = free_udp_port();
    auto other = free_udp_port();
    if (other == only) other = static_cast<std::uint16_t>(other + 1);
    DnsBalancerSettings off;  // doh_enabled = false
    balancer.configure({doh_group(only, {DnsBalancerUpstream::doh(doh.url())}),
                        doh_group(other, {DnsBalancerUpstream::doh(doh.url()),
                                          DnsBalancerUpstream::plain("127.0.0.1", plain.port())})},
                       off);
    CHECK(rcode_of(ask_udp(only, make_query(1))) == 2);
    const Bytes answer = ask_udp(other, make_query(2));
    REQUIRE(answer.size() > 12);
    CHECK(rcode_of(answer) == 0);
    CHECK(doh.requests() == 0);
    CHECK(plain.hits() == 1);
}

TEST_CASE("doh forwarder: a detour without a routing mark makes the server unusable") {
    CurlRuntime curl;
    FakeDohServer doh;
    DnsBalancer balancer(doh_options());
    const auto port = free_udp_port();
    balancer.configure({doh_group(port, {DnsBalancerUpstream::doh(doh.url(), "vpn")})}, doh_settings());
    CHECK(rcode_of(ask_udp(port, make_query(3))) == 2);
    CHECK(doh.requests() == 0);
}

TEST_CASE("doh forwarder: https to a server that does not speak TLS fails the query") {
    CurlRuntime curl;
    FakeDohServer not_tls;
    DnsBalancer balancer(doh_options());
    const auto port = free_udp_port();
    balancer.configure(
        {doh_group(port, {DnsBalancerUpstream::doh("https://127.0.0.1:" + std::to_string(not_tls.port()) +
                                                   "/dns-query")})},
        doh_settings());
    CHECK(rcode_of(ask_udp(port, make_query(5), 6000)) == 2);
}

TEST_CASE("doh bootstrap: looks the host up on a worker thread") {
    std::mutex mutex;
    std::vector<std::string> asked;
    std::optional<DnsBalancerUpstream> seen_server;
    std::atomic<int> ready{0};
    DohBootstrap bootstrap(
        [&] { ++ready; },
        [&](const std::string& host, const std::optional<DnsBalancerUpstream>& server) {
            std::lock_guard<std::mutex> lock(mutex);
            asked.push_back(host);
            seen_server = server;
            return host == "known.example" ? std::vector<std::string>{"192.0.2.7"}
                                           : std::vector<std::string>{};
        });
    bootstrap.set_server(DnsBalancerUpstream::plain("10.0.0.1", 53));
    bootstrap.request("known.example");
    bootstrap.request("unknown.example");
    for (int i = 0; i < 100 && ready.load() < 2; ++i) std::this_thread::sleep_for(20ms);
    const auto results = bootstrap.take_results();
    REQUIRE(results.size() == 2);
    CHECK(results[0].host == "known.example");
    CHECK(results[0].ips == std::vector<std::string>{"192.0.2.7"});
    CHECK(results[1].ips.empty());
    CHECK(seen_server == std::optional<DnsBalancerUpstream>(DnsBalancerUpstream::plain("10.0.0.1", 53)));
    CHECK(bootstrap.take_results().empty());
}

TEST_CASE("doh bootstrap: the default lookup asks the given DNS server, else the system resolver") {
    // A server that answers with a SERVFAIL gives no address.
    FakeUdpDns servfail(true);
    CHECK(DohBootstrap::resolve("dns.example", DnsBalancerUpstream::plain("127.0.0.1", servfail.port()))
              .empty());
    // The system resolver knows localhost.
    const auto local = DohBootstrap::resolve("localhost", std::nullopt);
    CHECK_FALSE(local.empty());
}

TEST_CASE("doh client: direct use on an epoll loop, with cancel and timeout") {
    CurlRuntime curl;
    struct State {
        int done{0};
        bool ok{false};
        std::uint64_t user{0};
    } state;
    const int ep = ::epoll_create1(EPOLL_CLOEXEC);
    DohClientOptions options;
    options.epoll_fd = ep;
    options.tag_kind = 1;
    options.allow_plain_http = true;
    DohClient client(options,
                     [](void* ctx, std::uint64_t user, const DohResult& result) {
                         auto* s = static_cast<State*>(ctx);
                         ++s->done;
                         s->ok = result.ok;
                         s->user = user;
                     },
                     &state);
    REQUIRE(client.valid());
    const auto pump = [&](int budget_ms, int want_done) {
        for (int waited = 0; waited < budget_ms && state.done < want_done; waited += 10) {
            epoll_event events[8];
            const int n = ::epoll_wait(ep, events, 8, 10);
            for (int i = 0; i < n; ++i) {
                client.handle_socket_event(static_cast<int>(events[i].data.u64 & 0xFFFFFFFFu),
                                           events[i].events);
            }
            if (client.next_timeout_ms() == 0) client.handle_timeout();
        }
    };

    FakeDohServer server;
    const std::size_t endpoint = client.add_endpoint(server.url(), 0);
    REQUIRE(endpoint != DohClient::kNoEndpoint);
    const Bytes query = make_query(0x4242);

    // Unresolved host: nothing is started.
    CHECK(client.start(endpoint, query.data(), query.size(), 1000ms, 1) == DohClient::kNoTransfer);
    client.set_addresses(endpoint, {"127.0.0.1"});
    CHECK(client.endpoint_ready(endpoint));

    CHECK(client.start(endpoint, query.data(), query.size(), 1000ms, 11) != DohClient::kNoTransfer);
    pump(2000, 1);
    CHECK(state.done == 1);
    CHECK(state.ok);
    CHECK(state.user == 11);

    // A cancelled transfer reports nothing.
    server.set_mode(FakeDohServer::Mode::Silent);
    const std::size_t transfer = client.start(endpoint, query.data(), query.size(), 1000ms, 12);
    REQUIRE(transfer != DohClient::kNoTransfer);
    pump(100, 2);
    client.cancel(transfer);
    pump(300, 2);
    CHECK(state.done == 1);

    // A server that never answers ends in an error at the transfer timeout.
    client.start(endpoint, query.data(), query.size(), 200ms, 13);
    pump(3000, 2);
    CHECK(state.done == 2);
    CHECK_FALSE(state.ok);
    CHECK(state.user == 13);

    // Not a usable URL.
    CHECK(client.add_endpoint("ftp://example.com/x", 0) == DohClient::kNoEndpoint);
    CHECK(client.add_endpoint("https://", 0) == DohClient::kNoEndpoint);
    ::close(ep);
}

TEST_CASE("find_ca_bundle: a certificate file wins over a directory; empty places are skipped") {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / ("kpbr-ca-" + std::to_string(::getpid()));
    fs::create_directories(root / "empty");
    fs::create_directories(root / "certs");
    { std::ofstream(root / "certs" / "a.pem") << "x"; }
    { std::ofstream(root / "bundle.crt") << "x"; }
    { std::ofstream(root / "zero.crt"); }

    const auto file = find_ca_bundle({(root / "missing.crt").string(), (root / "zero.crt").string(),
                                      (root / "bundle.crt").string()},
                                     {(root / "certs").string()});
    REQUIRE(file.has_value());
    CHECK(file->path == (root / "bundle.crt").string());
    CHECK_FALSE(file->is_directory);

    const auto dir = find_ca_bundle({(root / "missing.crt").string()},
                                    {(root / "empty").string(), (root / "nope").string(),
                                     (root / "certs").string()});
    REQUIRE(dir.has_value());
    CHECK(dir->path == (root / "certs").string());
    CHECK(dir->is_directory);

    CHECK_FALSE(find_ca_bundle({(root / "missing.crt").string()}, {(root / "empty").string()})
                    .has_value());
    fs::remove_all(root);
}
