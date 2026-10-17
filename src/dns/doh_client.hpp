#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace keen_pbr3 {

// DNS-over-HTTPS client (RFC 8484, POST application/dns-message) on the curl
// multi interface, for a single-threaded epoll loop: curl tells which sockets
// and which timeout it wants, the owner of the loop registers them in its epoll
// and hands the events back. Every method must be called from the loop thread.
//
// Connections are reused (HTTP/2 with multiplexing when libcurl has it,
// HTTP/1.1 keep-alive otherwise). The addresses of the DoH hosts are given by
// the caller and pinned (CURLOPT_RESOLVE), so a transfer never resolves a name
// by itself.
struct DohClientOptions {
    int epoll_fd{-1};
    // Epoll `data.u64` of a socket of curl is (tag_kind << 32) | fd.
    std::uint32_t tag_kind{0};
    // Trust anchors; empty: libcurl's built-in ones.
    std::string ca_path;
    bool ca_is_directory{false};
    // Tests only: also allow http:// URLs.
    bool allow_plain_http{false};
    // Most transfers at once.
    std::size_t max_in_flight{32};
};

struct DohResult {
    bool ok{false};                 // HTTP 200 with a DNS message
    std::string error;              // when !ok
    bool connection_problem{false}; // could not connect/resolve/handshake: re-resolve the host
    const std::uint8_t* body{nullptr};  // valid during the callback only
    std::size_t body_len{0};
};

class DohClient {
public:
    // Called from handle_socket_event()/handle_timeout() when a transfer is
    // over (not when it was cancelled). `user` is the value given to start().
    using DoneFn = void (*)(void* context, std::uint64_t user, const DohResult& result);

    DohClient(const DohClientOptions& options, DoneFn done, void* context);
    ~DohClient();
    DohClient(const DohClient&) = delete;
    DohClient& operator=(const DohClient&) = delete;

    bool valid() const;

    // A DoH server: its URL, the addresses its host is pinned to and the
    // firewall mark of its connections. Returns an id for start(), or
    // kNoEndpoint when the URL is not usable.
    static constexpr std::size_t kNoEndpoint = static_cast<std::size_t>(-1);
    std::size_t add_endpoint(const std::string& url, std::uint32_t fwmark);
    // Pins the host of the endpoint to `ips` (empty: unresolved, transfers
    // fail at once).
    void set_addresses(std::size_t endpoint, const std::vector<std::string>& ips);
    const std::string& endpoint_host(std::size_t endpoint) const;
    bool endpoint_ready(std::size_t endpoint) const;
    // Forgets every endpoint (cancels their transfers).
    void clear_endpoints();

    // Sends `query` (a DNS message). The id of the message is sent as zero;
    // the answer comes back with whatever id the server used, the caller
    // restores its own. Returns a transfer id for cancel(), or kNoTransfer.
    static constexpr std::size_t kNoTransfer = static_cast<std::size_t>(-1);
    std::size_t start(std::size_t endpoint, const std::uint8_t* query, std::size_t len,
                      std::chrono::milliseconds timeout, std::uint64_t user);
    void cancel(std::size_t transfer);

    // A socket curl asked for became ready (events are EPOLL* flags).
    void handle_socket_event(int fd, std::uint32_t events);
    // The deadline of next_timeout_ms() passed.
    void handle_timeout();
    // Milliseconds until handle_timeout() is due, -1 when curl wants none.
    int next_timeout_ms() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace keen_pbr3
