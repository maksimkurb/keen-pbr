#pragma once

#include <string_view>

namespace keen_pbr3 {

// Checks if a string is a valid DNS/hostname.
// Accepts ASCII letters (a-z, A-Z), digits (0-9), hyphens (-), underscores (_),
// asterisks (*), and dots (.). Empty strings or strings longer than 253 characters
// are rejected.
bool is_valid_dns_name(std::string_view name);

}  // namespace keen_pbr3
