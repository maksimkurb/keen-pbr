#pragma once

#include "control_protocol.hpp"

#include <string>

namespace keen_pbr3::ipc {

// Perform one bounded request/response exchange with the running daemon.
nlohmann::json request_control(const std::string& socket_path,
                               const nlohmann::json& request,
                               int timeout_ms = 5000);

} // namespace keen_pbr3::ipc
