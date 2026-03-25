#include "dns_probe_server.hpp"

#include <arpa/inet.h>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../log/logger.hpp"
#include "dns_server.hpp"

namespace keen_pbr3 {

namespace {

constexpr size_t DNS_HEADER_SIZE = 12;
constexpr uint16_t DNS_TYPE_A = 1;
constexpr uint16_t DNS_CLASS_IN = 1;
constexpr uint16_t DNS_TYPE_OPT = 41;
constexpr uint16_t DNS_FLAG_QR = 0x8000;
constexpr uint16_t DNS_FLAG_RD = 0x0100;
constexpr uint16_t DNS_EDNS_OPTION_ECS = 8;
constexpr size_t kMaxTcpClients = 16;
constexpr size_t kMaxTcpBufferSize = 16384;

bool is_valid_ipv4(const std::string& ip) {
    struct in_addr addr {};
    return inet_pton(AF_INET, ip.c_str(), &addr) == 1;
}

std::string normalize_dns_name(const std::vector<std::string>& labels) {
    std::string name;
    for (size_t i = 0; i < labels.size(); ++i) {
        if (i > 0) name.push_back('.');
        name += labels[i];
    }
    if (name.empty()) {
        return ".";
    }
    return name;
}

int make_socket_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        throw DnsError("fcntl(F_GETFL) failed: " + std::string(strerror(errno)));
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        throw DnsError("fcntl(F_SETFL) failed: " + std::string(strerror(errno)));
    }
    return fd;
}

int create_bound_socket(int type, const DnsProbeServerSettings& settings) {
    int fd = socket(AF_INET, type | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        throw DnsError("socket() failed: " + std::string(strerror(errno)));
    }

    try {
        make_socket_nonblocking(fd);

        int one = 1;
        if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) < 0) {
            throw DnsError("setsockopt(SO_REUSEADDR) failed: " + std::string(strerror(errno)));
        }

        sockaddr_in addr {};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(settings.port);
        if (inet_pton(AF_INET, settings.bind_ip.c_str(), &addr.sin_addr) != 1) {
            throw DnsError("Invalid IPv4 address: " + settings.bind_ip);
        }

        if (bind(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) < 0) {
            throw DnsError("bind(" + settings.listen + ") failed: " + std::string(strerror(errno)));
        }

        if (type == SOCK_STREAM && listen(fd, SOMAXCONN) < 0) {
            throw DnsError("listen(" + settings.listen + ") failed: " + std::string(strerror(errno)));
        }

        return fd;
    } catch (...) {
        close(fd);
        throw;
    }
}

void append_u16(std::vector<uint8_t>& out, uint16_t value) {
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(value & 0xFF));
}

void append_u32(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(value & 0xFF));
}

bool write_all(int fd, const uint8_t* data, size_t len) {
    size_t offset = 0;
    while (offset < len) {
        ssize_t written = send(fd, data + offset, len - offset, MSG_NOSIGNAL);
        if (written > 0) {
            offset += static_cast<size_t>(written);
            continue;
        }
        if (written < 0 && errno == EINTR) {
            continue;
        }
        return false;
    }
    return true;
}

size_t read_name_length(ByteView packet, size_t pos) {
    size_t cursor = pos;
    size_t jumps = 0;
    while (true) {
        if (cursor >= packet.size()) {
            throw DnsError("DNS name truncated");
        }

        uint8_t len = packet[cursor];
        if (len == 0) {
            return (cursor - pos) + 1;
        }
        if ((len & 0xC0) == 0xC0) {
            if (cursor + 1 >= packet.size()) {
                throw DnsError("DNS compression pointer truncated");
            }
            return (cursor - pos) + 2;
        }
        if ((len & 0xC0) != 0) {
            throw DnsError("DNS name label invalid");
        }

        cursor += 1 + len;
        if (++jumps > 128) {
            throw DnsError("DNS name parse exceeded limit");
        }
    }
}

std::optional<std::string> parse_ecs_option(ByteView option_data) {
    if (option_data.size() < 4) {
        return std::nullopt;
    }

    uint16_t family = static_cast<uint16_t>((option_data[0] << 8) | option_data[1]);
    uint8_t source_prefix = option_data[2];
    size_t address_bytes = option_data.size() - 4;

    if (family == 1) {
        if (source_prefix > 32 || address_bytes > 4) {
            return std::nullopt;
        }
        std::array<uint8_t, 4> addr {};
        for (size_t i = 0; i < address_bytes; ++i) {
            addr[i] = option_data[4 + i];
        }
        char text[INET_ADDRSTRLEN] {};
        if (!inet_ntop(AF_INET, addr.data(), text, sizeof(text))) {
            return std::nullopt;
        }
        return std::string(text) + "/" + std::to_string(source_prefix);
    }

    if (family == 2) {
        if (source_prefix > 128 || address_bytes > 16) {
            return std::nullopt;
        }
        std::array<uint8_t, 16> addr {};
        for (size_t i = 0; i < address_bytes; ++i) {
            addr[i] = option_data[4 + i];
        }
        char text[INET6_ADDRSTRLEN] {};
        if (!inet_ntop(AF_INET6, addr.data(), text, sizeof(text))) {
            return std::nullopt;
        }
        return std::string(text) + "/" + std::to_string(source_prefix);
    }

    return std::nullopt;
}

std::optional<std::string> parse_edns_client_subnet(ByteView packet,
                                                    size_t pos,
                                                    uint16_t arcount) {
    for (uint16_t i = 0; i < arcount; ++i) {
        size_t name_len = read_name_length(packet, pos);
        pos += name_len;
        if (pos + 10 > packet.size()) {
            throw DnsError("DNS additional record truncated");
        }

        uint16_t type = static_cast<uint16_t>((packet[pos] << 8) | packet[pos + 1]);
        pos += 2;
        pos += 2; // class
        pos += 4; // ttl
        uint16_t rdlen = static_cast<uint16_t>((packet[pos] << 8) | packet[pos + 1]);
        pos += 2;
        if (pos + rdlen > packet.size()) {
            throw DnsError("DNS additional record payload truncated");
        }

        if (type == DNS_TYPE_OPT) {
            size_t opt_pos = pos;
            size_t opt_end = pos + rdlen;
            while (opt_pos + 4 <= opt_end) {
                uint16_t option_code =
                    static_cast<uint16_t>((packet[opt_pos] << 8) | packet[opt_pos + 1]);
                uint16_t option_len =
                    static_cast<uint16_t>((packet[opt_pos + 2] << 8) | packet[opt_pos + 3]);
                opt_pos += 4;
                if (opt_pos + option_len > opt_end) {
                    break;
                }

                if (option_code == DNS_EDNS_OPTION_ECS) {
                    auto ecs = parse_ecs_option(packet.subspan(opt_pos, option_len));
                    if (ecs.has_value()) {
                        return ecs;
                    }
                }
                opt_pos += option_len;
            }
        }

        pos += rdlen;
    }

    return std::nullopt;
}

std::string socket_addr_to_string(const sockaddr* addr) {
    if (!addr) {
        return "unknown";
    }

    char buf[INET6_ADDRSTRLEN] {};
    if (addr->sa_family == AF_INET) {
        const auto* in = reinterpret_cast<const sockaddr_in*>(addr);
        if (inet_ntop(AF_INET, &in->sin_addr, buf, sizeof(buf))) {
            return buf;
        }
    } else if (addr->sa_family == AF_INET6) {
        const auto* in6 = reinterpret_cast<const sockaddr_in6*>(addr);
        if (inet_ntop(AF_INET6, &in6->sin6_addr, buf, sizeof(buf))) {
            return buf;
        }
    }
    return "unknown";
}

std::string peer_addr_to_string(int fd) {
    sockaddr_storage peer {};
    socklen_t peer_len = sizeof(peer);
    if (getpeername(fd, reinterpret_cast<sockaddr*>(&peer), &peer_len) < 0) {
        return "unknown";
    }
    return socket_addr_to_string(reinterpret_cast<const sockaddr*>(&peer));
}

} // namespace

DnsProbeListenAddress parse_dns_probe_listen_address(const std::string& listen) {
    auto parsed = parse_dns_address_str(listen);
    if (!is_valid_ipv4(parsed.ip)) {
        throw DnsError("DNS test server listen address must be IPv4: " + listen);
    }
    return {parsed.ip, parsed.port};
}

DnsProbeServerSettings parse_dns_probe_server_settings(const std::string& listen,
                                                       const std::string* answer_ipv4) {
    auto parsed = parse_dns_probe_listen_address(listen);
    std::string answer = answer_ipv4 ? *answer_ipv4 : parsed.ip;
    if (!is_valid_ipv4(answer)) {
        throw DnsError("DNS test server answer_ipv4 must be a valid IPv4 address: " + answer);
    }

    return {
        listen,
        parsed.ip,
        parsed.port,
        answer,
    };
}

DnsProbeQuestion parse_dns_probe_query(ByteView packet) {
    if (packet.size() < DNS_HEADER_SIZE) {
        throw DnsError("DNS probe packet too short");
    }

    auto read_u16 = [&](size_t offset) -> uint16_t {
        return static_cast<uint16_t>((packet[offset] << 8) | packet[offset + 1]);
    };

    uint16_t qdcount = read_u16(4);
    if (qdcount != 1) {
        throw DnsError("DNS probe expects exactly one question");
    }
    uint16_t ancount = read_u16(6);
    uint16_t nscount = read_u16(8);
    uint16_t arcount = read_u16(10);

    std::vector<std::string> labels;
    std::vector<uint8_t> question_wire;
    size_t pos = DNS_HEADER_SIZE;
    while (true) {
        if (pos >= packet.size()) {
            throw DnsError("DNS probe qname truncated");
        }

        uint8_t len = packet[pos++];
        question_wire.push_back(len);
        if (len == 0) {
            break;
        }
        if ((len & 0xC0) != 0) {
            throw DnsError("DNS probe does not accept compressed questions");
        }
        if (pos + len > packet.size()) {
            throw DnsError("DNS probe qname label truncated");
        }

        labels.emplace_back(reinterpret_cast<const char*>(packet.data() + pos), len);
        question_wire.insert(question_wire.end(), packet.begin() + static_cast<std::ptrdiff_t>(pos),
                             packet.begin() + static_cast<std::ptrdiff_t>(pos + len));
        pos += len;
    }

    if (pos + 4 > packet.size()) {
        throw DnsError("DNS probe question footer truncated");
    }

    question_wire.insert(question_wire.end(), packet.begin() + static_cast<std::ptrdiff_t>(pos),
                         packet.begin() + static_cast<std::ptrdiff_t>(pos + 4));

    DnsProbeQuestion question;
    question.id = read_u16(0);
    question.flags = read_u16(2);
    question.name = normalize_dns_name(labels);
    pos += 4;

    for (uint16_t i = 0; i < ancount + nscount; ++i) {
        size_t name_len = read_name_length(packet, pos);
        pos += name_len;
        if (pos + 10 > packet.size()) {
            throw DnsError("DNS record truncated");
        }
        uint16_t rdlen = static_cast<uint16_t>((packet[pos + 8] << 8) | packet[pos + 9]);
        pos += 10;
        if (pos + rdlen > packet.size()) {
            throw DnsError("DNS record payload truncated");
        }
        pos += rdlen;
    }

    question.ecs = parse_edns_client_subnet(packet, pos, arcount);
    question.question_wire = std::move(question_wire);
    return question;
}

std::vector<uint8_t> build_dns_probe_response(const DnsProbeQuestion& question,
                                              const std::string& answer_ipv4) {
    if (!is_valid_ipv4(answer_ipv4)) {
        throw DnsError("Invalid DNS probe answer IPv4: " + answer_ipv4);
    }

    std::vector<uint8_t> out;
    out.reserve(DNS_HEADER_SIZE + question.question_wire.size() + 16);

    append_u16(out, question.id);
    append_u16(out, static_cast<uint16_t>(DNS_FLAG_QR | (question.flags & DNS_FLAG_RD)));
    append_u16(out, 1); // qdcount
    append_u16(out, 1); // ancount
    append_u16(out, 0); // nscount
    append_u16(out, 0); // arcount

    out.insert(out.end(), question.question_wire.begin(), question.question_wire.end());

    append_u16(out, 0xC00C); // name pointer to question qname
    append_u16(out, DNS_TYPE_A);
    append_u16(out, DNS_CLASS_IN);
    append_u32(out, 0); // ttl
    append_u16(out, 4); // rdlength

    in_addr addr {};
    inet_pton(AF_INET, answer_ipv4.c_str(), &addr);
    const auto* bytes = reinterpret_cast<const uint8_t*>(&addr.s_addr);
    out.insert(out.end(), bytes, bytes + 4);
    return out;
}

DnsProbeServer::DnsProbeServer(const DnsProbeServerSettings& settings,
                               QueryCallback on_query)
    : settings_(settings)
    , on_query_(std::move(on_query))
{
    udp_fd_ = create_bound_socket(SOCK_DGRAM, settings_);
    try {
        tcp_fd_ = create_bound_socket(SOCK_STREAM, settings_);
    } catch (...) {
        close_fd(udp_fd_);
        throw;
    }
}

DnsProbeServer::~DnsProbeServer() {
    for (auto& [fd, state] : tcp_clients_) {
        (void)state;
        close(fd);
    }
    tcp_clients_.clear();
    close_fd(udp_fd_);
    close_fd(tcp_fd_);
}

std::vector<int> DnsProbeServer::all_fds() const {
    std::vector<int> fds;
    if (udp_fd_ >= 0) fds.push_back(udp_fd_);
    if (tcp_fd_ >= 0) fds.push_back(tcp_fd_);
    for (const auto& [fd, _] : tcp_clients_) {
        (void)_;
        fds.push_back(fd);
    }
    return fds;
}

std::vector<int> DnsProbeServer::accept_tcp_clients() {
    std::vector<int> accepted;
    while (true) {
        int client_fd = accept4(tcp_fd_, nullptr, nullptr, SOCK_CLOEXEC | SOCK_NONBLOCK);
        if (client_fd >= 0) {
            if (tcp_clients_.size() >= kMaxTcpClients) {
                close(client_fd);
                break;
            }
            tcp_clients_.emplace(client_fd, TcpClientState{});
            accepted.push_back(client_fd);
            continue;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        }
        if (errno == EINTR) {
            continue;
        }

        Logger::instance().warn("DNS test server accept failed: {}", strerror(errno));
        break;
    }
    return accepted;
}

bool DnsProbeServer::handle_udp_packet(const uint8_t* data, size_t len,
                                       const sockaddr* addr, socklen_t addrlen) {
    DnsProbeQuestion question = parse_dns_probe_query(ByteView(data, len));
    publish_query(question, socket_addr_to_string(addr));

    auto response = build_dns_probe_response(question, settings_.answer_ipv4);
    ssize_t sent = sendto(udp_fd_, response.data(), response.size(), 0, addr, addrlen);
    if (sent < 0) {
        Logger::instance().warn("DNS test server UDP send failed: {}", strerror(errno));
        return false;
    }
    return true;
}

bool DnsProbeServer::handle_udp_readable() {
    while (true) {
        sockaddr_storage peer {};
        socklen_t peer_len = sizeof(peer);
        uint8_t buf[1500];
        ssize_t n = recvfrom(udp_fd_, buf, sizeof(buf), 0,
                             reinterpret_cast<sockaddr*>(&peer), &peer_len);
        if (n > 0) {
            try {
                handle_udp_packet(buf, static_cast<size_t>(n),
                                  reinterpret_cast<sockaddr*>(&peer), peer_len);
            } catch (const std::exception& e) {
                Logger::instance().warn("DNS test server dropped malformed UDP query: {}", e.what());
            }
            continue;
        }

        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return true;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n < 0) {
            Logger::instance().warn("DNS test server UDP recv failed: {}", strerror(errno));
        }
        return false;
    }
}

bool DnsProbeServer::handle_tcp_packet(int fd, ByteView packet) {
    DnsProbeQuestion question = parse_dns_probe_query(packet);
    publish_query(question, peer_addr_to_string(fd));

    auto response = build_dns_probe_response(question, settings_.answer_ipv4);
    std::vector<uint8_t> framed;
    framed.reserve(response.size() + 2);
    append_u16(framed, static_cast<uint16_t>(response.size()));
    framed.insert(framed.end(), response.begin(), response.end());

    if (!write_all(fd, framed.data(), framed.size())) {
        Logger::instance().warn("DNS test server TCP send failed: {}", strerror(errno));
    }
    return false;
}

bool DnsProbeServer::handle_tcp_client_readable(int fd) {
    auto it = tcp_clients_.find(fd);
    if (it == tcp_clients_.end()) {
        return false;
    }

    auto& state = it->second;
    while (true) {
        uint8_t buf[1024];
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n > 0) {
            state.buffer.insert(state.buffer.end(), buf, buf + n);
            if (state.buffer.size() > kMaxTcpBufferSize) {
                Logger::instance().warn("DNS test server TCP buffer exceeded limit, closing connection");
                return false;
            }
            if (!state.have_size && state.buffer.size() >= 2) {
                state.expected_size = static_cast<uint16_t>((state.buffer[0] << 8) | state.buffer[1]);
                state.have_size = true;
            }
            if (state.have_size && state.buffer.size() >= static_cast<size_t>(state.expected_size) + 2) {
                try {
                    return handle_tcp_packet(fd, ByteView(
                        state.buffer.data() + 2, state.expected_size));
                } catch (const std::exception& e) {
                    Logger::instance().warn("DNS test server dropped malformed TCP query: {}", e.what());
                    return false;
                }
            }
            continue;
        }

        if (n == 0) {
            return false;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return true;
        }
        if (errno == EINTR) {
            continue;
        }

        Logger::instance().warn("DNS test server TCP recv failed: {}", strerror(errno));
        return false;
    }
}

void DnsProbeServer::remove_tcp_client(int fd) {
    tcp_clients_.erase(fd);
}

void DnsProbeServer::publish_query(const DnsProbeQuestion& question,
                                   const std::string& source_ip) const {
    if (question.ecs.has_value()) {
        Logger::instance().info("DNS test query: {} source={} ecs={}",
                                question.name, source_ip, *question.ecs);
    } else {
        Logger::instance().info("DNS test query: {} source={}",
                                question.name, source_ip);
    }
    if (on_query_) {
        on_query_(DnsProbeEvent{
            question.name,
            source_ip,
            question.ecs,
        });
    }
}

void DnsProbeServer::close_fd(int& fd) {
    if (fd >= 0) {
        close(fd);
        fd = -1;
    }
}

} // namespace keen_pbr3
