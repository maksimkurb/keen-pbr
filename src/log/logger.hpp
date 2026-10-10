#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>

#include "../util/format_compat.hpp"

namespace keen_pbr3 {

// Makes a traffic-derived string safe to log: printable ASCII is kept,
// backslash becomes "\\", any other byte "\xNN". Long input is cut at 255
// bytes and marked with "...".
inline std::string log_escape(std::string_view s) {
    static constexpr char kHex[] = "0123456789abcdef";
    constexpr std::size_t kMaxBytes = 255;
    std::string out;
    out.reserve(std::min(s.size(), kMaxBytes) + 3);
    for (std::size_t i = 0; i < s.size() && i < kMaxBytes; ++i) {
        const auto byte = static_cast<unsigned char>(s[i]);
        if (byte == '\\') {
            out += "\\\\";
        } else if (byte >= 0x20 && byte <= 0x7E) {
            out += static_cast<char>(byte);
        } else {
            out += "\\x";
            out += kHex[byte >> 4];
            out += kHex[byte & 0xF];
        }
    }
    if (s.size() > kMaxBytes) out += "...";
    return out;
}

enum class LogLevel { error, warn, info, verbose, debug };
enum class LogTarget { stderr_only, syslog_only, both };

LogLevel parse_log_level(std::string_view s);
LogTarget parse_log_target(std::string_view s);
// Resolve the --log-target option: an empty value selects the default (both),
// regardless of the command being run. Services pass "syslog" explicitly.
LogTarget resolve_log_target(std::string_view cli_value);

class Logger {
public:
    using Sink = std::function<void(const std::string&)>;

    static Logger& instance();

    void set_level(LogLevel level) { level_ = level; }
    LogLevel level() const { return level_; }
    bool is_enabled(LogLevel level) const { return level <= level_; }
    void set_target(LogTarget target);
    LogTarget target() const;

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

    void emit_line(const std::string& line, int syslog_priority);

    LogLevel level_{LogLevel::info};
    mutable std::mutex sink_mutex_;
    LogTarget target_{LogTarget::stderr_only};
    Sink sink_;
    std::chrono::steady_clock::time_point started_at_{std::chrono::steady_clock::now()};
};

} // namespace keen_pbr3
