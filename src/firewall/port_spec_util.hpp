#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace keen_pbr3 {

struct PortRange {
    uint16_t from{0};
    uint16_t to{0};

    bool operator==(const PortRange& other) const {
        return from == other.from && to == other.to;
    }
};

enum class PortSpecKind {
    Empty,
    Single,
    Range,
    List
};

struct PortSpec {
    std::vector<PortRange> ranges;

    PortSpec() = default;
    explicit PortSpec(std::string_view spec);

    PortSpec& operator=(std::string_view spec);

    bool empty() const { return ranges.empty(); }
    PortSpecKind kind() const;
    std::string to_config_string() const;
    std::string to_iptables_string() const;

    bool operator==(const PortSpec& other) const {
        return ranges == other.ranges;
    }
};

bool parse_port_value(const std::string& token, int& out);
bool parse_port_range(const std::string& token, int& lo, int& hi);
PortSpec parse_port_spec(std::string_view spec);
PortSpecKind classify_port_spec(const std::string& spec);
PortSpecKind classify_port_spec(const PortSpec& spec);
std::vector<std::string> split_port_spec_tokens(const std::string& spec);
std::string normalize_port_spec_for_iptables(const std::string& spec);

} // namespace keen_pbr3
