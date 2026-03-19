#pragma once

#include <iostream>
#include <string_view>

#include "../util/format_compat.hpp"

namespace keen_pbr3 {

enum class LogLevel { error, warn, info, verbose, debug };

LogLevel parse_log_level(std::string_view s);

class Logger {
public:
    static Logger& instance();

    void set_level(LogLevel level) { level_ = level; }
    LogLevel level() const { return level_; }
    bool is_enabled(LogLevel level) const { return level <= level_; }

    void error(std::string_view msg);
    void warn(std::string_view msg);
    void info(std::string_view msg);
    void verbose(std::string_view msg);
    void debug(std::string_view msg);

    template<typename... Args>
    void error(format_string<Args...> fmt, Args&&... args) {
        if (is_enabled(LogLevel::error))
            error(std::string_view(keen_pbr3::format(fmt, std::forward<Args>(args)...)));
    }

    template<typename... Args>
    void warn(format_string<Args...> fmt, Args&&... args) {
        if (is_enabled(LogLevel::warn))
            warn(std::string_view(keen_pbr3::format(fmt, std::forward<Args>(args)...)));
    }

    template<typename... Args>
    void info(format_string<Args...> fmt, Args&&... args) {
        if (is_enabled(LogLevel::info))
            info(std::string_view(keen_pbr3::format(fmt, std::forward<Args>(args)...)));
    }

    template<typename... Args>
    void verbose(format_string<Args...> fmt, Args&&... args) {
        if (is_enabled(LogLevel::verbose))
            verbose(std::string_view(keen_pbr3::format(fmt, std::forward<Args>(args)...)));
    }

    template<typename... Args>
    void debug(format_string<Args...> fmt, Args&&... args) {
        if (is_enabled(LogLevel::debug))
            debug(std::string_view(keen_pbr3::format(fmt, std::forward<Args>(args)...)));
    }

private:
    Logger() = default;
    LogLevel level_{LogLevel::info};
};

} // namespace keen_pbr3
