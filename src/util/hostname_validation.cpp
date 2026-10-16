#include "hostname_validation.hpp"

#include <cstdint>

namespace keen_pbr3 {

namespace {

bool is_valid_hostname_char(uint8_t c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           c == '.' || c == '-' || c == '_' || c == '*';
}

}  // namespace

bool is_valid_dns_name(std::string_view name) {
    if (name.empty() || name.size() > 253) {
        return false;
    }

    for (uint8_t c : name) {
        if (!is_valid_hostname_char(c)) {
            return false;
        }
    }

    return true;
}

}  // namespace keen_pbr3
