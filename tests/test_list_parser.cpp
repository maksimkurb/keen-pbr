#include <doctest/doctest.h>

#include "../src/config/list_parser.hpp"
#include "../src/log/logger.hpp"

#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace keen_pbr3 {
namespace {

class RecordingVisitor : public ListEntryVisitor {
public:
    void on_entry(EntryType type, std::string_view entry) override {
        entries.emplace_back(type, std::string(entry));
    }
    std::vector<std::pair<EntryType, std::string>> entries;
};

} // namespace

TEST_CASE("ListParser normalizes wildcard and root-dot domains") {
    RecordingVisitor visitor;
    CHECK(ListParser::classify_entry("*.google.com", visitor));
    CHECK(ListParser::classify_entry("_dns._udp.example.com.", visitor));
    REQUIRE(visitor.entries.size() == 2);
    CHECK(visitor.entries[0].second == "google.com");
    CHECK(visitor.entries[1].second == "_dns._udp.example.com");
}

TEST_CASE("ListParser applies identical domain rules to streamed sources") {
    std::istringstream input(
        "*.google.com\n"
        "valid_example.test.\n"
        "bad/domain.test\n"
        "bad label.test\n"
        "-bad.example\n");
    RecordingVisitor visitor;
    ListParser::stream_parse(input, visitor, "test-list");
    REQUIRE(visitor.entries.size() == 2);
    CHECK(visitor.entries[0].second == "google.com");
    CHECK(visitor.entries[1].second == "valid_example.test");
}

TEST_CASE("ListParser skips blank and comment entries") {
    std::istringstream input(
        "\n"
        "   \t\r\n"
        "  # comment\n"
        "example.com\n");
    RecordingVisitor visitor;
    ListParser::stream_parse(input, visitor, "test-list");
    REQUIRE(visitor.entries.size() == 1);
    CHECK(visitor.entries[0].second == "example.com");
}

TEST_CASE("ListParser limits malformed-entry warnings per parse") {
    auto& logger = Logger::instance();
    const LogLevel previous_level = logger.level();
    logger.set_level(LogLevel::warn);
    std::string log;
    logger.set_sink([&log](const std::string& line) {
        log += line;
        log.push_back('\n');
    });

    RecordingVisitor visitor;
    ListParser::ParseContext context;
    for (std::size_t line = 1; line <= 7; ++line) {
        ListParser::parse_line("bad/domain" + std::to_string(line), visitor,
                               "limited-test-list", line, &context);
    }

    logger.clear_sink();
    logger.set_level(previous_level);
    CHECK(log.find("Skipping invalid list entry 'bad/domain1' in limited-test-list") !=
          std::string::npos);
    CHECK(log.find("Skipping invalid list entry 'bad/domain5' in limited-test-list") !=
          std::string::npos);
    CHECK(log.find("Skipping invalid list entry 'bad/domain6'") == std::string::npos);
    CHECK(log.find("Too many invalid list entries in limited-test-list") != std::string::npos);
}

TEST_CASE("ListParser rejects malformed DNS labels") {
    const std::vector<std::string> invalid = {
             "", "*", "*.*.example.com", "example..com", ".example.com",
             "example.com..", "bad-.example", "-bad.example",
             std::string(64, 'a') + ".example",
             "example.com\nserver=/evil/1.1.1.1"};
    for (const std::string& value : invalid) {
        CAPTURE(value);
        CHECK_FALSE(ListParser::normalize_domain(value).has_value());
    }
}

} // namespace keen_pbr3
