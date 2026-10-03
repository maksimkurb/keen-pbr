#pragma once

#include "nl_msg.hpp"

#include <sys/types.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <vector>

namespace keen_pbr3::nfnl {

class NlSocketError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
    // `code` is the errno-style kernel/transport error behind the failure (0 if unknown).
    NlSocketError(const std::string& what, int code) : std::runtime_error(what), code_(code) {}
    int code() const { return code_; }

private:
    int code_{0};
};

class NlSocket {
public:
    explicit NlSocket(int rcvbuf_bytes = 1 << 20, bool no_enobufs = false);
    ~NlSocket();
    NlSocket(const NlSocket&) = delete;
    NlSocket& operator=(const NlSocket&) = delete;
    NlSocket(NlSocket&& other) noexcept;
    NlSocket& operator=(NlSocket&& other) noexcept;

    int fd() const { return fd_; }
    uint32_t port_id() const { return port_id_; }
    uint32_t next_seq() { return ++seq_; }

    // Sends to the kernel (nl_pid=0). On failure returns false and sets err (errno value).
    bool send(const uint8_t* data, std::size_t len, int& err);

    // Receives one datagram into buf (grown to at least 16 KiB, never shrunk;
    // use the returned length). Returns bytes received or -1 with err set
    // (EAGAIN when nothing is pending, EMSGSIZE when the datagram was
    // truncated; buf is then grown so the next datagram fits).
    ssize_t recv(std::vector<uint8_t>& buf, int& err);

    // Sends `req`, then polls/receives until an NLMSG_ERROR/ACK (or NLMSG_DONE for dumps) has arrived for
    // every seq in [first_seq, last_seq] or the timeout expires. Replies with a
    // seq outside the range are ignored. Non-error messages go to on_msg.
    // Returns 0 if all succeeded, the first non-zero errno seen, or ETIMEDOUT.
    int transact(const uint8_t* req, std::size_t len, uint32_t first_seq, uint32_t last_seq,
                 int timeout_ms,
                 const std::function<void(const MsgView&)>& on_msg,
                 const std::function<void(uint32_t seq, int err)>& on_ack,
                 const std::vector<uint32_t>* extra_error_seqs = nullptr);

private:
    void close_fd();

    int fd_{-1};
    uint32_t port_id_{0};
    uint32_t seq_{0};
    std::vector<uint8_t> rx_;
};

} // namespace keen_pbr3::nfnl
