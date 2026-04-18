#include "logger.hpp"

#include "trace.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>
#if defined(__unix__) || defined(__APPLE__)
#include <syslog.h>
#endif

namespace keen_pbr3 {

namespace {

std::string format_wall_clock_now() {
    const auto now = std::chrono::system_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % std::chrono::seconds(1);
    const std::time_t time = std::chrono::system_clock::to_time_t(now);

    std::tm tm {};
#if defined(_WIN32)
    localtime_s(&tm, &time);
#else
    localtime_r(&time, &tm);
#endif

    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S")
        << '.'
        << std::setw(3) << std::setfill('0') << ms.count();
    return out.str();
}

std::string current_thread_id_string() {
    std::ostringstream out;
    out << std::this_thread::get_id();
    return out.str();
}

#if defined(__unix__) || defined(__APPLE__)
void emit_syslog_line(const std::string& line, int priority) {
    static std::once_flag openlog_once;
    std::call_once(openlog_once, []() {
        openlog("keen-pbr", LOG_PID, LOG_DAEMON);
    });
    syslog(priority, "%s", line.c_str());
}
#else
void emit_syslog_line(const std::string&, int) {}
#endif

} // namespace

LogLevel parse_log_level(std::string_view s) {
    if (s == "error") return LogLevel::error;
    if (s == "warn") return LogLevel::warn;
    if (s == "info") return LogLevel::info;
    if (s == "verbose") return LogLevel::verbose;
    if (s == "debug") return LogLevel::debug;
    throw std::runtime_error(
        keen_pbr3::format("Unknown log level '{}'. Valid: error, warn, info, verbose, debug", s));
}

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

void Logger::set_sink(Sink sink) {
    std::lock_guard<std::mutex> lock(sink_mutex_);
    sink_ = std::move(sink);
}

void Logger::clear_sink() {
    std::lock_guard<std::mutex> lock(sink_mutex_);
    sink_ = nullptr;
}

void Logger::emit_line(const std::string& line, int syslog_priority) {
    std::lock_guard<std::mutex> lock(sink_mutex_);
    std::cerr << line << "\n";
    emit_syslog_line(line, syslog_priority);
    if (sink_) {
        sink_(line);
    }
}

void Logger::error(std::string_view msg) {
    if (is_enabled(LogLevel::error))
        emit_line("[E] " + std::string(msg),
#if defined(__unix__) || defined(__APPLE__)
                  LOG_ERR
#else
                  0
#endif
        );
}

void Logger::warn(std::string_view msg) {
    if (is_enabled(LogLevel::warn))
        emit_line("[W] " + std::string(msg),
#if defined(__unix__) || defined(__APPLE__)
                  LOG_WARNING
#else
                  0
#endif
        );
}

void Logger::info(std::string_view msg) {
    if (is_enabled(LogLevel::info))
        emit_line(std::string(msg),
#if defined(__unix__) || defined(__APPLE__)
                  LOG_INFO
#else
                  0
#endif
        );
}

void Logger::verbose(std::string_view msg) {
    if (is_enabled(LogLevel::verbose))
        emit_line("[V] " + std::string(msg),
#if defined(__unix__) || defined(__APPLE__)
                  LOG_INFO
#else
                  0
#endif
        );
}

void Logger::debug(std::string_view msg) {
    if (is_enabled(LogLevel::debug))
        emit_line("[D] " + std::string(msg),
#if defined(__unix__) || defined(__APPLE__)
                  LOG_DEBUG
#else
                  0
#endif
        );
}

void Logger::trace(std::string_view event, std::string_view details) {
    if (!is_enabled(LogLevel::debug)) {
        return;
    }

    const auto mono_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started_at_).count();
    std::ostringstream out;
    out << "[T] "
        << format_wall_clock_now()
        << " mono_ms=" << mono_ms
        << " tid=" << current_thread_id_string()
        << " trace=" << current_trace_id()
        << " event=" << event;
    if (!details.empty()) {
        out << " " << details;
    }
    emit_line(out.str(),
#if defined(__unix__) || defined(__APPLE__)
              LOG_DEBUG
#else
              0
#endif
    );
}

} // namespace keen_pbr3
