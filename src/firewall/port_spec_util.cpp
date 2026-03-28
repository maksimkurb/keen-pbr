#include "port_spec_util.hpp"
#include "../util/format_compat.hpp"

#include <cctype>
#include <limits>
#include <stdexcept>
#include <vector>

namespace keen_pbr3 {

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
    const auto dash = token.find('-');
    if (dash == std::string::npos) {
        if (!parse_port_value(token, lo)) {
            return false;
        }
        hi = lo;
        return true;
    }

    if (token.find('-', dash + 1) != std::string::npos) {
        return false;
    }

    if (!parse_port_value(token.substr(0, dash), lo) ||
        !parse_port_value(token.substr(dash + 1), hi)) {
        return false;
    }

    return lo <= hi;
}

PortSpecKind classify_port_spec(const std::string& spec) {
    if (spec.find(',') != std::string::npos) {
        return PortSpecKind::List;
    }
    if (spec.find('-') != std::string::npos) {
        return PortSpecKind::Range;
    }
    return PortSpecKind::Single;
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
    if (token.empty()) {
        throw std::invalid_argument(keen_pbr3::format("Invalid empty token in port spec '{}'", spec));
    }
    tokens.push_back(token);
    return tokens;
}

std::string normalize_port_spec_for_iptables(const std::string& spec) {
    std::vector<std::string> tokens = split_port_spec_tokens(spec);
    std::string normalized;
    for (size_t i = 0; i < tokens.size(); ++i) {
        int lo = 0;
        int hi = 0;
        if (!parse_port_range(tokens[i], lo, hi)) {
            throw std::invalid_argument(keen_pbr3::format("Invalid port token '{}' in port spec '{}'", tokens[i], spec));
        }

        if (i != 0) {
            normalized += ",";
        }
        normalized += (lo == hi)
            ? std::to_string(lo)
            : keen_pbr3::format("{}:{}", lo, hi);
    }

    return normalized;
}

} // namespace keen_pbr3
