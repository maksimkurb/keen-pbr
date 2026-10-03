#pragma once

#include "control_protocol.hpp"

#include <string>

namespace keen_pbr3::ipc {

class ControlTimeoutError final : public ControlProtocolError {
public:
    using ControlProtocolError::ControlProtocolError;
};

// Perform one bounded request/response exchange with the running daemon. The
// first timeout covers connecting, sending the request, and receiving the
// daemon's HELO acknowledgement. The second is the total deadline for the
// framed response after that acknowledgement.
nlohmann::json request_control(const std::string& socket_path,
                               const nlohmann::json& request,
                               int connect_timeout_ms = 5000,
                               int total_read_timeout_ms = 60000);

} // namespace keen_pbr3::ipc
