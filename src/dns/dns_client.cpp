#include "dns_client.hpp"

#include "dns_wire.hpp"
#include "../util/byte_view.hpp"

#include <arpa/inet.h>
#include <algorithm>
#include <cerrno>
#include <exception>
#include <cstring>
#include <netinet/in.h>
#include <poll.h>
#include <random>
#include <sys/socket.h>
#include <unistd.h>

namespace keen_pbr3 {

namespace {

constexpr std::size_t kMaxUdpResponse = 4096;

struct Fd {
    int fd{-1};
    explicit Fd(int f) : fd(f) {}
    ~Fd() { if (fd >= 0) ::close(fd); }
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
};

DnsQueryResult fail(DnsQueryStatus status, std::string error) {
    DnsQueryResult r;
    r.status = status;
    r.error = std::move(error);
    return r;
}

std::string errno_text(const char* what, int err) {
    return std::string(what) + ": " + std::strerror(err);
}

bool make_sockaddr(const std::string& address, std::uint16_t port,
                   sockaddr_storage& target, socklen_t& target_len) {
    target = {};
    target_len = 0;
    sockaddr_in v4{};
    sockaddr_in6 v6{};
    if (inet_pton(AF_INET, address.c_str(), &v4.sin_addr) == 1) {
        v4.sin_family = AF_INET;
        v4.sin_port = htons(port);
        std::memcpy(&target, &v4, sizeof(v4));
        target_len = sizeof(v4);
        return true;
    }
    if (inet_pton(AF_INET6, address.c_str(), &v6.sin6_addr) == 1) {
        v6.sin6_family = AF_INET6;
        v6.sin6_port = htons(port);
        std::memcpy(&target, &v6, sizeof(v6));
        target_len = sizeof(v6);
        return true;
    }
    return false;
}

std::string lowercase(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
    }
    return out;
}

std::string normalize_name(std::string_view name) {
    if (!name.empty() && name.back() == '.') name.remove_suffix(1);
    return lowercase(name);
}

using Clock = std::chrono::steady_clock;

int remaining_ms(Clock::time_point deadline) {
    const auto r = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
    return r.count() > 0 ? static_cast<int>(r.count()) : 0;
}

enum class PollResult { Ready, Timeout, Error };

PollResult poll_fd(int fd, short events, Clock::time_point deadline, int& err) {
    while (true) {
        const int ms = remaining_ms(deadline);
        if (ms <= 0) return PollResult::Timeout;
        pollfd pfd{fd, events, 0};
        const int ready = ::poll(&pfd, 1, ms);
        if (ready < 0) {
            if (errno == EINTR) continue;
            err = errno;
            return PollResult::Error;
        }
        if (ready == 0) return PollResult::Timeout;
        return PollResult::Ready;
    }
}

bool matches(const dns_wire::ParsedResponse& r, std::uint16_t id, std::uint16_t qtype,
             std::uint16_t qclass, const std::string& qname) {
    return r.id == id && r.qtype == qtype && r.qclass == qclass && lowercase(r.qname) == qname;
}

DnsQueryResult map_response(const dns_wire::ParsedResponse& resp, std::uint16_t qtype) {
    DnsQueryResult r;
    r.rcode = resp.rcode;
    r.cname_chain = resp.cname_chain;
    switch (resp.rcode) {
    case 0: {
        for (const auto& rec : resp.addresses) {
            char buf[INET6_ADDRSTRLEN] = {};
            if (rec.family == 4 && qtype == kDnsTypeA) {
                if (inet_ntop(AF_INET, rec.addr.data(), buf, sizeof(buf))) r.addresses.emplace_back(buf);
            } else if (rec.family == 6 && qtype == kDnsTypeAAAA) {
                if (inet_ntop(AF_INET6, rec.addr.data(), buf, sizeof(buf))) r.addresses.emplace_back(buf);
            }
        }
        r.status = r.addresses.empty() ? DnsQueryStatus::NoData : DnsQueryStatus::Ok;
        break;
    }
    case 3: r.status = DnsQueryStatus::NxDomain; break;
    case 2: r.status = DnsQueryStatus::ServFail; break;
    case 5: r.status = DnsQueryStatus::Refused; break;
    default: r.status = DnsQueryStatus::OtherRcode; break;
    }
    return r;
}

// Sends all of `data`; false + err on failure/timeout (err == 0 for timeout).
bool tcp_send_all(int fd, const std::uint8_t* data, std::size_t size, Clock::time_point deadline,
                  int& err) {
    std::size_t sent = 0;
    while (sent < size) {
        const ssize_t n = ::send(fd, data + sent, size - sent, MSG_NOSIGNAL);
        if (n > 0) { sent += static_cast<std::size_t>(n); continue; }
        if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            err = errno;
            return false;
        }
        err = 0;
        const PollResult p = poll_fd(fd, POLLOUT, deadline, err);
        if (p == PollResult::Timeout) { err = 0; return false; }
        if (p == PollResult::Error) return false;
    }
    return true;
}

bool tcp_read_exact(int fd, std::uint8_t* out, std::size_t size, Clock::time_point deadline,
                    int& err, bool& eof) {
    std::size_t got = 0;
    eof = false;
    while (got < size) {
        const ssize_t n = ::recv(fd, out + got, size - got, 0);
        if (n > 0) { got += static_cast<std::size_t>(n); continue; }
        if (n == 0) { eof = true; return false; }
        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            err = errno;
            return false;
        }
        err = 0;
        const PollResult p = poll_fd(fd, POLLIN, deadline, err);
        if (p == PollResult::Timeout) { err = 0; return false; }
        if (p == PollResult::Error) return false;
    }
    return true;
}

DnsQueryResult query_tcp(const sockaddr_storage& target, socklen_t target_len,
                         const std::vector<std::uint8_t>& query, std::uint16_t id,
                         std::uint16_t qtype, std::uint16_t qclass, const std::string& qname,
                         std::chrono::milliseconds timeout) {
    const auto deadline = Clock::now() + timeout;
    Fd sock(::socket(target.ss_family, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0));
    if (sock.fd < 0) return fail(DnsQueryStatus::NetworkError, errno_text("socket", errno));

    if (::connect(sock.fd, reinterpret_cast<const sockaddr*>(&target), target_len) != 0) {
        if (errno != EINPROGRESS) {
            return fail(DnsQueryStatus::NetworkError, errno_text("connect", errno));
        }
        int err = 0;
        const PollResult p = poll_fd(sock.fd, POLLOUT, deadline, err);
        if (p == PollResult::Timeout) return fail(DnsQueryStatus::Timeout, "tcp connect timed out");
        if (p == PollResult::Error) return fail(DnsQueryStatus::NetworkError, errno_text("poll", err));
        int so_err = 0;
        socklen_t len = sizeof(so_err);
        if (::getsockopt(sock.fd, SOL_SOCKET, SO_ERROR, &so_err, &len) != 0) so_err = errno;
        if (so_err != 0) return fail(DnsQueryStatus::NetworkError, errno_text("connect", so_err));
    }

    std::vector<std::uint8_t> framed;
    framed.reserve(query.size() + 2);
    framed.push_back(static_cast<std::uint8_t>(query.size() >> 8));
    framed.push_back(static_cast<std::uint8_t>(query.size() & 0xFF));
    framed.insert(framed.end(), query.begin(), query.end());

    int err = 0;
    if (!tcp_send_all(sock.fd, framed.data(), framed.size(), deadline, err)) {
        if (err == 0) return fail(DnsQueryStatus::Timeout, "tcp send timed out");
        return fail(DnsQueryStatus::NetworkError, errno_text("send", err));
    }

    std::uint8_t lenbuf[2];
    bool eof = false;
    if (!tcp_read_exact(sock.fd, lenbuf, 2, deadline, err, eof)) {
        if (eof) return fail(DnsQueryStatus::NetworkError, "tcp connection closed");
        if (err == 0) return fail(DnsQueryStatus::Timeout, "tcp read timed out");
        return fail(DnsQueryStatus::NetworkError, errno_text("recv", err));
    }
    const std::size_t msg_len = (static_cast<std::size_t>(lenbuf[0]) << 8) | lenbuf[1];
    if (msg_len == 0) return fail(DnsQueryStatus::Malformed, "empty tcp DNS message");
    std::vector<std::uint8_t> msg(msg_len);
    if (!tcp_read_exact(sock.fd, msg.data(), msg_len, deadline, err, eof)) {
        if (eof) return fail(DnsQueryStatus::Malformed, "tcp message truncated");
        if (err == 0) return fail(DnsQueryStatus::Timeout, "tcp read timed out");
        return fail(DnsQueryStatus::NetworkError, errno_text("recv", err));
    }

    dns_wire::ParsedResponse resp;
    if (!dns_wire::parse_response(ByteView(msg.data(), msg.size()), resp)) {
        return fail(DnsQueryStatus::Malformed, "malformed DNS response");
    }
    if (!matches(resp, id, qtype, qclass, qname)) {
        return fail(DnsQueryStatus::Malformed, "DNS response does not match query");
    }
    DnsQueryResult r = map_response(resp, qtype);
    r.via_tcp = true;
    return r;
}

} // namespace

const char* dns_query_status_name(DnsQueryStatus s) {
    switch (s) {
    case DnsQueryStatus::Ok: return "ok";
    case DnsQueryStatus::NoData: return "nodata";
    case DnsQueryStatus::NxDomain: return "nxdomain";
    case DnsQueryStatus::ServFail: return "servfail";
    case DnsQueryStatus::Refused: return "refused";
    case DnsQueryStatus::OtherRcode: return "rcode";
    case DnsQueryStatus::Timeout: return "timeout";
    case DnsQueryStatus::NetworkError: return "network_error";
    case DnsQueryStatus::Malformed: return "malformed";
    }
    return "network_error";
}

std::vector<std::uint8_t> build_dns_query(std::uint16_t id, std::string_view name,
                                          std::uint16_t qtype, std::uint16_t qclass, bool edns0) {
    std::vector<std::uint8_t> packet;
    packet.reserve(32 + name.size());
    packet.push_back(static_cast<std::uint8_t>(id >> 8));
    packet.push_back(static_cast<std::uint8_t>(id & 0xFF));
    packet.push_back(0x01);  // RD
    packet.push_back(0x00);
    packet.push_back(0x00); packet.push_back(0x01);  // QDCOUNT
    packet.push_back(0x00); packet.push_back(0x00);  // ANCOUNT
    packet.push_back(0x00); packet.push_back(0x00);  // NSCOUNT
    packet.push_back(0x00); packet.push_back(edns0 ? 0x01 : 0x00);  // ARCOUNT

    const std::string trimmed = normalize_name(name);
    std::size_t start = 0;
    while (start < trimmed.size()) {
        std::size_t dot = trimmed.find('.', start);
        if (dot == std::string::npos) dot = trimmed.size();
        const std::size_t len = std::min<std::size_t>(dot - start, 63);
        packet.push_back(static_cast<std::uint8_t>(len));
        packet.insert(packet.end(), trimmed.begin() + static_cast<std::ptrdiff_t>(start),
                      trimmed.begin() + static_cast<std::ptrdiff_t>(start + len));
        start = dot + 1;
    }
    packet.push_back(0x00);
    packet.push_back(static_cast<std::uint8_t>(qtype >> 8));
    packet.push_back(static_cast<std::uint8_t>(qtype & 0xFF));
    packet.push_back(static_cast<std::uint8_t>(qclass >> 8));
    packet.push_back(static_cast<std::uint8_t>(qclass & 0xFF));
    if (edns0) {
        static const std::uint8_t opt[] = {0x00, 0x00, 0x29, 0x04, 0xD0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        packet.insert(packet.end(), std::begin(opt), std::end(opt));
    }
    return packet;
}

DnsQueryResult dns_query(const std::string& address, std::uint16_t port, std::string_view name,
                         std::uint16_t qtype, std::uint16_t qclass,
                         std::chrono::milliseconds timeout) {
    try {
        sockaddr_storage target{};
        socklen_t target_len = 0;
        if (!make_sockaddr(address, port, target, target_len)) {
            return fail(DnsQueryStatus::NetworkError, "invalid address");
        }

        thread_local std::mt19937 rng{std::random_device{}()};
        const auto id = static_cast<std::uint16_t>(rng() & 0xFFFF);
        const std::vector<std::uint8_t> query =
            build_dns_query(id, name, qtype, qclass, qclass == kDnsClassIN);
        const std::string qname = normalize_name(name);

        Fd sock(::socket(target.ss_family, SOCK_DGRAM | SOCK_CLOEXEC, 0));
        if (sock.fd < 0) return fail(DnsQueryStatus::NetworkError, errno_text("socket", errno));
        if (::connect(sock.fd, reinterpret_cast<const sockaddr*>(&target), target_len) != 0) {
            return fail(DnsQueryStatus::NetworkError, errno_text("connect", errno));
        }
        if (::send(sock.fd, query.data(), query.size(), 0) < 0) {
            if (errno == ECONNREFUSED) return fail(DnsQueryStatus::NetworkError, "connection refused");
            return fail(DnsQueryStatus::NetworkError, errno_text("send", errno));
        }

        const auto deadline = Clock::now() + timeout;
        std::vector<std::uint8_t> buffer(kMaxUdpResponse);
        while (true) {
            int err = 0;
            const PollResult p = poll_fd(sock.fd, POLLIN, deadline, err);
            if (p == PollResult::Timeout) return fail(DnsQueryStatus::Timeout, "timed out");
            if (p == PollResult::Error) return fail(DnsQueryStatus::NetworkError, errno_text("poll", err));
            const ssize_t got = ::recv(sock.fd, buffer.data(), buffer.size(), 0);
            if (got < 0) {
                if (errno == EINTR || errno == EAGAIN) continue;
                if (errno == ECONNREFUSED) return fail(DnsQueryStatus::NetworkError, "connection refused");
                return fail(DnsQueryStatus::NetworkError, errno_text("recv", errno));
            }
            dns_wire::ParsedResponse resp;
            if (!dns_wire::parse_response(ByteView(buffer.data(), static_cast<std::size_t>(got)), resp)) {
                continue;
            }
            if (!matches(resp, id, qtype, qclass, qname)) continue;
            if (resp.truncated) {
                return query_tcp(target, target_len, query, id, qtype, qclass, qname, timeout);
            }
            return map_response(resp, qtype);
        }
    } catch (const std::exception& e) {
        return fail(DnsQueryStatus::NetworkError, e.what());
    }
}

bool dns_probe_responding(const std::string& address, std::uint16_t port,
                          std::chrono::milliseconds timeout) {
    const DnsQueryResult r = dns_query(address, port, "version.bind", kDnsTypeTXT, kDnsClassCH, timeout);
    return r.status != DnsQueryStatus::Timeout && r.status != DnsQueryStatus::NetworkError &&
           r.status != DnsQueryStatus::Malformed;
}

} // namespace keen_pbr3
