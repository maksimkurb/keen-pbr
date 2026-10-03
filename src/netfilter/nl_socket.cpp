#include "nl_socket.hpp"

#include <linux/netfilter/nfnetlink.h>
#include <linux/netlink.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <ctime>
#include <string>

#ifndef SOL_NETLINK
#define SOL_NETLINK 270
#endif
#ifndef NETLINK_NO_ENOBUFS
#define NETLINK_NO_ENOBUFS 5
#endif
#ifndef NETLINK_CAP_ACK
#define NETLINK_CAP_ACK 10
#endif
#ifndef NETLINK_EXT_ACK
#define NETLINK_EXT_ACK 11
#endif

namespace keen_pbr3::nfnl {

namespace {
constexpr std::size_t kMinRxBuf = 16384;
}

NlSocket::NlSocket(int rcvbuf_bytes, bool no_enobufs) {
    fd_ = ::socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC | SOCK_NONBLOCK, NETLINK_NETFILTER);
    if (fd_ < 0) {
        throw NlSocketError(std::string("netlink socket(): ") + std::strerror(errno));
    }

    sockaddr_nl addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.nl_family = AF_NETLINK;
    if (::bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        const int e = errno;
        close_fd();
        throw NlSocketError(std::string("netlink bind(): ") + std::strerror(e));
    }

    socklen_t alen = sizeof(addr);
    if (::getsockname(fd_, reinterpret_cast<sockaddr*>(&addr), &alen) == 0) {
        port_id_ = addr.nl_pid;
    }

    if (rcvbuf_bytes > 0) {
        if (::setsockopt(fd_, SOL_SOCKET, SO_RCVBUFFORCE, &rcvbuf_bytes, sizeof(rcvbuf_bytes)) < 0) {
            (void)::setsockopt(fd_, SOL_SOCKET, SO_RCVBUF, &rcvbuf_bytes, sizeof(rcvbuf_bytes));
        }
    }
    const int one = 1;
    if (no_enobufs) {
        (void)::setsockopt(fd_, SOL_NETLINK, NETLINK_NO_ENOBUFS, &one, sizeof(one));
    }
    (void)::setsockopt(fd_, SOL_NETLINK, NETLINK_EXT_ACK, &one, sizeof(one));
    (void)::setsockopt(fd_, SOL_NETLINK, NETLINK_CAP_ACK, &one, sizeof(one));

    seq_ = static_cast<uint32_t>(std::time(nullptr));
}

NlSocket::~NlSocket() {
    close_fd();
}

void NlSocket::close_fd() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

NlSocket::NlSocket(NlSocket&& other) noexcept
    : fd_(other.fd_), port_id_(other.port_id_), seq_(other.seq_), rx_(std::move(other.rx_)) {
    other.fd_ = -1;
}

NlSocket& NlSocket::operator=(NlSocket&& other) noexcept {
    if (this != &other) {
        close_fd();
        fd_ = other.fd_;
        port_id_ = other.port_id_;
        seq_ = other.seq_;
        rx_ = std::move(other.rx_);
        other.fd_ = -1;
    }
    return *this;
}

bool NlSocket::send(const uint8_t* data, std::size_t len, int& err) {
    sockaddr_nl dst;
    std::memset(&dst, 0, sizeof(dst));
    dst.nl_family = AF_NETLINK;
    for (;;) {
        const ssize_t n = ::sendto(fd_, data, len, 0, reinterpret_cast<sockaddr*>(&dst), sizeof(dst));
        if (n >= 0) {
            if (static_cast<std::size_t>(n) == len) { err = 0; return true; }
            err = EMSGSIZE;
            return false;
        }
        if (errno == EINTR) continue;
        err = errno;
        return false;
    }
}

ssize_t NlSocket::recv(std::vector<uint8_t>& buf, int& err) {
    if (buf.size() < kMinRxBuf) buf.resize(kMinRxBuf);
    for (;;) {
        const ssize_t n = ::recv(fd_, buf.data(), buf.size(), MSG_TRUNC);
        if (n < 0) {
            if (errno == EINTR) continue;
            err = errno;
            return -1;
        }
        if (static_cast<std::size_t>(n) > buf.size()) {
            buf.resize(static_cast<std::size_t>(n));
            err = EMSGSIZE;
            return -1;
        }
        err = 0;
        return n;
    }
}

int NlSocket::transact(const uint8_t* req, std::size_t len, uint32_t first_seq, uint32_t last_seq,
                       int timeout_ms,
                       const std::function<void(const MsgView&)>& on_msg,
                       const std::function<void(uint32_t seq, int err)>& on_ack) {
    int err = 0;
    if (!send(req, len, err)) return err;

    const uint32_t count = last_seq - first_seq + 1;  // wrap-safe
    std::vector<bool> acked(count, false);
    uint32_t pending = count;
    int first_error = 0;

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

    while (pending > 0) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) return first_error != 0 ? first_error : ETIMEDOUT;
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();

        pollfd pfd;
        pfd.fd = fd_;
        pfd.events = POLLIN;
        pfd.revents = 0;
        const int pr = ::poll(&pfd, 1, static_cast<int>(remaining) + 1);
        if (pr < 0) {
            if (errno == EINTR) continue;
            return errno;
        }
        if (pr == 0) continue;  // deadline re-checked at loop top

        for (;;) {
            const ssize_t n = recv(rx_, err);
            if (n < 0) {
                if (err == EAGAIN || err == EWOULDBLOCK) break;
                if (err == EMSGSIZE) continue;
                return err;
            }
            for_each_msg(ByteView(rx_.data(), static_cast<std::size_t>(n)), [&](const MsgView& m) {
                const uint32_t idx = m.seq - first_seq;
                if (idx >= count) return true;  // stale reply
                if (m.type == NLMSG_ERROR) {
                    int e = 0;
                    uint32_t orig = 0;
                    if (!parse_error(m, e, orig)) return true;
                    if (!acked[idx]) {
                        acked[idx] = true;
                        --pending;
                    }
                    if (e != 0 && first_error == 0) first_error = e;
                    if (on_ack) on_ack(m.seq, e);
                } else if (m.type == NLMSG_DONE) {
                    // A dump request completes with DONE rather than an ACK.
                    if (!acked[idx]) {
                        acked[idx] = true;
                        --pending;
                    }
                    if (on_ack) on_ack(m.seq, 0);
                } else if (on_msg) {
                    on_msg(m);
                }
                return true;
            });
        }
    }
    return first_error;
}

} // namespace keen_pbr3::nfnl
