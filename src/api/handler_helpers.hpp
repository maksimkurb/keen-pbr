#pragma once

#ifdef WITH_API

#include <iomanip>
#include <sstream>

#include <nlohmann/json.hpp>

#include "../config/config.hpp"
#include "../health/circuit_breaker.hpp"

namespace keen_pbr3 {

// Helper to extract the tag from an Outbound
inline std::string outbound_tag(const Outbound& ob) {
    return ob.tag;
}

// Helper to get the type name string from an Outbound
inline std::string outbound_type(const Outbound& ob) {
    nlohmann::json j = ob.type;
    return j.get<std::string>();
}

// Format uint32_t as hex string (e.g., "0x00010000")
inline std::string format_hex(uint32_t val) {
    std::ostringstream ss;
    ss << "0x" << std::hex << std::setfill('0') << std::setw(8) << val;
    return ss.str();
}

// Build JSON for a single outbound with fwmark info
inline nlohmann::json outbound_to_json(const Outbound& ob, const OutboundMarkMap& marks) {
    nlohmann::json j;
    j["tag"] = ob.tag;
    j["type"] = outbound_type(ob);

    if (ob.type == OutboundType::INTERFACE) {
        if (ob.interface) j["interface"] = *ob.interface;
        if (ob.gateway)   j["gateway"]   = *ob.gateway;
    } else if (ob.type == OutboundType::TABLE) {
        if (ob.table) j["table"] = *ob.table;
    } else if (ob.type == OutboundType::URLTEST) {
        if (ob.url)         j["url"]         = *ob.url;
        if (ob.interval_ms) j["interval_ms"] = *ob.interval_ms;
    }

    auto mark_it = marks.find(ob.tag);
    if (mark_it != marks.end()) {
        j["fwmark"] = format_hex(mark_it->second);
    }

    return j;
}

// Convert CircuitState to string
inline std::string circuit_state_string(CircuitState state) {
    switch (state) {
        case CircuitState::closed: return "closed";
        case CircuitState::open: return "open";
        case CircuitState::half_open: return "half_open";
        default: return "unknown";
    }
}

} // namespace keen_pbr3

#endif // WITH_API
