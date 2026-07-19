#include "control_protocol.hpp"

#include <arpa/inet.h>

#include <cstring>

namespace keen_pbr3::ipc {

std::string encode_message(const nlohmann::json& envelope) {
    const std::string payload = envelope.dump();
    if (payload.size() > kMaxControlMessageBytes) {
        throw ControlProtocolError("control message exceeds maximum size");
    }

    const std::uint32_t encoded_length = htonl(static_cast<std::uint32_t>(payload.size()));
    std::string frame(sizeof(encoded_length), '\0');
    std::memcpy(frame.data(), &encoded_length, sizeof(encoded_length));
    frame += payload;
    return frame;
}

nlohmann::json decode_message(const std::string& frame) {
    if (frame.size() < sizeof(std::uint32_t)) {
        throw ControlProtocolError("truncated control message header");
    }

    std::uint32_t encoded_length = 0;
    std::memcpy(&encoded_length, frame.data(), sizeof(encoded_length));
    const std::size_t payload_size = ntohl(encoded_length);
    if (payload_size > kMaxControlMessageBytes) {
        throw ControlProtocolError("control message exceeds maximum size");
    }
    if (frame.size() != sizeof(encoded_length) + payload_size) {
        throw ControlProtocolError("control message length does not match frame");
    }

    try {
        return nlohmann::json::parse(frame.data() + sizeof(encoded_length),
                                     frame.data() + frame.size());
    } catch (const nlohmann::json::exception& error) {
        throw ControlProtocolError("invalid control JSON: " + std::string(error.what()));
    }
}

void validate_request_envelope(const nlohmann::json& request) {
    if (!request.is_object()) {
        throw ControlProtocolError("control request must be an object");
    }
    if (!request.contains("protocol_version") ||
        request.at("protocol_version") != kControlProtocolVersion) {
        throw ControlProtocolError("unsupported control protocol version");
    }
    if (!request.contains("request_id") || !request.at("request_id").is_string() ||
        request.at("request_id").get_ref<const std::string&>().empty()) {
        throw ControlProtocolError("control request requires a non-empty request_id");
    }
    if (!request.contains("operation") || !request.at("operation").is_string() ||
        request.at("operation").get_ref<const std::string&>().empty()) {
        throw ControlProtocolError("control request requires an operation");
    }
}

nlohmann::json make_error_response(const nlohmann::json& request,
                                   std::string code,
                                   std::string message) {
    nlohmann::json response{{"protocol_version", kControlProtocolVersion},
                            {"ok", false},
                            {"error", {{"code", std::move(code)},
                                       {"message", std::move(message)}}}};
    if (request.is_object() && request.contains("request_id") && request.at("request_id").is_string()) {
        response["request_id"] = request.at("request_id");
    }
    return response;
}

} // namespace keen_pbr3::ipc
