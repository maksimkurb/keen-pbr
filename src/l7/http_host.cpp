#include "http_host.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace keen_pbr3::l7 {

namespace {

bool is_http_method_start(ByteView payload) {
    const char* methods[] = {"GET ", "POST ", "HEAD ", "PUT ", "DELETE ", "OPTIONS ", "PATCH ", "CONNECT "};
    for (const auto* method : methods) {
        std::size_t len = std::strlen(method);
        if (payload.size() >= len) {
            bool match = true;
            for (std::size_t i = 0; i < len; ++i) {
                if (payload.data()[i] != static_cast<unsigned char>(method[i])) {
                    match = false;
                    break;
                }
            }
            if (match) {
                return true;
            }
        }
    }
    return false;
}

bool is_ip_literal(const std::string& str) {
    // Check for IPv6: [...]
    if (!str.empty() && str[0] == '[') {
        return true;
    }
    // Check for IPv4: all digits and dots
    bool all_digits_dots = true;
    for (char c : str) {
        if (c != '.' && (c < '0' || c > '9')) {
            all_digits_dots = false;
            break;
        }
    }
    return all_digits_dots;
}

}  // namespace

ParseStatus http_host(ByteView payload, std::string& host_out) {
    const uint8_t* data = payload.data();
    std::size_t size = payload.size();
    const std::size_t max_search = std::min(size, static_cast<std::size_t>(4096));

    // Check for valid HTTP method
    if (!is_http_method_start(ByteView(data, std::min(max_search, size)))) {
        return ParseStatus::NotMatched;
    }

    // Find the end of the request line (\r\n)
    std::size_t request_line_end = max_search;
    for (std::size_t i = 0; i + 1 < max_search; ++i) {
        if (data[i] == '\r' && data[i + 1] == '\n') {
            request_line_end = i;
            break;
        }
    }

    // Find the end of headers (\r\n\r\n)
    bool headers_complete = false;
    std::size_t headers_end = max_search;
    for (std::size_t i = 0; i + 3 < size && i + 3 <= max_search; ++i) {
        if (data[i] == '\r' && data[i + 1] == '\n' && data[i + 2] == '\r' && data[i + 3] == '\n') {
            headers_end = i;
            headers_complete = true;
            break;
        }
    }

    // Search for Host header (never in the body)
    const std::size_t search_end = headers_complete ? headers_end + 2 : max_search;
    for (std::size_t i = request_line_end + 2; i < search_end; ++i) {
        // Check if this could be the start of a header line
        if ((i == request_line_end + 2) || (i > 0 && i + 1 < max_search && data[i - 2] == '\r' && data[i - 1] == '\n')) {
            // Check for "Host:" (case-insensitive)
            if (i + 5 <= max_search) {
                std::string prefix;
                for (int j = 0; j < 4; ++j) {
                    if (i + j < max_search) {
                        prefix += static_cast<char>(std::tolower(static_cast<unsigned char>(data[i + j])));
                    }
                }

                if (prefix == "host") {
                    // Next char must be ':'
                    if (i + 4 < max_search && data[i + 4] == ':') {
                        std::size_t value_start = i + 5;

                        // Trim leading spaces/tabs
                        while (value_start < max_search && (data[value_start] == ' ' || data[value_start] == '\t')) {
                            ++value_start;
                        }

                        // Find end of header value (\r\n)
                        std::size_t value_end = value_start;
                        bool found_crlf = false;
                        for (std::size_t j = value_start; j + 1 < max_search; ++j) {
                            if (data[j] == '\r' && data[j + 1] == '\n') {
                                value_end = j;
                                found_crlf = true;
                                break;
                            }
                        }

                        // If we didn't find CRLF within max_search
                        if (!found_crlf) {
                            if (!headers_complete) {
                                // Headers not complete, need more data
                                return ParseStatus::NeedMore;
                            }
                            // If headers complete but no CRLF for this header, skip it
                            continue;
                        }

                        // Trim trailing spaces/tabs
                        while (value_end > value_start && (data[value_end - 1] == ' ' || data[value_end - 1] == '\t')) {
                            --value_end;
                        }

                        if (value_end <= value_start) {
                            return ParseStatus::NotMatched;  // Empty Host header
                        }

                        std::string host_with_port(reinterpret_cast<const char*>(data) + value_start,
                                                   value_end - value_start);

                        // Check for and remove port
                        std::string host = host_with_port;

                        // Handle IPv6 [::1]:port format
                        if (!host.empty() && host[0] == '[') {
                            // IPv6 literal
                            return ParseStatus::NotMatched;
                        }

                        // Handle port removal for IPv4/domain
                        std::size_t colon_pos = host.rfind(':');
                        if (colon_pos != std::string::npos) {
                            host = host.substr(0, colon_pos);
                        }

                        // Check if it's an IP literal
                        if (is_ip_literal(host)) {
                            return ParseStatus::NotMatched;
                        }

                        // Lowercase and strip trailing dot
                        for (auto& c : host) {
                            if (c >= 'A' && c <= 'Z') {
                                c = static_cast<char>(c - 'A' + 'a');
                            }
                        }

                        if (!host.empty() && host.back() == '.') {
                            host.pop_back();
                        }

                        if (host.empty()) {
                            return ParseStatus::NotMatched;
                        }

                        host_out = host;
                        return ParseStatus::Found;
                    }
                }
            }
        }
    }

    // Host header not found within our search
    if (!headers_complete) {
        // We haven't seen complete headers yet
        return ParseStatus::NeedMore;
    }

    return ParseStatus::NotMatched;  // Host header not found
}

}  // namespace keen_pbr3::l7
