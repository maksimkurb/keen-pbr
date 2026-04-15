#include <doctest/doctest.h>

#include "../src/dns/dns_txt_client.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace keen_pbr3;

namespace {

std::vector<uint8_t> encode_qname(const std::string& domain) {
    std::vector<uint8_t> encoded;
    size_t start = 0;
    while (start < domain.size()) {
        const size_t dot = domain.find('.', start);
        const size_t end = (dot == std::string::npos) ? domain.size() : dot;
        const size_t len = end - start;
        encoded.push_back(static_cast<uint8_t>(len));
        encoded.insert(encoded.end(), domain.begin() + static_cast<std::ptrdiff_t>(start),
                       domain.begin() + static_cast<std::ptrdiff_t>(end));
        if (dot == std::string::npos) {
            break;
        }
        start = dot + 1;
    }
    encoded.push_back(0x00);
    return encoded;
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

std::vector<uint8_t> build_txt_response(uint16_t id,
                                        const std::string& domain,
                                        const std::vector<std::string>& txt_answers) {
    std::vector<uint8_t> packet;
    packet.reserve(512);

    push_u16(packet, id);
    push_u16(packet, 0x8180);
    push_u16(packet, 0x0001);
    push_u16(packet, static_cast<uint16_t>(txt_answers.size()));
    push_u16(packet, 0x0000);
    push_u16(packet, 0x0000);

    const auto qname = encode_qname(domain);
    packet.insert(packet.end(), qname.begin(), qname.end());
    push_u16(packet, 0x0010);
    push_u16(packet, 0x0001);

    for (const auto& txt : txt_answers) {
        push_u16(packet, 0xC00C);
        push_u16(packet, 0x0010);
        push_u16(packet, 0x0001);
        push_u32(packet, 0);
        push_u16(packet, static_cast<uint16_t>(txt.size() + 1));
        packet.push_back(static_cast<uint8_t>(txt.size()));
        packet.insert(packet.end(), txt.begin(), txt.end());
    }

    return packet;
}

class SingleResponseDnsServer {
public:
    explicit SingleResponseDnsServer(std::vector<std::string> txt_answers)
        : txt_answers_(std::move(txt_answers)) {
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

        server_thread_ = std::thread([this]() { serve_once(); });
    }

    ~SingleResponseDnsServer() {
        if (socket_fd_ >= 0) {
            close(socket_fd_);
            socket_fd_ = -1;
        }
        if (server_thread_.joinable()) {
            server_thread_.join();
        }
    }

    std::string address() const {
        return "127.0.0.1:" + std::to_string(port_);
    }

private:
    void serve_once() {
        std::array<uint8_t, 512> buffer {};
        sockaddr_in client_addr {};
        socklen_t client_len = sizeof(client_addr);
        const ssize_t received = recvfrom(socket_fd_,
                                          buffer.data(),
                                          buffer.size(),
                                          0,
                                          reinterpret_cast<sockaddr*>(&client_addr),
                                          &client_len);
        if (received < 2) {
            return;
        }

        const uint16_t id = static_cast<uint16_t>((buffer[0] << 8) | buffer[1]);
        auto response = build_txt_response(id, "config-hash.keen.pbr", txt_answers_);
        (void)sendto(socket_fd_,
                     response.data(),
                     response.size(),
                     0,
                     reinterpret_cast<const sockaddr*>(&client_addr),
                     client_len);
    }

    int socket_fd_{-1};
    uint16_t port_{0};
    std::vector<std::string> txt_answers_;
    std::thread server_thread_;
};

} // namespace

TEST_CASE("parse_resolver_config_hash_txt parses ts/hash payload") {
    const auto parsed = parse_resolver_config_hash_txt("1744060800|0123456789abcdef0123456789abcdef");
    REQUIRE(parsed.ts.has_value());
    CHECK(*parsed.ts == 1744060800);
    CHECK(parsed.hash == "0123456789abcdef0123456789abcdef");
}

TEST_CASE("parse_resolver_config_hash_txt parses quoted ts/hash payload") {
    const auto parsed = parse_resolver_config_hash_txt("\"1744060800|0123456789abcdef0123456789abcdef\"");
    REQUIRE(parsed.ts.has_value());
    CHECK(*parsed.ts == 1744060800);
    CHECK(parsed.hash == "0123456789abcdef0123456789abcdef");
}

TEST_CASE("parse_resolver_config_hash_txt parses md5-prefixed hash payload") {
    const auto parsed = parse_resolver_config_hash_txt("md5=0123456789ABCDEF0123456789ABCDEF");
    CHECK_FALSE(parsed.ts.has_value());
    CHECK(parsed.hash == "0123456789abcdef0123456789abcdef");
}

TEST_CASE("query_resolver_config_hash_txt selects TXT answer with latest timestamp") {
    SingleResponseDnsServer server({
        "1744060800|aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
        "1744060802|bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
    });

    const auto result = query_resolver_config_hash_txt(
        server.address(),
        "config-hash.keen.pbr",
        std::chrono::milliseconds(1000));

    CHECK(result.status == ResolverConfigHashProbeStatus::SUCCESS);
    REQUIRE(result.raw_txt.has_value());
    CHECK(*result.raw_txt == "1744060802|bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    CHECK(result.parsed_value.ts == std::optional<std::int64_t>{1744060802});
    CHECK(result.parsed_value.hash == "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
}

TEST_CASE("query_resolver_config_hash_txt reports missing TXT as no usable TXT") {
    SingleResponseDnsServer server({});

    const auto result = query_resolver_config_hash_txt(
        server.address(),
        "config-hash.keen.pbr",
        std::chrono::milliseconds(1000));

    CHECK(result.status == ResolverConfigHashProbeStatus::NO_USABLE_TXT);
    CHECK_FALSE(result.raw_txt.has_value());
}

TEST_CASE("query_resolver_config_hash_txt reports invalid payload") {
    SingleResponseDnsServer server({"not-a-md5"});

    const auto result = query_resolver_config_hash_txt(
        server.address(),
        "config-hash.keen.pbr",
        std::chrono::milliseconds(1000));

    CHECK(result.status == ResolverConfigHashProbeStatus::INVALID_TXT);
    REQUIRE(result.raw_txt.has_value());
    CHECK(*result.raw_txt == "not-a-md5");
}

TEST_CASE("query_resolver_config_hash_txt reports query failure for invalid resolver address") {
    const auto result = query_resolver_config_hash_txt(
        "not-an-address",
        "config-hash.keen.pbr",
        std::chrono::milliseconds(1000));

    CHECK(result.status == ResolverConfigHashProbeStatus::QUERY_FAILED);
    CHECK_FALSE(result.error.empty());
}
