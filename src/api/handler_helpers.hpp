#pragma once

#ifdef WITH_API

#include <iomanip>
#include <sstream>

#include <nlohmann/json.hpp>

#include "../config/config.hpp"
#include "../health/circuit_breaker.hpp"
#include "../routing/firewall_state.hpp"

namespace keen_pbr3 {

// Helper to extract the tag from any Outbound variant
inline std::string outbound_tag(const Outbound& ob) {
    return std::visit([](const auto& o) -> std::string { return o.tag; }, ob);
}

// Helper to get the type name from an Outbound variant
inline std::string outbound_type(const Outbound& ob) {
    return std::visit([](const auto& o) -> std::string {
        using T = std::decay_t<decltype(o)>;
        if constexpr (std::is_same_v<T, InterfaceOutbound>) return "interface";
        else if constexpr (std::is_same_v<T, TableOutbound>) return "table";
        else if constexpr (std::is_same_v<T, BlackholeOutbound>) return "blackhole";
        else if constexpr (std::is_same_v<T, IgnoreOutbound>) return "ignore";
        else if constexpr (std::is_same_v<T, UrltestOutbound>) return "urltest";
        else return "unknown";
    }, ob);
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
    std::string tag = outbound_tag(ob);
    j["tag"] = tag;
    j["type"] = outbound_type(ob);

    std::visit([&j](const auto& o) {
        using T = std::decay_t<decltype(o)>;
        if constexpr (std::is_same_v<T, InterfaceOutbound>) {
            j["interface"] = o.interface;
            if (o.gateway) j["gateway"] = *o.gateway;
        } else if constexpr (std::is_same_v<T, TableOutbound>) {
            j["table_id"] = o.table_id;
        } else if constexpr (std::is_same_v<T, UrltestOutbound>) {
            j["url"] = o.url;
            j["interval_ms"] = o.interval_ms;
        }
    }, ob);

    auto mark_it = marks.find(tag);
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
