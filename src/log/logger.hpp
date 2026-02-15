#pragma once

#include <format>
#include <iostream>
#include <string_view>

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
    void error(std::format_string<Args...> fmt, Args&&... args) {
        if (is_enabled(LogLevel::error))
            error(std::string_view(std::format(fmt, std::forward<Args>(args)...)));
    }

    template<typename... Args>
    void warn(std::format_string<Args...> fmt, Args&&... args) {
        if (is_enabled(LogLevel::warn))
            warn(std::string_view(std::format(fmt, std::forward<Args>(args)...)));
    }

    template<typename... Args>
    void info(std::format_string<Args...> fmt, Args&&... args) {
        if (is_enabled(LogLevel::info))
            info(std::string_view(std::format(fmt, std::forward<Args>(args)...)));
    }

    template<typename... Args>
    void verbose(std::format_string<Args...> fmt, Args&&... args) {
        if (is_enabled(LogLevel::verbose))
            verbose(std::string_view(std::format(fmt, std::forward<Args>(args)...)));
    }

    template<typename... Args>
    void debug(std::format_string<Args...> fmt, Args&&... args) {
        if (is_enabled(LogLevel::debug))
            debug(std::string_view(std::format(fmt, std::forward<Args>(args)...)));
    }

private:
    Logger() = default;
    LogLevel level_{LogLevel::info};
};

} // namespace keen_pbr3
