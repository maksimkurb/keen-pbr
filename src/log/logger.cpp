#include "logger.hpp"

#include "trace.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>

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

void Logger::emit_line(const std::string& line) {
    std::lock_guard<std::mutex> lock(sink_mutex_);
    if (sink_) {
        sink_(line);
        return;
    }
    std::cerr << line << "\n";
}

void Logger::error(std::string_view msg) {
    if (is_enabled(LogLevel::error))
        emit_line("[E] " + std::string(msg));
}

void Logger::warn(std::string_view msg) {
    if (is_enabled(LogLevel::warn))
        emit_line("[W] " + std::string(msg));
}

void Logger::info(std::string_view msg) {
    if (is_enabled(LogLevel::info))
        emit_line(std::string(msg));
}

void Logger::verbose(std::string_view msg) {
    if (is_enabled(LogLevel::verbose))
        emit_line("[V] " + std::string(msg));
}

void Logger::debug(std::string_view msg) {
    if (is_enabled(LogLevel::debug))
        emit_line("[D] " + std::string(msg));
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
    emit_line(out.str());
}

} // namespace keen_pbr3
