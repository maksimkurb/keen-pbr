#include "logger.hpp"

#include <stdexcept>

namespace keen_pbr3 {

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

void Logger::error(std::string_view msg) {
    if (is_enabled(LogLevel::error))
        std::cerr << "[E] " << msg << "\n";
}

void Logger::warn(std::string_view msg) {
    if (is_enabled(LogLevel::warn))
        std::cerr << "[W] " << msg << "\n";
}

void Logger::info(std::string_view msg) {
    if (is_enabled(LogLevel::info))
        std::cerr << msg << "\n";
}

void Logger::verbose(std::string_view msg) {
    if (is_enabled(LogLevel::verbose))
        std::cerr << "[V] " << msg << "\n";
}

void Logger::debug(std::string_view msg) {
    if (is_enabled(LogLevel::debug))
        std::cerr << "[D] " << msg << "\n";
}

} // namespace keen_pbr3
