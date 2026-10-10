#include "dns_txt_probe.hpp"

#include "dns_wire.hpp"
#include "../util/byte_view.hpp"

#include <arpa/inet.h>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <netinet/in.h>
#include <poll.h>
#include <random>
#include <sys/socket.h>
#include <unistd.h>

namespace keen_pbr3 {

namespace {

constexpr std::uint16_t kTypeTxt = 16;
constexpr std::uint16_t kClassIn = 1;
constexpr std::size_t kMaxUdpResponse = 4096;

DnsTxtProbeResult failed(std::string error) {
    DnsTxtProbeResult result;
    result.status = DnsTxtProbeStatus::QueryFailed;
    result.error = std::move(error);
    return result;
}

using keen_pbr3::load_be16;

std::string lowercase(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
    }
    return out;
}

std::string strip_trailing_dot(std::string_view name) {
    if (!name.empty() && name.back() == '.') name.remove_suffix(1);
    return lowercase(name);
}

// Skips an encoded (possibly compressed) name; returns false when malformed.
bool skip_name(const std::uint8_t* msg, std::size_t size, std::size_t& pos) {
    while (true) {
        if (pos >= size) return false;
        const std::uint8_t len = msg[pos];
        if ((len & 0xC0) == 0xC0) {
            if (pos + 2 > size) return false;
            pos += 2;
            return true;
        }
        if ((len & 0xC0) != 0) return false;
        if (len == 0) {
            ++pos;
            return true;
        }
        pos += 1u + len;
    }
}

} // namespace

std::vector<std::uint8_t> build_dns_txt_query(std::uint16_t transaction_id,
                                              std::string_view name) {
    std::vector<std::uint8_t> packet;
    packet.reserve(18 + name.size());
    packet.push_back(static_cast<std::uint8_t>(transaction_id >> 8));
    packet.push_back(static_cast<std::uint8_t>(transaction_id & 0xFF));
    packet.push_back(0x01);  // RD
    packet.push_back(0x00);
    packet.push_back(0x00); packet.push_back(0x01);  // QDCOUNT
    for (int i = 0; i < 6; ++i) packet.push_back(0x00);  // AN/NS/AR

    const std::string trimmed = strip_trailing_dot(name);
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
    packet.push_back(static_cast<std::uint8_t>(kTypeTxt >> 8));
    packet.push_back(static_cast<std::uint8_t>(kTypeTxt & 0xFF));
    packet.push_back(static_cast<std::uint8_t>(kClassIn >> 8));
    packet.push_back(static_cast<std::uint8_t>(kClassIn & 0xFF));
    return packet;
}

DnsTxtProbeResult parse_dns_txt_response(const std::uint8_t* data,
                                         std::size_t size,
                                         std::uint16_t transaction_id,
                                         std::string_view name) {
    dns_wire::ParsedResponse response;
    if (!dns_wire::parse_response(ByteView(data, size), response)) {
        return failed("malformed DNS response");
    }
    if (response.id != transaction_id) {
        DnsTxtProbeResult result;
        result.status = DnsTxtProbeStatus::IdMismatch;
        result.error = "DNS response id mismatch";
        return result;
    }
    if (response.qtype != kTypeTxt || response.qclass != kClassIn ||
        response.qname != strip_trailing_dot(name)) {
        return failed("DNS response question mismatch");
    }
    if (response.truncated) {
        return failed("DNS response truncated");
    }
    if (response.rcode == 3) {  // NXDOMAIN
        DnsTxtProbeResult result;
        result.status = DnsTxtProbeStatus::Missing;
        return result;
    }
    if (response.rcode != 0) {
        return failed("DNS rcode " + std::to_string(response.rcode));
    }

    // parse_response() validated the structure; walk the answers for TXT.
    const std::uint16_t ancount = load_be16(data + 6);
    std::size_t pos = response.question_end;
    for (std::uint16_t i = 0; i < ancount; ++i) {
        if (!skip_name(data, size, pos) || pos + 10 > size) {
            return failed("malformed DNS response");
        }
        const std::uint16_t type = load_be16(data + pos);
        const std::uint16_t cls = load_be16(data + pos + 2);
        const std::size_t rdlen = load_be16(data + pos + 8);
        pos += 10;
        if (pos + rdlen > size) {
            return failed("malformed DNS response");
        }
        if (type == kTypeTxt && cls == kClassIn) {
            DnsTxtProbeResult result;
            result.status = DnsTxtProbeStatus::Ok;
            std::size_t offset = 0;
            while (offset < rdlen) {
                const std::size_t chunk = data[pos + offset++];
                if (offset + chunk > rdlen) {
                    return failed("malformed DNS TXT record");
                }
                result.txt.append(reinterpret_cast<const char*>(data + pos + offset), chunk);
                offset += chunk;
            }
            return result;
        }
        pos += rdlen;
    }
    DnsTxtProbeResult result;
    result.status = DnsTxtProbeStatus::Missing;
    return result;
}

DnsTxtProbeResult probe_dns_txt(const std::string& address,
                                std::uint16_t port,
                                std::string_view name,
                                std::chrono::milliseconds timeout) {
    sockaddr_storage target{};
    socklen_t target_len = 0;
    {
        sockaddr_in v4{};
        sockaddr_in6 v6{};
        if (inet_pton(AF_INET, address.c_str(), &v4.sin_addr) == 1) {
            v4.sin_family = AF_INET;
            v4.sin_port = htons(port);
            std::memcpy(&target, &v4, sizeof(v4));
            target_len = sizeof(v4);
        } else if (inet_pton(AF_INET6, address.c_str(), &v6.sin6_addr) == 1) {
            v6.sin6_family = AF_INET6;
            v6.sin6_port = htons(port);
            std::memcpy(&target, &v6, sizeof(v6));
            target_len = sizeof(v6);
        } else {
            return failed("invalid probe address " + address);
        }
    }

    const int fd = ::socket(target.ss_family, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        return failed(std::string("socket: ") + std::strerror(errno));
    }
    struct FdCloser {
        int fd;
        ~FdCloser() { ::close(fd); }
    } closer{fd};

    if (::connect(fd, reinterpret_cast<const sockaddr*>(&target), target_len) != 0) {
        return failed(std::string("connect: ") + std::strerror(errno));
    }

    thread_local std::mt19937 rng{std::random_device{}()};
    const auto transaction_id = static_cast<std::uint16_t>(rng() & 0xFFFF);
    const std::vector<std::uint8_t> query = build_dns_txt_query(transaction_id, name);
    if (::send(fd, query.data(), query.size(), 0) < 0) {
        return failed(std::string("send: ") + std::strerror(errno));
    }

    const auto deadline = std::chrono::steady_clock::now() + timeout;
    std::vector<std::uint8_t> buffer(kMaxUdpResponse);
    while (true) {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        if (remaining.count() <= 0) {
            return failed("timed out");
        }
        pollfd pfd{fd, POLLIN, 0};
        const int ready = ::poll(&pfd, 1, static_cast<int>(remaining.count()));
        if (ready < 0) {
            if (errno == EINTR) continue;
            return failed(std::string("poll: ") + std::strerror(errno));
        }
        if (ready == 0) {
            return failed("timed out");
        }
        const ssize_t got = ::recv(fd, buffer.data(), buffer.size(), 0);
        if (got < 0) {
            if (errno == EINTR) continue;
            // ECONNREFUSED: ICMP port unreachable from a loopback target.
            return failed(std::string("recv: ") + std::strerror(errno));
        }
        DnsTxtProbeResult result = parse_dns_txt_response(
            buffer.data(), static_cast<std::size_t>(got), transaction_id, name);
        // A stray/spoofed datagram with a foreign id is ignored, not fatal.
        if (result.status == DnsTxtProbeStatus::IdMismatch) {
            continue;
        }
        return result;
    }
}

} // namespace keen_pbr3
