#include <doctest/doctest.h>
#include <sstream>

#include "../src/config/list_parser.hpp"

using namespace keen_pbr3;

TEST_CASE("ListParser::is_valid_entry accepts known entry kinds") {
    CHECK(ListParser::is_valid_entry("10.0.0.1"));
    CHECK(ListParser::is_valid_entry("10.0.0.0/8"));
    CHECK(ListParser::is_valid_entry("example.com"));
    CHECK(ListParser::is_valid_entry("*.example.com"));
}

TEST_CASE("ListParser::is_valid_entry rejects unknown entries") {
    CHECK_FALSE(ListParser::is_valid_entry("not a valid entry"));
    CHECK_FALSE(ListParser::is_valid_entry(""));
}

TEST_CASE("ListParser::count_invalid_lines ignores comments and blank lines") {
    std::istringstream input(R"(

# comment
10.0.0.1
garbage-line

example.com
)");
    CHECK(ListParser::count_invalid_lines(input) == 1);
}
