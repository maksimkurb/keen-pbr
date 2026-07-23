#pragma once

#include <istream>
#include <optional>
#include <string>
#include <string_view>

#include "../lists/list_entry_visitor.hpp"

namespace keen_pbr3 {

class ListParser {
public:
    struct ParseContext {
        bool log_invalid_entries{true};
        std::size_t invalid_entry_count{0};
    };

    // Stream-parse from an istream, dispatching each entry to the visitor.
    static void stream_parse(std::istream& input,
                             ListEntryVisitor& visitor,
                             std::string_view source_name = {});

    // Classify a single trimmed entry and dispatch to the visitor.
    // Returns true if the entry was recognized and dispatched.
    static bool classify_entry(std::string_view entry, ListEntryVisitor& visitor);

    static void parse_line(std::string_view line,
                           ListEntryVisitor& visitor,
                           std::string_view source_name,
                           std::size_t line_number,
                           ParseContext* context = nullptr);

    // Validate and normalize a DNS-compatible domain. Leading "*." and one
    // trailing root dot are removed from the returned value.
    static std::optional<std::string> normalize_domain(std::string_view domain);

private:
    static bool is_ipv4(std::string_view s);
    static bool is_ipv6(std::string_view s);
    static bool is_cidr_v4(std::string_view s);
    static bool is_cidr_v6(std::string_view s);
};

} // namespace keen_pbr3
