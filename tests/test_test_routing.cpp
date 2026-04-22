#include <doctest/doctest.h>

#include "../src/cache/cache_manager.hpp"
#include "../src/cmd/test_routing.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace keen_pbr3;

namespace {

std::filesystem::path make_temp_dir() {
    char path_template[] = "/tmp/keen-pbr-test-routing-XXXXXX";
    const char* created = mkdtemp(path_template);
    if (created == nullptr) {
        throw std::runtime_error("mkdtemp failed");
    }
    return std::filesystem::path(created);
}

bool udp_socket_available() {
    const int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return false;
    }
    close(fd);
    return true;
}

void push_u16(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(value & 0xFF));
}

void push_u32(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(value & 0xFF));
}

size_t find_question_end(const uint8_t* packet, size_t packet_len) {
    size_t offset = 12;
    while (offset < packet_len) {
        const uint8_t len = packet[offset++];
        if (len == 0) {
            break;
        }
        offset += len;
    }
    if (offset + 4 > packet_len) {
        throw std::runtime_error("truncated DNS question");
    }
    return offset + 4;
}

uint16_t read_qtype(const uint8_t* packet, size_t packet_len) {
    const size_t end = find_question_end(packet, packet_len);
    return static_cast<uint16_t>((packet[end - 4] << 8) | packet[end - 3]);
}

std::vector<uint8_t> build_dns_response(const uint8_t* request,
                                        size_t request_len,
                                        const std::vector<std::string>& ipv4_answers,
                                        const std::vector<std::string>& ipv6_answers) {
    std::vector<uint8_t> packet;
    packet.reserve(512);

    const size_t question_end = find_question_end(request, request_len);
    const uint16_t qtype = read_qtype(request, request_len);
    const uint16_t answer_count =
        static_cast<uint16_t>((qtype == 1 ? ipv4_answers.size() : 0) +
                              (qtype == 28 ? ipv6_answers.size() : 0));

    push_u16(packet, static_cast<uint16_t>((request[0] << 8) | request[1]));
    push_u16(packet, 0x8180);
    push_u16(packet, 0x0001);
    push_u16(packet, answer_count);
    push_u16(packet, 0x0000);
    push_u16(packet, 0x0000);
    packet.insert(packet.end(), request + 12, request + question_end);

    const auto append_answer = [&packet](uint16_t type, const std::string& ip) {
        push_u16(packet, 0xC00C);
        push_u16(packet, type);
        push_u16(packet, 0x0001);
        push_u32(packet, 0);

        if (type == 1) {
            in_addr addr {};
            if (inet_pton(AF_INET, ip.c_str(), &addr) != 1) {
                throw std::runtime_error("invalid IPv4 answer");
            }
            push_u16(packet, 4);
            const auto* bytes = reinterpret_cast<const uint8_t*>(&addr);
            packet.insert(packet.end(), bytes, bytes + 4);
        } else {
            in6_addr addr {};
            if (inet_pton(AF_INET6, ip.c_str(), &addr) != 1) {
                throw std::runtime_error("invalid IPv6 answer");
            }
            push_u16(packet, 16);
            const auto* bytes = reinterpret_cast<const uint8_t*>(&addr);
            packet.insert(packet.end(), bytes, bytes + 16);
        }
    };

    if (qtype == 1) {
        for (const auto& ip : ipv4_answers) {
            append_answer(1, ip);
        }
    } else if (qtype == 28) {
        for (const auto& ip : ipv6_answers) {
            append_answer(28, ip);
        }
    }

    return packet;
}

class TestDnsServer {
public:
    TestDnsServer(std::vector<std::string> ipv4_answers,
                  std::vector<std::string> ipv6_answers)
        : ipv4_answers_(std::move(ipv4_answers))
        , ipv6_answers_(std::move(ipv6_answers)) {
        socket_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
        if (socket_fd_ < 0) {
            throw std::runtime_error("socket() failed");
        }

        sockaddr_in addr {};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(0);
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (bind(socket_fd_, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0) {
            close(socket_fd_);
            throw std::runtime_error("bind() failed");
        }

        socklen_t len = sizeof(addr);
        if (getsockname(socket_fd_, reinterpret_cast<sockaddr*>(&addr), &len) != 0) {
            close(socket_fd_);
            throw std::runtime_error("getsockname() failed");
        }
        port_ = ntohs(addr.sin_port);

        server_thread_ = std::thread([this]() { serve(); });
    }

    ~TestDnsServer() {
        stop_ = true;
        if (socket_fd_ >= 0) {
            sockaddr_in addr {};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(port_);
            addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            (void)sendto(socket_fd_, "", 0, 0, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
        }
        if (server_thread_.joinable()) {
            server_thread_.join();
        }
        if (socket_fd_ >= 0) {
            close(socket_fd_);
            socket_fd_ = -1;
        }
    }

    std::string address() const {
        return "127.0.0.1:" + std::to_string(port_);
    }

private:
    void serve() {
        while (!stop_) {
            uint8_t buffer[512] = {};
            sockaddr_in client_addr {};
            socklen_t client_len = sizeof(client_addr);
            const ssize_t received = recvfrom(socket_fd_,
                                              buffer,
                                              sizeof(buffer),
                                              0,
                                              reinterpret_cast<sockaddr*>(&client_addr),
                                              &client_len);
            if (received <= 0) {
                continue;
            }
            if (stop_) {
                break;
            }

            auto response = build_dns_response(buffer,
                                               static_cast<size_t>(received),
                                               ipv4_answers_,
                                               ipv6_answers_);
            (void)sendto(socket_fd_,
                         response.data(),
                         response.size(),
                         0,
                         reinterpret_cast<const sockaddr*>(&client_addr),
                         client_len);
        }
    }

    int socket_fd_{-1};
    uint16_t port_{0};
    std::atomic<bool> stop_{false};
    std::vector<std::string> ipv4_answers_;
    std::vector<std::string> ipv6_answers_;
    std::thread server_thread_;
};

Config build_test_config() {
    Config config;
    config.lists = std::map<std::string, ListConfig>{};
    config.dns = DnsConfig{};
    return config;
}

} // namespace

TEST_CASE("compute_test_routing resolves domain through configured system resolver") {
    if (!udp_socket_available()) {
        DOCTEST_INFO("UDP sockets unavailable in current environment");
        return;
    }

    const auto temp_dir = make_temp_dir();
    CacheManager cache(temp_dir);
    cache.ensure_dir();

    TestDnsServer server({"10.0.0.53"}, {"2001:db8::53"});

    Config config = build_test_config();
    api::SystemResolver system_resolver;
    system_resolver.address = server.address();
    config.dns->system_resolver = system_resolver;

    const auto result = compute_test_routing(config, cache, "www.example.com");

    CHECK(result.is_domain);
    CHECK(result.resolved_ips == std::vector<std::string>{"10.0.0.53", "2001:db8::53"});
    CHECK_FALSE(result.dns_error.has_value());

    std::filesystem::remove_all(temp_dir);
}

TEST_CASE("compute_test_routing falls back to resolv.conf when system resolver is absent") {
    const auto temp_dir = make_temp_dir();
    CacheManager cache(temp_dir);
    cache.ensure_dir();

    Config config = build_test_config();

    const auto result = compute_test_routing(config, cache, "example.invalid");

    CHECK(result.is_domain);
    CHECK(result.resolved_ips.empty());
    REQUIRE(result.entries.size() == 1);
    CHECK(result.entries.front().ip == "(no IPs resolved)");

    std::filesystem::remove_all(temp_dir);
}
