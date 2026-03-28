#pragma once

#include <string>
#include <vector>

namespace keen_pbr3 {

enum class PortSpecKind {
    Single,
    Range,
    List
};

bool parse_port_value(const std::string& token, int& out);
bool parse_port_range(const std::string& token, int& lo, int& hi);
PortSpecKind classify_port_spec(const std::string& spec);
std::vector<std::string> split_port_spec_tokens(const std::string& spec);
std::string normalize_port_spec_for_iptables(const std::string& spec);

} // namespace keen_pbr3
