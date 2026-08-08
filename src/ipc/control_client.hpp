#pragma once

#include "control_protocol.hpp"

#include <iosfwd>
#include <string>

namespace keen_pbr3::ipc {

// Perform one bounded request/response exchange with the running daemon. The
// first timeout covers connecting, sending the request, and receiving the
// daemon's HELO acknowledgement. The second is the total deadline for the
// framed response after that acknowledgement.
nlohmann::json request_control(const std::string& socket_path,
                               const nlohmann::json& request,
                               int connect_timeout_ms = 5000,
                               int total_read_timeout_ms = 60000);

class ControlStreamError : public ControlProtocolError {
public:
    ControlStreamError(std::string message, bool active_bytes_streamed)
        : ControlProtocolError(std::move(message))
        , active_bytes_streamed_(active_bytes_streamed) {}

    bool active_bytes_streamed() const noexcept { return active_bytes_streamed_; }

private:
    bool active_bytes_streamed_;
};

// Receive a framed stream-start envelope and copy following raw bytes directly
// to output. The connect timeout also covers the HELO acknowledgement; the
// idle timeout applies while waiting for each subsequent frame or chunk.
void stream_control(const std::string& socket_path,
                    const nlohmann::json& request,
                    std::ostream& output,
                    int connect_timeout_ms = 5000,
                    int idle_timeout_ms = 15000);

} // namespace keen_pbr3::ipc
