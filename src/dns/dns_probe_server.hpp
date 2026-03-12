#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <sys/socket.h>
#include <vector>

#include "dns_server.hpp"

namespace keen_pbr3 {

struct DnsProbeListenAddress {
    std::string ip;
    uint16_t port{53};
};

struct DnsProbeServerSettings {
    std::string listen;
    std::string bind_ip;
    uint16_t port{53};
    std::string answer_ipv4;
};

struct DnsProbeQuestion {
    uint16_t id{0};
    uint16_t flags{0};
    std::string name;
    std::optional<std::string> ecs;
    std::vector<uint8_t> question_wire;
};

struct DnsProbeEvent {
    std::string domain;
    std::string source_ip;
    std::optional<std::string> ecs;
};

DnsProbeListenAddress parse_dns_probe_listen_address(const std::string& listen);
DnsProbeServerSettings parse_dns_probe_server_settings(const std::string& listen,
                                                       const std::string* answer_ipv4);
DnsProbeQuestion parse_dns_probe_query(std::span<const uint8_t> packet);
std::vector<uint8_t> build_dns_probe_response(const DnsProbeQuestion& question,
                                              const std::string& answer_ipv4);

class DnsProbeServer {
public:
    using QueryCallback = std::function<void(const DnsProbeEvent&)>;

    explicit DnsProbeServer(const DnsProbeServerSettings& settings,
                            QueryCallback on_query = {});
    ~DnsProbeServer();

    DnsProbeServer(const DnsProbeServer&) = delete;
    DnsProbeServer& operator=(const DnsProbeServer&) = delete;
    DnsProbeServer(DnsProbeServer&&) = delete;
    DnsProbeServer& operator=(DnsProbeServer&&) = delete;

    int udp_fd() const { return udp_fd_; }
    int tcp_fd() const { return tcp_fd_; }
    std::vector<int> all_fds() const;

    std::vector<int> accept_tcp_clients();
    bool handle_udp_readable();
    bool handle_tcp_client_readable(int fd);
    void remove_tcp_client(int fd);

private:
    struct TcpClientState {
        std::vector<uint8_t> buffer;
        uint16_t expected_size{0};
        bool have_size{false};
    };

    bool handle_udp_packet(const uint8_t* data, size_t len,
                           const struct sockaddr* addr, socklen_t addrlen);
    bool handle_tcp_packet(int fd, std::span<const uint8_t> packet);
    void publish_query(const DnsProbeQuestion& question, const std::string& source_ip) const;
    void close_fd(int& fd);

    DnsProbeServerSettings settings_;
    QueryCallback on_query_;
    int udp_fd_{-1};
    int tcp_fd_{-1};
    std::map<int, TcpClientState> tcp_clients_;
};

} // namespace keen_pbr3
