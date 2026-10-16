#include <doctest/doctest.h>

#include "../src/intercept/intercept_service.hpp"
#include "../src/netfilter/set_writer.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

using namespace keen_pbr3;

namespace {

bool isolated_netns_guard() {
    const char* enabled = std::getenv("KPBR_NETLINK_IT");
    const char* guard = std::getenv("KPBR_NETLINK_IN_NETNS");
    const char* parent_id = std::getenv("KPBR_NETLINK_PARENT_NS");
    if (enabled == nullptr || std::string(enabled) != "1" || guard == nullptr ||
        std::string(guard) != "1" || parent_id == nullptr || std::string(parent_id).empty()) {
        return false;
    }
    struct stat self{};
    if (::stat("/proc/self/ns/net", &self) != 0) return false;
    const std::string current = std::to_string(self.st_dev) + ":" + std::to_string(self.st_ino);
    return current != parent_id;
}

bool live_enabled() {
    const char* enabled = std::getenv("KPBR_NFNETLINK_LIVE");
    return enabled != nullptr && std::string(enabled) == "1" && isolated_netns_guard();
}

int shell(const char* command) { return std::system(command); }

constexpr const char* kRule =
    "-t mangle %s POSTROUTING %s -p udp --sport 53 -m conntrack --ctstate ESTABLISHED "
    "--ctdir REPLY -j NFQUEUE --queue-num 9053 --queue-bypass";

std::vector<uint8_t> make_query(const char* labels, std::size_t labels_len) {
    std::vector<uint8_t> q = {0x42, 0x42, 0x01, 0x00, 0, 1, 0, 0, 0, 0, 0, 0};
    q.insert(q.end(), labels, labels + labels_len);  // includes terminating zero
    q.insert(q.end(), {0, 1, 0, 1});
    return q;
}

std::vector<uint8_t> make_answer(const std::vector<uint8_t>& query) {
    std::vector<uint8_t> r = query;
    r[2] = 0x81;
    r[3] = 0x80;
    r[7] = 1;  // ancount
    const uint8_t rr[] = {0xC0, 0x0C, 0, 1, 0, 1, 0, 0, 0, 60, 0, 4, 93, 184, 216, 34};
    r.insert(r.end(), rr, rr + sizeof(rr));
    return r;
}

// Sends `query` to 127.0.0.1:53 and returns the reply (empty on timeout).
std::vector<uint8_t> ask(const std::vector<uint8_t>& query, int timeout_ms = 2000) {
    const int fd = ::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    REQUIRE(fd >= 0);
    sockaddr_in dst{};
    dst.sin_family = AF_INET;
    dst.sin_port = htons(53);
    dst.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    REQUIRE(::sendto(fd, query.data(), query.size(), 0, reinterpret_cast<sockaddr*>(&dst),
                     sizeof(dst)) == static_cast<ssize_t>(query.size()));
    std::vector<uint8_t> reply;
    pollfd pfd{fd, POLLIN, 0};
    if (::poll(&pfd, 1, timeout_ms) > 0) {
        uint8_t buf[1500];
        const ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if (n > 0) reply.assign(buf, buf + n);
    }
    ::close(fd);
    return reply;
}

struct LiveCleanup {
    ~LiveCleanup() {
        char cmd[512];
        std::snprintf(cmd, sizeof(cmd), (std::string("iptables ") + kRule).c_str(), "-D", "");
        shell(cmd);
        shell("ipset destroy kpbr4d_ex 2>/dev/null");
    }
};

} // namespace

TEST_CASE("intercept: live DNS hold adds the IP before the answer reaches the client") {
    if (!live_enabled()) return;

    shell("ipset destroy kpbr4d_ex 2>/dev/null");
    REQUIRE(shell("ipset create kpbr4d_ex hash:net timeout 0") == 0);
    LiveCleanup cleanup;
    {
        char cmd[512];
        std::snprintf(cmd, sizeof(cmd), (std::string("iptables ") + kRule).c_str(), "-I", "1");
        REQUIRE(shell(cmd) == 0);
    }

    // Tiny DNS server on 127.0.0.1:53.
    const int server = ::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    REQUIRE(server >= 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(53);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    REQUIRE(::bind(server, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
    std::atomic<bool> server_stop{false};
    std::vector<uint8_t> sent_answer;
    std::atomic<int> answered{0};
    std::thread server_thread([&] {
        while (!server_stop.load()) {
            pollfd pfd{server, POLLIN, 0};
            if (::poll(&pfd, 1, 50) <= 0) continue;
            uint8_t buf[1500];
            sockaddr_in peer{};
            socklen_t peer_len = sizeof(peer);
            const ssize_t n = ::recvfrom(server, buf, sizeof(buf), 0,
                                         reinterpret_cast<sockaddr*>(&peer), &peer_len);
            if (n <= 0) continue;
            const std::vector<uint8_t> answer = make_answer(std::vector<uint8_t>(buf, buf + n));
            if (answered.load() == 0) sent_answer = answer;
            ::sendto(server, answer.data(), answer.size(), 0, reinterpret_cast<sockaddr*>(&peer),
                     peer_len);
            answered.fetch_add(1);
        }
    });

    auto stop_server = [&] {
        server_stop = true;
        if (server_thread.joinable()) server_thread.join();
        ::close(server);
    };

    try {
        DomainIndex::Builder builder;
        const auto id = builder.add_list("ex");
        builder.add_domain(id, "example.com");
        auto snapshot = std::make_shared<InterceptSnapshot>();
        snapshot->index = std::make_shared<DomainIndex>(std::move(builder).build());
        snapshot->targets.push_back({"kpbr4d_ex", "", 300});

        InterceptService service(nfnl::make_ipset_writer());
        InterceptServiceOptions options;
        options.queue_num = 9053;
        options.hold_timeout_ms = 500;  // generous: CI netns can be slow
        service.start(options, snapshot);
        CHECK(service.running());

        // www.example.com
        static const char kName[] = "\3www\7example\3com";
        const auto query = make_query(kName, sizeof(kName));  // sizeof includes the root label
        const auto reply = ask(query);
        REQUIRE_FALSE(reply.empty());
        // Right after the client got the answer, the element must already be present.
        CHECK(shell("ipset test kpbr4d_ex 93.184.216.34 >/dev/null 2>&1") == 0);
        CHECK(reply == sent_answer);
        CHECK(service.counters().dns_matched >= 1);
        CHECK(service.counters().set_added >= 1);
        const auto events = service.events_since(0, 10);
        REQUIRE_FALSE(events.empty());
        CHECK(events[0].domain == "www.example.com");

        service.stop();
        CHECK_FALSE(service.running());

        // Listener gone: --queue-bypass must keep DNS working.
        const auto after = ask(query);
        CHECK(after.size() == reply.size());
    } catch (const std::exception& e) {
        INFO(e.what());
        CHECK(false);
    }
    stop_server();
}
