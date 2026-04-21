#include "port_spec_util.hpp"
#include "../util/format_compat.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <stdexcept>
#include <vector>

namespace keen_pbr3 {

namespace {

bool parse_port_range_token(const std::string& token,
                            char range_sep,
                            int& lo,
                            int& hi) {
    const auto dash = token.find(range_sep);
    if (dash == std::string::npos) {
        if (!parse_port_value(token, lo)) {
            return false;
        }
        hi = lo;
        return true;
    }

    if (token.find(range_sep, dash + 1) != std::string::npos) {
        return false;
    }

    if (!parse_port_value(token.substr(0, dash), lo) ||
        !parse_port_value(token.substr(dash + 1), hi)) {
        return false;
    }

    return lo <= hi;
}

std::vector<PortRange> normalize_ranges(std::vector<PortRange> ranges) {
    std::sort(ranges.begin(), ranges.end(), [](const PortRange& lhs, const PortRange& rhs) {
        if (lhs.from != rhs.from) return lhs.from < rhs.from;
        return lhs.to < rhs.to;
    });
    return ranges;
}

std::string ranges_to_string(const std::vector<PortRange>& ranges, char range_sep) {
    std::string out;
    for (size_t i = 0; i < ranges.size(); ++i) {
        if (i != 0) out += ",";
        if (ranges[i].from == ranges[i].to) {
            out += std::to_string(ranges[i].from);
        } else {
            out += keen_pbr3::format("{}{}{}", ranges[i].from, range_sep, ranges[i].to);
        }
    }
    return out;
}

} // namespace

PortSpec::PortSpec(std::string_view spec)
    : ranges(parse_port_spec(spec).ranges) {}

PortSpec& PortSpec::operator=(std::string_view spec) {
    ranges = parse_port_spec(spec).ranges;
    return *this;
}

PortSpecKind PortSpec::kind() const {
    if (ranges.empty()) return PortSpecKind::Empty;
    if (ranges.size() > 1) return PortSpecKind::List;
    return ranges.front().from == ranges.front().to
        ? PortSpecKind::Single
        : PortSpecKind::Range;
}

std::string PortSpec::to_config_string() const {
    return ranges_to_string(ranges, '-');
}

std::string PortSpec::to_iptables_string() const {
    return ranges_to_string(ranges, ':');
}

bool parse_port_value(const std::string& token, int& out) {
    if (token.empty()) {
        return false;
    }

    long long value = 0;
    for (char c : token) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            return false;
        }

        value = value * 10 + (c - '0');
        if (value > std::numeric_limits<int>::max()) {
            return false;
        }
    }

    if (value < 1 || value > 65535) {
        return false;
    }

    out = static_cast<int>(value);
    return true;
}

bool parse_port_range(const std::string& token, int& lo, int& hi) {
    return parse_port_range_token(token, '-', lo, hi);
}

PortSpec parse_port_spec(std::string_view spec_view) {
    PortSpec spec;
    if (spec_view.empty()) {
        return spec;
    }

    const std::string input(spec_view);
    for (const auto& token : split_port_spec_tokens(input)) {
        int lo = 0;
        int hi = 0;
        if (!parse_port_range_token(token, '-', lo, hi) &&
            !parse_port_range_token(token, ':', lo, hi)) {
            throw std::invalid_argument(
                keen_pbr3::format("Invalid port token '{}' in port spec '{}'", token, input));
        }
        spec.ranges.push_back(PortRange{
            .from = static_cast<uint16_t>(lo),
            .to = static_cast<uint16_t>(hi),
        });
    }

    spec.ranges = normalize_ranges(std::move(spec.ranges));
    return spec;
}

PortSpecKind classify_port_spec(const std::string& spec) {
    return parse_port_spec(spec).kind();
}

PortSpecKind classify_port_spec(const PortSpec& spec) {
    return spec.kind();
}

std::vector<std::string> split_port_spec_tokens(const std::string& spec) {
    std::vector<std::string> tokens;
    std::string token;
    for (char c : spec) {
        if (c == ',') {
            if (token.empty()) {
                throw std::invalid_argument(keen_pbr3::format("Invalid empty token in port spec '{}'", spec));
            }
            tokens.push_back(token);
            token.clear();
        } else {
            token += c;
        }
    }
    if (spec.empty()) {
        return tokens;
    }
    if (token.empty()) {
        throw std::invalid_argument(keen_pbr3::format("Invalid empty token in port spec '{}'", spec));
    }
    tokens.push_back(token);
    return tokens;
}

std::string normalize_port_spec_for_iptables(const std::string& spec) {
    return parse_port_spec(spec).to_iptables_string();
}

} // namespace keen_pbr3
