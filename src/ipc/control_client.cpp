#include "control_client.hpp"

#include <arpa/inet.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <ostream>

namespace keen_pbr3::ipc {
namespace {

constexpr std::size_t kResolverStreamChunkBytes = static_cast<std::size_t>(16) * 1024U;
using Deadline = std::chrono::steady_clock::time_point;

Deadline deadline_after(int timeout_ms) {
    return std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
}

int remaining_ms(Deadline deadline) {
    const auto remaining = std::chrono::ceil<std::chrono::milliseconds>(
        deadline - std::chrono::steady_clock::now());
    return static_cast<int>(std::max<std::int64_t>(0, remaining.count()));
}

void wait_for(int fd, short events, Deadline deadline) {
    pollfd descriptor{fd, events, 0};
    int result = 0;
    do {
        const int timeout_ms = remaining_ms(deadline);
        if (timeout_ms == 0) throw ControlProtocolError("control socket timeout");
        result = poll(&descriptor, 1, timeout_ms);
    } while (result < 0 && errno == EINTR);
    if (result == 0) throw ControlProtocolError("control socket timeout");
    if (result < 0) throw ControlProtocolError("control socket poll failed: " + std::string(strerror(errno)));
    if ((descriptor.revents & events) == 0 &&
        (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
        throw ControlProtocolError("control socket closed");
    }
}

void write_all(int fd, const std::string& data, Deadline deadline) {
    std::size_t written = 0;
    while (written < data.size()) {
        wait_for(fd, POLLOUT, deadline);
        const ssize_t count = send(fd, data.data() + written, data.size() - written, MSG_NOSIGNAL);
        if (count <= 0) throw ControlProtocolError("control socket write failed: " + std::string(strerror(errno)));
        written += static_cast<std::size_t>(count);
    }
}

std::string read_exact(int fd, std::size_t size, Deadline deadline) {
    std::string result(size, '\0');
    std::size_t received = 0;
    while (received < size) {
        wait_for(fd, POLLIN, deadline);
        const ssize_t count = recv(fd, result.data() + received, size - received, 0);
        if (count <= 0) throw ControlProtocolError("control socket read failed");
        received += static_cast<std::size_t>(count);
    }
    return result;
}

int connect_control_socket(const std::string& socket_path, Deadline deadline) {
    if (socket_path.size() >= sizeof(sockaddr_un::sun_path)) {
        throw ControlProtocolError("control socket path is too long");
    }
    const int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) throw ControlProtocolError("control socket create failed: " + std::string(strerror(errno)));
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::strncpy(address.sun_path, socket_path.c_str(), sizeof(address.sun_path) - 1U);
    const int original_flags = fcntl(fd, F_GETFL, 0);
    if (original_flags < 0 || fcntl(fd, F_SETFL, original_flags | O_NONBLOCK) != 0) {
        const std::string error = strerror(errno);
        close(fd);
        throw ControlProtocolError("control socket setup failed: " + error);
    }
    const int connect_result =
        connect(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
    const bool connecting = connect_result != 0 && errno == EINPROGRESS;
    if (connect_result != 0 && !connecting) {
        const std::string error = strerror(errno);
        close(fd);
        throw ControlProtocolError("control socket unavailable: " + error);
    }
    if (connecting) {
        wait_for(fd, POLLOUT, deadline);
        int socket_error = 0;
        socklen_t error_size = sizeof(socket_error);
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &socket_error, &error_size) != 0 ||
            socket_error != 0) {
            const int error_number = socket_error != 0 ? socket_error : errno;
            close(fd);
            throw ControlProtocolError("control socket unavailable: " +
                                       std::string(strerror(error_number)));
        }
    }
    if (fcntl(fd, F_SETFL, original_flags) != 0) {
        const std::string error = strerror(errno);
        close(fd);
        throw ControlProtocolError("control socket setup failed: " + error);
    }
    return fd;
}

nlohmann::json read_response_envelope(int fd, Deadline deadline) {
    const std::string header = read_exact(fd, sizeof(std::uint32_t), deadline);
    std::uint32_t length = 0;
    std::memcpy(&length, header.data(), sizeof(length));
    const std::size_t payload_size = ntohl(length);
    if (payload_size > kMaxControlMessageBytes) throw ControlProtocolError("control response exceeds maximum size");
    return decode_message(header + read_exact(fd, payload_size, deadline));
}

void read_start_marker(int fd, Deadline deadline) {
    const auto marker = read_exact(fd, kControlHelloMarker.size(), deadline);
    if (marker != kControlHelloMarker) {
        throw ControlProtocolError("invalid control socket start acknowledgement");
    }
}

} // namespace

nlohmann::json request_control(const std::string& socket_path,
                               const nlohmann::json& request,
                               int connect_timeout_ms,
                               int total_read_timeout_ms) {
    validate_request_envelope(request);
    const auto connect_deadline = deadline_after(connect_timeout_ms);
    const int fd = connect_control_socket(socket_path, connect_deadline);
    const auto close_fd = [&]() { close(fd); };
    try {
        write_all(fd, encode_message(request), connect_deadline);
        read_start_marker(fd, connect_deadline);
        return read_response_envelope(fd, deadline_after(total_read_timeout_ms));
    } catch (...) {
        close_fd();
        throw;
    }
}

void stream_control(const std::string& socket_path,
                    const nlohmann::json& request,
                    std::ostream& output,
                    int connect_timeout_ms,
                    int idle_timeout_ms) {
    validate_request_envelope(request);
    bool active_bytes_streamed = false;
    int fd = -1;
    try {
        const auto connect_deadline = deadline_after(connect_timeout_ms);
        fd = connect_control_socket(socket_path, connect_deadline);
        write_all(fd, encode_message(request), connect_deadline);
        read_start_marker(fd, connect_deadline);
        const auto response = read_response_envelope(fd, deadline_after(idle_timeout_ms));
        if (!response.value("ok", false)) {
            const auto code = response.value("error", nlohmann::json::object()).value("code", "daemon_error");
            throw ControlStreamError(code, false);
        }
        if (!response.value("stream", false)) {
            throw ControlStreamError("protocol_error", false);
        }

        while (true) {
            const std::string length_frame = read_exact(
                fd, sizeof(std::uint32_t), deadline_after(idle_timeout_ms));
            std::uint32_t length = 0;
            std::memcpy(&length, length_frame.data(), sizeof(length));
            const std::size_t chunk_size = ntohl(length);
            if (chunk_size == 0) break;
            if (chunk_size > kResolverStreamChunkBytes) {
                throw ControlStreamError("protocol_error", active_bytes_streamed);
            }
            const std::string chunk = read_exact(
                fd, chunk_size, deadline_after(idle_timeout_ms));
            output.write(chunk.data(), static_cast<std::streamsize>(chunk.size()));
            output.flush();
            if (!output) throw ControlStreamError("stdout_error", active_bytes_streamed);
            active_bytes_streamed = true;
        }
    } catch (const ControlStreamError&) {
        if (fd >= 0) close(fd);
        throw;
    } catch (const ControlProtocolError& error) {
        if (fd >= 0) close(fd);
        throw ControlStreamError(error.what(), active_bytes_streamed);
    } catch (...) {
        if (fd >= 0) close(fd);
        throw;
    }
    if (fd >= 0) close(fd);
}

} // namespace keen_pbr3::ipc
