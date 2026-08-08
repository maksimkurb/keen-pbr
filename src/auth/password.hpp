#pragma once

#include <string>
#include <string_view>

namespace keen_pbr3::auth {

std::string generate_password_hash(std::string_view password);
bool verify_password(std::string_view password, std::string_view encoded);
bool valid_password_hash(std::string_view encoded);
std::string random_token();
std::string blake2b_hex(std::string_view value);
bool constant_time_equal(std::string_view lhs, std::string_view rhs);

} // namespace keen_pbr3::auth
