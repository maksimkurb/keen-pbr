#pragma once

#include <chrono>
#include <functional>
#include <iostream>
#include <mutex>
#include <string>
#include <string_view>

#include "../util/format_compat.hpp"

namespace keen_pbr3 {

enum class LogLevel { error, warn, info, verbose, debug };

LogLevel parse_log_level(std::string_view s);

class Logger {
public:
    using Sink = std::function<void(const std::string&)>;

    static Logger& instance();

    void set_level(LogLevel level) { level_ = level; }
    LogLevel level() const { return level_; }
    bool is_enabled(LogLevel level) const { return level <= level_; }

    void set_sink(Sink sink);
    void clear_sink();

    void error(std::string_view msg);
    void warn(std::string_view msg);
    void info(std::string_view msg);
    void verbose(std::string_view msg);
    void debug(std::string_view msg);
    void trace(std::string_view event, std::string_view details = {});

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

    template<typename... Args>
    void trace(std::string_view event, format_string<Args...> fmt, Args&&... args) {
        if (is_enabled(LogLevel::debug))
            trace(event, std::string_view(keen_pbr3::format(fmt, std::forward<Args>(args)...)));
    }

private:
    Logger() = default;

    void emit_line(const std::string& line);

    LogLevel level_{LogLevel::info};
    std::mutex sink_mutex_;
    Sink sink_;
    std::chrono::steady_clock::time_point started_at_{std::chrono::steady_clock::now()};
};

} // namespace keen_pbr3
