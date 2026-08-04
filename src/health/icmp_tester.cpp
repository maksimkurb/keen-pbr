#include "icmp_tester.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <netinet/icmp6.h>
#include <netinet/ip_icmp.h>
#include <poll.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

#include <algorithm>

namespace keen_pbr3 {

namespace {

class SocketFd {
public:
    explicit SocketFd(int fd) : fd_(fd) {}
    ~SocketFd() { if (fd_ >= 0) close(fd_); }
    SocketFd(const SocketFd&) = delete;
    SocketFd& operator=(const SocketFd&) = delete;
    int get() const { return fd_; }
private:
    int fd_;
};

bool addresses_equal(int family, const sockaddr_storage& left,
                     const sockaddr_storage& right) {
    if (family == AF_INET) {
        const auto& lhs = reinterpret_cast<const sockaddr_in&>(left);
        const auto& rhs = reinterpret_cast<const sockaddr_in&>(right);
        return lhs.sin_family == AF_INET && rhs.sin_family == AF_INET &&
               lhs.sin_addr.s_addr == rhs.sin_addr.s_addr;
    }
    if (family == AF_INET6) {
        const auto& lhs = reinterpret_cast<const sockaddr_in6&>(left);
        const auto& rhs = reinterpret_cast<const sockaddr_in6&>(right);
        return lhs.sin6_family == AF_INET6 && rhs.sin6_family == AF_INET6 &&
               std::memcmp(&lhs.sin6_addr, &rhs.sin6_addr, sizeof(in6_addr)) == 0;
    }
    return false;
}

uint16_t socket_identifier(int fd, int family) {
    sockaddr_storage local{};
    socklen_t length = sizeof(local);
    if (getsockname(fd, reinterpret_cast<sockaddr*>(&local), &length) < 0) {
        return 0;
    }
    return family == AF_INET
        ? ntohs(reinterpret_cast<const sockaddr_in&>(local).sin_port)
        : ntohs(reinterpret_cast<const sockaddr_in6&>(local).sin6_port);
}

} // namespace

namespace icmp_detail {

bool reply_matches(int family, const unsigned char* data, size_t size,
                   const sockaddr_storage& source,
                   const sockaddr_storage& expected_source,
                   uint16_t identifier, uint16_t sequence) {
    if (!addresses_equal(family, source, expected_source)) return false;
    if (family == AF_INET) {
        if (size < sizeof(icmphdr)) return false;
        const auto* header = reinterpret_cast<const icmphdr*>(data);
        return header->type == ICMP_ECHOREPLY && header->code == 0 &&
               ntohs(header->un.echo.id) == identifier &&
               ntohs(header->un.echo.sequence) == sequence;
    }
    if (family == AF_INET6) {
        if (size < sizeof(icmp6_hdr)) return false;
        const auto* header = reinterpret_cast<const icmp6_hdr*>(data);
        return header->icmp6_type == ICMP6_ECHO_REPLY && header->icmp6_code == 0 &&
               ntohs(header->icmp6_id) == identifier &&
               ntohs(header->icmp6_seq) == sequence;
    }
    return false;
}

} // namespace icmp_detail

URLTestResult IcmpTester::test(const std::string& target, uint32_t fwmark,
                               uint32_t count, uint32_t max_failed,
                               uint32_t packet_interval_ms, uint32_t timeout_ms,
                               uint32_t max_rtt_ms) const {
    URLTestResult result;
    result.probe_target = target;
    result.packets_attempted = count;
    result.packets_sent = 0;
    result.packets_received = 0;
    result.packets_failed = 0;
    sockaddr_storage address{};
    socklen_t address_len = 0;
    int family = AF_UNSPEC;
    if (inet_pton(AF_INET, target.c_str(), &reinterpret_cast<sockaddr_in&>(address).sin_addr) == 1) {
        auto& a = reinterpret_cast<sockaddr_in&>(address); a.sin_family = AF_INET;
        family = AF_INET; address_len = sizeof(a);
    } else if (inet_pton(AF_INET6, target.c_str(), &reinterpret_cast<sockaddr_in6&>(address).sin6_addr) == 1) {
        auto& a = reinterpret_cast<sockaddr_in6&>(address); a.sin6_family = AF_INET6;
        family = AF_INET6; address_len = sizeof(a);
    } else {
        result.error = "invalid ICMP target";
        result.packets_failed = count;
        return result;
    }

    const int protocol = family == AF_INET
        ? static_cast<int>(IPPROTO_ICMP)
        : static_cast<int>(IPPROTO_ICMPV6);
    SocketFd socket_fd(socket(family, SOCK_DGRAM | SOCK_CLOEXEC, protocol));
    if (socket_fd.get() < 0) {
        result.error = std::string("ICMP socket: ") + strerror(errno);
        result.packets_failed = count;
        return result;
    }
    if (setsockopt(socket_fd.get(), SOL_SOCKET, SO_MARK, &fwmark, sizeof(fwmark)) < 0) {
        result.error = std::string("SO_MARK: ") + strerror(errno);
        result.packets_failed = count;
        return result;
    }

    sockaddr_storage local{};
    socklen_t local_len = family == AF_INET ? sizeof(sockaddr_in) : sizeof(sockaddr_in6);
    if (family == AF_INET) reinterpret_cast<sockaddr_in&>(local).sin_family = AF_INET;
    else reinterpret_cast<sockaddr_in6&>(local).sin6_family = AF_INET6;
    if (bind(socket_fd.get(), reinterpret_cast<sockaddr*>(&local), local_len) < 0) {
        result.error = std::string("ICMP bind: ") + strerror(errno);
        result.packets_failed = count;
        return result;
    }
    if (connect(socket_fd.get(), reinterpret_cast<sockaddr*>(&address), address_len) < 0) {
        result.error = std::string("ICMP connect: ") + strerror(errno);
        result.packets_failed = count;
        return result;
    }
    const uint16_t identifier = socket_identifier(socket_fd.get(), family);
    if (identifier == 0) {
        result.error = "ICMP socket identifier is unavailable";
        result.packets_failed = count;
        return result;
    }

    uint32_t failed = 0, accepted = 0;
    uint64_t total_ms = 0;
    for (uint32_t sequence = 0; sequence < count; ++sequence) {
        if (sequence) std::this_thread::sleep_for(std::chrono::milliseconds(packet_interval_ms));
        unsigned char packet[sizeof(icmphdr)]{};
        if (family == AF_INET) {
            auto* h = reinterpret_cast<icmphdr*>(packet); h->type = ICMP_ECHO;
            h->un.echo.id = htons(identifier); h->un.echo.sequence = htons(sequence);
        } else {
            auto* h = reinterpret_cast<icmp6_hdr*>(packet); h->icmp6_type = ICMP6_ECHO_REQUEST;
            h->icmp6_id = htons(identifier); h->icmp6_seq = htons(sequence);
        }
        const auto started = std::chrono::steady_clock::now();
        if (send(socket_fd.get(), packet, sizeof(packet), 0) < 0) {
            ++failed; *result.packets_failed = failed; continue;
        }
        ++*result.packets_sent;
        const auto deadline = started + std::chrono::milliseconds(timeout_ms);
        bool matched = false;
        while (std::chrono::steady_clock::now() < deadline) {
            const auto remaining = std::chrono::ceil<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now());
            pollfd pfd{socket_fd.get(), POLLIN, 0};
            const int poll_result = poll(&pfd, 1, static_cast<int>(std::max<int64_t>(1, remaining.count())));
            if (poll_result < 0 && errno == EINTR) continue;
            if (poll_result <= 0) break;
            sockaddr_storage source{};
            socklen_t source_len = sizeof(source);
            unsigned char reply[256];
            const auto size = recvfrom(socket_fd.get(), reply, sizeof(reply), 0,
                                       reinterpret_cast<sockaddr*>(&source), &source_len);
            if (size < 0) {
                if (errno == EINTR) continue;
                break;
            }
            if (!icmp_detail::reply_matches(family, reply, static_cast<size_t>(size),
                                            source, address, identifier,
                                            static_cast<uint16_t>(sequence))) continue;
            matched = true;
            ++*result.packets_received;
            const auto elapsed = std::chrono::steady_clock::now() - started;
            const auto rtt_ms = std::chrono::ceil<std::chrono::milliseconds>(elapsed).count();
            if (rtt_ms <= max_rtt_ms) {
                ++accepted;
                total_ms += static_cast<uint64_t>(rtt_ms);
            } else {
                ++failed;
            }
            break;
        }
        if (!matched) ++failed;
        *result.packets_failed = failed;
    }
    if (failed > max_failed) {
        result.error = "ICMP failed packets " + std::to_string(failed) + "/" + std::to_string(count);
        return result;
    }
    result.success = true;
    result.latency_ms = accepted ? static_cast<uint32_t>(total_ms / accepted) : 0;
    return result;
}

} // namespace keen_pbr3
