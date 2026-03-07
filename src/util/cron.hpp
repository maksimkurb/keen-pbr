#pragma once

#include <chrono>
#include <ctime>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace keen_pbr3 {

namespace detail {

inline int cron_parse_int(const std::string& s, const std::string& ctx) {
    if (s.empty())
        throw std::invalid_argument("Empty number in cron field: " + ctx);
    for (char c : s) {
        if (c < '0' || c > '9')
            throw std::invalid_argument("Non-numeric character in cron field: " + ctx);
    }
    try {
        return std::stoi(s);
    } catch (...) {
        throw std::invalid_argument("Number out of range in cron field: " + ctx);
    }
}

// Expands one cron field token (e.g. "1-5/2", "*/3", "0,15,30,45") into a sorted set.
// lo/hi define the valid range for this field.
inline std::set<int> expand_cron_field(const std::string& token, int lo, int hi) {
    std::set<int> result;

    // Split by comma
    std::vector<std::string> parts;
    {
        std::string cur;
        for (char c : token) {
            if (c == ',') { parts.push_back(cur); cur.clear(); }
            else           { cur += c; }
        }
        parts.push_back(cur);
    }

    for (const auto& part : parts) {
        // Separate optional /step suffix
        int step = 1;
        std::string range_part = part;
        auto slash = part.find('/');
        if (slash != std::string::npos) {
            step = cron_parse_int(part.substr(slash + 1), token);
            if (step < 1)
                throw std::invalid_argument("Step must be >= 1 in cron field: " + token);
            range_part = part.substr(0, slash);
        }

        int start, end;
        if (range_part == "*") {
            start = lo;
            end   = hi;
        } else {
            auto dash = range_part.find('-');
            if (dash != std::string::npos) {
                start = cron_parse_int(range_part.substr(0, dash), token);
                end   = cron_parse_int(range_part.substr(dash + 1), token);
            } else {
                start = cron_parse_int(range_part, token);
                // Plain integer with a step: iterate start..hi
                end = (slash != std::string::npos) ? hi : start;
            }
        }

        if (start < lo || start > hi)
            throw std::invalid_argument("Value " + std::to_string(start) +
                " out of range [" + std::to_string(lo) + "," + std::to_string(hi) +
                "] in cron field: " + token);
        if (end < lo || end > hi)
            throw std::invalid_argument("Value " + std::to_string(end) +
                " out of range [" + std::to_string(lo) + "," + std::to_string(hi) +
                "] in cron field: " + token);
        if (start > end)
            throw std::invalid_argument("Range start > end in cron field: " + token);

        for (int v = start; v <= end; v += step)
            result.insert(v);
    }

    if (result.empty())
        throw std::invalid_argument("Empty expansion for cron field: " + token);
    return result;
}

struct CronFields {
    std::set<int> minute;  // 0-59
    std::set<int> hour;    // 0-23
    std::set<int> mday;    // 1-31
    std::set<int> month;   // 1-12
    std::set<int> wday;    // 0-6 (0=Sunday)
};

inline CronFields parse_cron_fields(const std::string& expr) {
    // Tokenize on whitespace
    std::vector<std::string> tokens;
    std::string cur;
    for (char c : expr) {
        if (c == ' ' || c == '\t') {
            if (!cur.empty()) { tokens.push_back(cur); cur.clear(); }
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) tokens.push_back(cur);

    if (tokens.size() != 5)
        throw std::invalid_argument(
            "Cron expression must have exactly 5 fields, got " +
            std::to_string(tokens.size()) + ": '" + expr + "'");

    CronFields f;
    f.minute = expand_cron_field(tokens[0], 0, 59);
    f.hour   = expand_cron_field(tokens[1], 0, 23);
    f.mday   = expand_cron_field(tokens[2], 1, 31);
    f.month  = expand_cron_field(tokens[3], 1, 12);
    f.wday   = expand_cron_field(tokens[4], 0, 6);
    return f;
}

} // namespace detail

// Throws std::invalid_argument if expression is syntactically invalid.
inline void cron_validate(const std::string& expr) {
    detail::parse_cron_fields(expr);
}

// Returns the next fire time strictly after `from`, rounded up to the next whole minute.
// Throws std::invalid_argument if the expression is invalid or no match found within ~2 years.
inline std::chrono::system_clock::time_point cron_next(
    const std::string& expr,
    std::chrono::system_clock::time_point from = std::chrono::system_clock::now())
{
    const auto fields = detail::parse_cron_fields(expr);

    // Advance past `from` and round up to the next whole minute boundary
    time_t t = std::chrono::system_clock::to_time_t(from + std::chrono::seconds{1});
    t = ((t + 59) / 60) * 60;

    // Iterate minute-by-minute for up to ~2 years
    constexpr int MAX_MINUTES = 366 * 2 * 24 * 60;
    for (int i = 0; i < MAX_MINUTES; ++i, t += 60) {
        std::tm tm{};
        localtime_r(&t, &tm);

        if (fields.month.count(tm.tm_mon + 1) &&
            fields.mday.count(tm.tm_mday)      &&
            fields.wday.count(tm.tm_wday)      &&
            fields.hour.count(tm.tm_hour)      &&
            fields.minute.count(tm.tm_min))
        {
            return std::chrono::system_clock::from_time_t(t);
        }
    }

    throw std::invalid_argument(
        "cron_next: no matching time found within 2 years for: '" + expr + "'");
}

} // namespace keen_pbr3
