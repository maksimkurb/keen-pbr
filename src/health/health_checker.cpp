#include "health_checker.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <netinet/ip_icmp.h>
#include <netinet/icmp6.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace keen_pbr3 {

namespace {

// Compute ICMP checksum (RFC 1071)
uint16_t icmp_checksum(const void* data, size_t len) {
    auto* buf = static_cast<const uint16_t*>(data);
    uint32_t sum = 0;
    for (; len > 1; len -= 2) {
        sum += *buf++;
    }
    if (len == 1) {
        sum += *reinterpret_cast<const uint8_t*>(buf);
    }
    sum = (sum >> 16) + (sum & 0xFFFF);
    sum += (sum >> 16);
    return static_cast<uint16_t>(~sum);
}

// Determine if an address string is IPv6
bool is_ipv6(const std::string& addr) {
    return addr.find(':') != std::string::npos;
}

} // anonymous namespace

void HealthChecker::register_target(const std::string& tag,
                                     const std::string& interface,
                                     const std::string& target,
                                     std::chrono::seconds timeout) {
    PingTarget pt;
    pt.tag = tag;
    pt.interface = interface;
    pt.target = target;
    pt.timeout = timeout;

    targets_[tag] = std::move(pt);

    // Initialize result as unknown
    HealthResult result;
    result.tag = tag;
    result.status = HealthStatus::unknown;
    results_[tag] = result;
}

bool HealthChecker::check(const std::string& tag) {
    auto it = targets_.find(tag);
    if (it == targets_.end()) {
        // No ping target configured - consider healthy by default
        return true;
    }

    bool ok = ping(it->second);

    HealthResult& result = results_[tag];
    result.tag = tag;
    result.status = ok ? HealthStatus::healthy : HealthStatus::unhealthy;
    result.detail = ok ? "ping ok" : "ping failed";

    return ok;
}

std::map<std::string, HealthResult> HealthChecker::check_all() {
    for (auto& [tag, target] : targets_) {
        check(tag);
    }
    return results_;
}

HealthStatus HealthChecker::last_status(const std::string& tag) const {
    auto it = results_.find(tag);
    if (it == results_.end()) {
        return HealthStatus::unknown;
    }
    return it->second.status;
}

const std::map<std::string, HealthResult>& HealthChecker::results() const {
    return results_;
}

bool HealthChecker::has_target(const std::string& tag) const {
    return targets_.count(tag) > 0;
}

bool HealthChecker::ping(const PingTarget& target) {
    bool v6 = is_ipv6(target.target);
    int family = v6 ? AF_INET6 : AF_INET;
    int proto = v6 ? static_cast<int>(IPPROTO_ICMPV6) : static_cast<int>(IPPROTO_ICMP);

    // Create raw ICMP socket
    int sockfd = socket(family, SOCK_DGRAM, proto);
    if (sockfd < 0) {
        return false;
    }

    // Bind to interface
    if (setsockopt(sockfd, SOL_SOCKET, SO_BINDTODEVICE,
                   target.interface.c_str(), target.interface.size() + 1) < 0) {
        close(sockfd);
        return false;
    }

    // Set send/receive timeout
    struct timeval tv;
    tv.tv_sec = target.timeout.count();
    tv.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    // Resolve target address
    bool ok = false;

    if (v6) {
        struct sockaddr_in6 addr{};
        addr.sin6_family = AF_INET6;
        if (inet_pton(AF_INET6, target.target.c_str(), &addr.sin6_addr) != 1) {
            close(sockfd);
            return false;
        }

        // Build ICMPv6 echo request
        struct icmp6_hdr icmp{};
        icmp.icmp6_type = ICMP6_ECHO_REQUEST;
        icmp.icmp6_code = 0;
        icmp.icmp6_id = htons(static_cast<uint16_t>(getpid() & 0xFFFF));
        icmp.icmp6_seq = htons(1);
        // ICMPv6 checksum is computed by the kernel

        if (sendto(sockfd, &icmp, sizeof(icmp), 0,
                   reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) > 0) {
            // Wait for reply using poll
            struct pollfd pfd;
            pfd.fd = sockfd;
            pfd.events = POLLIN;
            int timeout_ms = static_cast<int>(target.timeout.count() * 1000);

            if (poll(&pfd, 1, timeout_ms) > 0) {
                char buf[128];
                ssize_t n = recv(sockfd, buf, sizeof(buf), 0);
                if (n >= static_cast<ssize_t>(sizeof(struct icmp6_hdr))) {
                    auto* reply = reinterpret_cast<struct icmp6_hdr*>(buf);
                    if (reply->icmp6_type == ICMP6_ECHO_REPLY) {
                        ok = true;
                    }
                }
            }
        }
    } else {
        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        if (inet_pton(AF_INET, target.target.c_str(), &addr.sin_addr) != 1) {
            close(sockfd);
            return false;
        }

        // Build ICMP echo request
        struct icmphdr icmp{};
        icmp.type = ICMP_ECHO;
        icmp.code = 0;
        icmp.un.echo.id = htons(static_cast<uint16_t>(getpid() & 0xFFFF));
        icmp.un.echo.sequence = htons(1);
        icmp.checksum = 0;
        icmp.checksum = icmp_checksum(&icmp, sizeof(icmp));

        if (sendto(sockfd, &icmp, sizeof(icmp), 0,
                   reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) > 0) {
            // Wait for reply using poll
            struct pollfd pfd;
            pfd.fd = sockfd;
            pfd.events = POLLIN;
            int timeout_ms = static_cast<int>(target.timeout.count() * 1000);

            if (poll(&pfd, 1, timeout_ms) > 0) {
                char buf[128];
                ssize_t n = recv(sockfd, buf, sizeof(buf), 0);
                if (n >= static_cast<ssize_t>(sizeof(struct icmphdr))) {
                    // For SOCK_DGRAM ICMP sockets, kernel strips IP header
                    auto* reply = reinterpret_cast<struct icmphdr*>(buf);
                    if (reply->type == ICMP_ECHOREPLY) {
                        ok = true;
                    }
                }
            }
        }
    }

    close(sockfd);
    return ok;
}

} // namespace keen_pbr3
