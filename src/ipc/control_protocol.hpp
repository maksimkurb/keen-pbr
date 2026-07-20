#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

namespace keen_pbr3::ipc {

constexpr std::uint32_t kControlProtocolVersion = 1;
constexpr std::size_t kMaxControlMessageBytes = std::size_t{1024} * 1024U;

class ControlProtocolError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

// Encode a single JSON envelope as a network-byte-order 32-bit length followed
// by UTF-8 JSON. The framing deliberately has no delimiter ambiguity.
std::string encode_message(const nlohmann::json& envelope);

// Decode a fully buffered message. This is also used by socket code after it
// has read the exact frame length, keeping validation independent of transport.
nlohmann::json decode_message(const std::string& frame);

// Validate the common request envelope before dispatching an operation.
void validate_request_envelope(const nlohmann::json& request);

nlohmann::json make_error_response(const nlohmann::json& request,
                                   std::string code,
                                   std::string message);

} // namespace keen_pbr3::ipc
