#include "control_client.hpp"

#include <arpa/inet.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace keen_pbr3::ipc {
namespace {

void wait_for(int fd, short events, int timeout_ms) {
    pollfd descriptor{fd, events, 0};
    const int result = poll(&descriptor, 1, timeout_ms);
    if (result == 0) throw ControlProtocolError("control socket timeout");
    if (result < 0) throw ControlProtocolError("control socket poll failed: " + std::string(strerror(errno)));
    if ((descriptor.revents & events) == 0 &&
        (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
        throw ControlProtocolError("control socket closed");
    }
}

void write_all(int fd, const std::string& data, int timeout_ms) {
    std::size_t written = 0;
    while (written < data.size()) {
        wait_for(fd, POLLOUT, timeout_ms);
        const ssize_t count = send(fd, data.data() + written, data.size() - written, MSG_NOSIGNAL);
        if (count <= 0) throw ControlProtocolError("control socket write failed: " + std::string(strerror(errno)));
        written += static_cast<std::size_t>(count);
    }
}

std::string read_exact(int fd, std::size_t size, int timeout_ms) {
    std::string result(size, '\0');
    std::size_t received = 0;
    while (received < size) {
        wait_for(fd, POLLIN, timeout_ms);
        const ssize_t count = recv(fd, result.data() + received, size - received, 0);
        if (count <= 0) throw ControlProtocolError("control socket read failed");
        received += static_cast<std::size_t>(count);
    }
    return result;
}

} // namespace

nlohmann::json request_control(const std::string& socket_path,
                               const nlohmann::json& request,
                               int timeout_ms) {
    if (socket_path.size() >= sizeof(sockaddr_un::sun_path)) {
        throw ControlProtocolError("control socket path is too long");
    }
    validate_request_envelope(request);
    const int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) throw ControlProtocolError("control socket create failed: " + std::string(strerror(errno)));
    const auto close_fd = [&]() { close(fd); };
    try {
        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        std::strncpy(address.sun_path, socket_path.c_str(), sizeof(address.sun_path) - 1U);
        if (connect(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
            throw ControlProtocolError("control socket unavailable: " + std::string(strerror(errno)));
        }
        write_all(fd, encode_message(request), timeout_ms);
        const std::string header = read_exact(fd, sizeof(std::uint32_t), timeout_ms);
        std::uint32_t length = 0;
        std::memcpy(&length, header.data(), sizeof(length));
        const std::size_t payload_size = ntohl(length);
        if (payload_size > kMaxControlMessageBytes) throw ControlProtocolError("control response exceeds maximum size");
        return decode_message(header + read_exact(fd, payload_size, timeout_ms));
    } catch (...) {
        close_fd();
        throw;
    }
}

} // namespace keen_pbr3::ipc
