#pragma once

// Test helper: an in-process DNS server on 127.0.0.1 (UDP + TCP on the same
// ephemeral port) answering from a scripted handler.
//
//   FakeDnsServer srv([](const Bytes& query, bool tcp) {
//       return fake_dns::make_reply(query, 0, {fake_dns::a_rr("192.0.2.10")});
//   });
//   dns_query("127.0.0.1", srv.port(), ...);
//
// The handler returns either one reply (std::vector<uint8_t>; empty = send
// nothing) or a list of replies (std::vector<std::vector<uint8_t>>, sent in
// order over UDP; over TCP each is length-prefixed on the same connection).

#include <arpa/inet.h>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <functional>
#include <netinet/in.h>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <thread>
#include <type_traits>
#include <unistd.h>
#include <vector>

namespace fake_dns {

using Bytes = std::vector<std::uint8_t>;

struct Rr {
    std::uint16_t type{1};
    std::uint16_t cls{1};
    std::uint32_t ttl{60};
    Bytes rdata;
};

inline Rr a_rr(const std::string& ip, std::uint32_t ttl = 60) {
    Rr r;
    r.type = 1;
    r.ttl = ttl;
    r.rdata.resize(4);
    inet_pton(AF_INET, ip.c_str(), r.rdata.data());
    return r;
}

inline Rr aaaa_rr(const std::string& ip, std::uint32_t ttl = 60) {
    Rr r;
    r.type = 28;
    r.ttl = ttl;
    r.rdata.resize(16);
    inet_pton(AF_INET6, ip.c_str(), r.rdata.data());
    return r;
}

inline Rr txt_rr(const std::string& text, std::uint32_t ttl = 60) {
    Rr r;
    r.type = 16;
    r.ttl = ttl;
    r.rdata.push_back(static_cast<std::uint8_t>(text.size()));
    r.rdata.insert(r.rdata.end(), text.begin(), text.end());
    return r;
}

// Builds a response for `query`: copies id + question, sets QR/RD/RA, RCODE and
// TC, appends `answers` using the 0xC00C name pointer. Returns {} when the
// query has no parsable question.
inline Bytes make_reply(const Bytes& query, std::uint8_t rcode,
                        const std::vector<Rr>& answers = {}, bool truncated = false,
                        int id_override = -1) {
    if (query.size() < 12) return {};
    std::size_t pos = 12;
    while (pos < query.size() && query[pos] != 0) pos += 1u + query[pos];
    pos += 1;  // root label
    const std::size_t question_end = pos + 4;
    if (question_end > query.size()) return {};

    Bytes out(query.begin(), query.begin() + 12);
    if (id_override >= 0) {
        out[0] = static_cast<std::uint8_t>(id_override >> 8);
        out[1] = static_cast<std::uint8_t>(id_override & 0xFF);
    }
    out[2] = static_cast<std::uint8_t>(0x80 | 0x01 | (truncated ? 0x02 : 0));  // QR RD [TC]
    out[3] = static_cast<std::uint8_t>(0x80 | (rcode & 0x0F));                 // RA
    out[4] = 0; out[5] = 1;
    out[6] = static_cast<std::uint8_t>(answers.size() >> 8);
    out[7] = static_cast<std::uint8_t>(answers.size() & 0xFF);
    out[8] = out[9] = out[10] = out[11] = 0;
    out.insert(out.end(), query.begin() + 12, query.begin() + static_cast<std::ptrdiff_t>(question_end));
    for (const Rr& rr : answers) {
        out.push_back(0xC0); out.push_back(0x0C);
        out.push_back(static_cast<std::uint8_t>(rr.type >> 8));
        out.push_back(static_cast<std::uint8_t>(rr.type & 0xFF));
        out.push_back(static_cast<std::uint8_t>(rr.cls >> 8));
        out.push_back(static_cast<std::uint8_t>(rr.cls & 0xFF));
        for (int shift = 24; shift >= 0; shift -= 8) {
            out.push_back(static_cast<std::uint8_t>((rr.ttl >> shift) & 0xFF));
        }
        out.push_back(static_cast<std::uint8_t>(rr.rdata.size() >> 8));
        out.push_back(static_cast<std::uint8_t>(rr.rdata.size() & 0xFF));
        out.insert(out.end(), rr.rdata.begin(), rr.rdata.end());
    }
    return out;
}

// Returns a loopback UDP port nothing listens on (bound then closed).
inline std::uint16_t unused_udp_port() {
    const int fd = ::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ::bind(fd, reinterpret_cast<sockaddr*>(&a), sizeof(a));
    socklen_t len = sizeof(a);
    ::getsockname(fd, reinterpret_cast<sockaddr*>(&a), &len);
    ::close(fd);
    return ntohs(a.sin_port);
}

} // namespace fake_dns

class FakeDnsServer {
public:
    using Bytes = fake_dns::Bytes;
    using Handler = std::function<std::vector<Bytes>(const Bytes& query, bool tcp)>;

    // Accepts handlers returning Bytes or std::vector<Bytes>.
    template <typename F>
    explicit FakeDnsServer(F handler) {
        if constexpr (std::is_same_v<std::invoke_result_t<F, const Bytes&, bool>, Bytes>) {
            handler_ = [handler](const Bytes& q, bool tcp) {
                std::vector<Bytes> r;
                Bytes one = handler(q, tcp);
                if (!one.empty()) r.push_back(std::move(one));
                return r;
            };
        } else {
            handler_ = handler;
        }
        start();
    }

    FakeDnsServer(const FakeDnsServer&) = delete;
    FakeDnsServer& operator=(const FakeDnsServer&) = delete;

    ~FakeDnsServer() {
        const std::uint64_t one = 1;
        if (stop_fd_ >= 0) { auto r = ::write(stop_fd_, &one, sizeof(one)); (void)r; }
        if (thread_.joinable()) thread_.join();
        for (int fd : {udp_fd_, tcp_fd_, stop_fd_}) if (fd >= 0) ::close(fd);
    }

    std::uint16_t port() const { return port_; }

private:
    void start() {
        for (int attempt = 0; attempt < 50; ++attempt) {
            udp_fd_ = ::socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
            tcp_fd_ = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
            sockaddr_in a{};
            a.sin_family = AF_INET;
            a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            socklen_t len = sizeof(a);
            if (::bind(udp_fd_, reinterpret_cast<sockaddr*>(&a), sizeof(a)) == 0 &&
                ::getsockname(udp_fd_, reinterpret_cast<sockaddr*>(&a), &len) == 0) {
                port_ = ntohs(a.sin_port);
                int on = 1;
                ::setsockopt(tcp_fd_, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
                if (::bind(tcp_fd_, reinterpret_cast<sockaddr*>(&a), sizeof(a)) == 0 &&
                    ::listen(tcp_fd_, 8) == 0) {
                    break;
                }
            }
            ::close(udp_fd_); ::close(tcp_fd_);
            udp_fd_ = tcp_fd_ = -1;
        }
        if (udp_fd_ < 0) throw std::runtime_error("FakeDnsServer: cannot bind udp+tcp port");
        stop_fd_ = ::eventfd(0, EFD_CLOEXEC);
        thread_ = std::thread([this] { run(); });
    }

    void run() {
        while (true) {
            pollfd fds[3] = {{stop_fd_, POLLIN, 0}, {udp_fd_, POLLIN, 0}, {tcp_fd_, POLLIN, 0}};
            if (::poll(fds, 3, -1) < 0) {
                if (errno == EINTR) continue;
                return;
            }
            if (fds[0].revents & POLLIN) return;
            if (fds[1].revents & POLLIN) serve_udp();
            if (fds[2].revents & POLLIN) serve_tcp();
        }
    }

    void serve_udp() {
        Bytes buf(4096);
        sockaddr_storage peer{};
        socklen_t peer_len = sizeof(peer);
        const ssize_t n = ::recvfrom(udp_fd_, buf.data(), buf.size(), 0,
                                     reinterpret_cast<sockaddr*>(&peer), &peer_len);
        if (n <= 0) return;
        buf.resize(static_cast<std::size_t>(n));
        for (const Bytes& reply : handler_(buf, false)) {
            ::sendto(udp_fd_, reply.data(), reply.size(), 0,
                     reinterpret_cast<sockaddr*>(&peer), peer_len);
        }
    }

    static bool read_exact(int fd, std::uint8_t* out, std::size_t size) {
        std::size_t got = 0;
        while (got < size) {
            pollfd p{fd, POLLIN, 0};
            if (::poll(&p, 1, 1000) <= 0) return false;
            const ssize_t n = ::recv(fd, out + got, size - got, 0);
            if (n <= 0) return false;
            got += static_cast<std::size_t>(n);
        }
        return true;
    }

    void serve_tcp() {
        const int conn = ::accept4(tcp_fd_, nullptr, nullptr, SOCK_CLOEXEC);
        if (conn < 0) return;
        std::uint8_t lenbuf[2];
        if (read_exact(conn, lenbuf, 2)) {
            Bytes query((static_cast<std::size_t>(lenbuf[0]) << 8) | lenbuf[1]);
            if (read_exact(conn, query.data(), query.size())) {
                for (const Bytes& reply : handler_(query, true)) {
                    Bytes framed{static_cast<std::uint8_t>(reply.size() >> 8),
                                 static_cast<std::uint8_t>(reply.size() & 0xFF)};
                    framed.insert(framed.end(), reply.begin(), reply.end());
                    ::send(conn, framed.data(), framed.size(), MSG_NOSIGNAL);
                }
            }
        }
        ::close(conn);
    }

    Handler handler_;
    int udp_fd_{-1};
    int tcp_fd_{-1};
    int stop_fd_{-1};
    std::uint16_t port_{0};
    std::thread thread_;
};
