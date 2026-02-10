#pragma once

#include <istream>
#include <string_view>

#include "../lists/list_entry_visitor.hpp"

namespace keen_pbr3 {

class ListParser {
public:
    // Stream-parse from an istream, dispatching each entry to the visitor.
    static void stream_parse(std::istream& input, ListEntryVisitor& visitor);

    // Classify a single trimmed entry and dispatch to the visitor.
    // Returns true if the entry was recognized and dispatched.
    static bool classify_entry(std::string_view entry, ListEntryVisitor& visitor);

private:
    static bool is_ipv4(std::string_view s);
    static bool is_ipv6(std::string_view s);
    static bool is_cidr_v4(std::string_view s);
    static bool is_cidr_v6(std::string_view s);
    static bool is_domain(std::string_view s);
};

} // namespace keen_pbr3
