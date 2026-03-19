#pragma once

#include <string>
#include <utility>

#include <fmt/format.h>

namespace keen_pbr3 {

template<typename... Args>
using format_string = fmt::format_string<Args...>;

template<typename... Args>
std::string format(format_string<Args...> fmt_str, Args&&... args) {
    return fmt::format(fmt_str, std::forward<Args>(args)...);
}

} // namespace keen_pbr3
