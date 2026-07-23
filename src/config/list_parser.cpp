#include "list_parser.hpp"
#include "../log/logger.hpp"

#include <arpa/inet.h>

#include <algorithm>
#include <charconv>

namespace keen_pbr3 {

static std::string_view trim(std::string_view sv) {
    while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t' || sv.front() == '\r')) {
        sv.remove_prefix(1);
    }
    while (!sv.empty() && (sv.back() == ' ' || sv.back() == '\t' || sv.back() == '\r')) {
        sv.remove_suffix(1);
    }
    return sv;
}

static std::string format_entry_for_log(std::string_view entry) {
    constexpr std::size_t kMaxLoggedBytes = 256;
    std::string result;
    result.reserve(std::min(entry.size(), kMaxLoggedBytes) + 2);
    result.push_back('\'');
    const std::size_t length = std::min(entry.size(), kMaxLoggedBytes);
    for (std::size_t index = 0; index < length; ++index) {
        const unsigned char ch = static_cast<unsigned char>(entry[index]);
        switch (ch) {
            case '\\': result += "\\\\"; break;
            case '\'': result += "\\'"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:
                if (ch < 0x20 || ch == 0x7f) {
                    constexpr char hex[] = "0123456789abcdef";
                    result += "\\x";
                    result.push_back(hex[ch >> 4]);
                    result.push_back(hex[ch & 0x0f]);
                } else {
                    result.push_back(static_cast<char>(ch));
                }
        }
    }
    if (entry.size() > length) result += "…";
    result.push_back('\'');
    return result;
}

bool ListParser::is_ipv4(std::string_view s) {
    int octets = 0;
    size_t pos = 0;
    while (pos <= s.size() && octets < 4) {
        auto dot = s.find('.', pos);
        if (dot == std::string_view::npos) dot = s.size();
        auto part = s.substr(pos, dot - pos);
        if (part.empty() || part.size() > 3) return false;
        int val = 0;
        auto [ptr, ec] = std::from_chars(part.data(), part.data() + part.size(), val);
        if (ec != std::errc{} || ptr != part.data() + part.size()) return false;
        if (val < 0 || val > 255) return false;
        ++octets;
        pos = dot + 1;
    }
    return octets == 4 && pos == s.size() + 1;
}

bool ListParser::is_ipv6(std::string_view s) {
    if (s.empty()) return false;
    // Reject CIDR suffixes and scoped zone IDs. This parser classifies plain IPs;
    // CIDR is handled separately and zone IDs are not supported downstream.
    if (s.find('/') != std::string_view::npos) return false;
    if (s.find('%') != std::string_view::npos) return false;

    std::string ip(s);
    in6_addr parsed{};
    return inet_pton(AF_INET6, ip.c_str(), &parsed) == 1;
}

bool ListParser::is_cidr_v4(std::string_view s) {
    auto slash = s.find('/');
    if (slash == std::string_view::npos) return false;
    auto ip_part = s.substr(0, slash);
    auto prefix_part = s.substr(slash + 1);
    if (!is_ipv4(ip_part)) return false;
    if (prefix_part.empty() || prefix_part.size() > 2) return false;
    int prefix = 0;
    auto [ptr, ec] = std::from_chars(prefix_part.data(), prefix_part.data() + prefix_part.size(), prefix);
    if (ec != std::errc{} || ptr != prefix_part.data() + prefix_part.size()) return false;
    return prefix >= 0 && prefix <= 32;
}

bool ListParser::is_cidr_v6(std::string_view s) {
    auto slash = s.find('/');
    if (slash == std::string_view::npos) return false;
    auto ip_part = s.substr(0, slash);
    auto prefix_part = s.substr(slash + 1);
    if (!is_ipv6(ip_part)) return false;
    if (prefix_part.empty() || prefix_part.size() > 3) return false;
    int prefix = 0;
    auto [ptr, ec] = std::from_chars(prefix_part.data(), prefix_part.data() + prefix_part.size(), prefix);
    if (ec != std::errc{} || ptr != prefix_part.data() + prefix_part.size()) return false;
    return prefix >= 0 && prefix <= 128;
}

std::optional<std::string> ListParser::normalize_domain(std::string_view s) {
    if (s.empty()) return std::nullopt;
    if (s.size() >= 2 && s.substr(0, 2) == "*.") s.remove_prefix(2);
    if (!s.empty() && s.back() == '.') s.remove_suffix(1);
    if (s.empty() || s.back() == '.' || s.size() > 253) return std::nullopt;

    bool has_alpha = false;
    std::size_t label_start = 0;
    while (label_start < s.size()) {
        const std::size_t dot = s.find('.', label_start);
        const std::size_t label_end = dot == std::string_view::npos ? s.size() : dot;
        const std::size_t label_size = label_end - label_start;
        if (label_size == 0 || label_size > 63) return std::nullopt;
        if (s[label_start] == '-' || s[label_end - 1] == '-') return std::nullopt;

        for (std::size_t i = label_start; i < label_end; ++i) {
            const char c = s[i];
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
                has_alpha = true;
            } else if ((c >= '0' && c <= '9') || c == '-' || c == '_') {
                // DNS-compatible service labels may contain underscores.
            } else {
                return std::nullopt;
            }
        }
        if (dot == std::string_view::npos) break;
        label_start = dot + 1;
    }

    if (!has_alpha) return std::nullopt;
    return std::string(s);
}

bool ListParser::classify_entry(std::string_view entry, ListEntryVisitor& visitor) {
    if (is_cidr_v4(entry) || is_cidr_v6(entry)) {
        visitor.on_entry(EntryType::Cidr, entry);
        return true;
    }
    if (is_ipv4(entry) || is_ipv6(entry)) {
        visitor.on_entry(EntryType::Ip, entry);
        return true;
    }
    if (auto domain = normalize_domain(entry)) {
        visitor.on_entry(EntryType::Domain, *domain);
        return true;
    }
    return false;
}

void ListParser::stream_parse(std::istream& input,
                              ListEntryVisitor& visitor,
                              std::string_view source_name) {
    std::string line;
    std::size_t line_number = 0;
    ParseContext context;
    while (std::getline(input, line)) {
        ++line_number;
        parse_line(line, visitor, source_name, line_number, &context);
    }
}

void ListParser::parse_line(std::string_view line,
                            ListEntryVisitor& visitor,
                            std::string_view source_name,
                            std::size_t line_number,
                            ParseContext* context) {
    const auto value = trim(line);
    if (value.empty() || value.front() == '#') return;
    if (!classify_entry(value, visitor)
        && (!context || context->log_invalid_entries)) {
        constexpr std::size_t kMaxDetailedInvalidEntries = 5;
        const std::size_t count = context ? ++context->invalid_entry_count : 1;
        if (count > kMaxDetailedInvalidEntries + 1) return;
        if (count == kMaxDetailedInvalidEntries + 1) {
            Logger::instance().warn(
                "Too many invalid list entries in {}; further entries will be skipped without warnings",
                source_name.empty() ? std::string("list source") : std::string(source_name));
            return;
        }
        Logger::instance().warn("Skipping invalid list entry {} in {} at line {}",
                                format_entry_for_log(value),
                                source_name.empty() ? std::string("list source")
                                                    : std::string(source_name),
                                line_number);
    }
}

} // namespace keen_pbr3
