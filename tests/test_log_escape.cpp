#include <doctest/doctest.h>

#include "../src/log/logger.hpp"

namespace keen_pbr3 {

TEST_CASE("log_escape leaves plain ASCII text unchanged") {
    CHECK(log_escape("hello world") == "hello world");
    CHECK(log_escape("Hello, World! 123") == "Hello, World! 123");
}

TEST_CASE("log_escape escapes backslash") {
    const std::string input = "a\\b";
    const std::string result = log_escape(input);
    CHECK(result == "a\\\\b");
}

TEST_CASE("log_escape escapes newline") {
    std::string input;
    input += 'l';
    input += 'i';
    input += 'n';
    input += 'e';
    input += '\n';  // newline
    input += 'x';
    const auto result = log_escape(input);
    CHECK(result.find("line") != std::string::npos);
    CHECK(result.find("\\x0a") != std::string::npos);
    CHECK(result.find("x") != std::string::npos);
}

TEST_CASE("log_escape escapes null byte") {
    const auto result = log_escape(std::string(1, '\x00'));
    CHECK(result == "\\x00");
}

TEST_CASE("log_escape escapes byte 0xFF") {
    const auto result = log_escape(std::string(1, '\xFF'));
    CHECK(result == "\\xff");
}

TEST_CASE("log_escape escapes byte 0xE9") {
    const auto result = log_escape(std::string(1, '\xE9'));
    CHECK(result == "\\xe9");
}

TEST_CASE("log_escape uses lowercase hex") {
    const auto ab_result = log_escape(std::string(1, '\xAB'));
    CHECK(ab_result == "\\xab");

    const auto cd_result = log_escape(std::string(1, '\xCD'));
    CHECK(cd_result == "\\xcd");
}

TEST_CASE("log_escape truncates at 255 bytes input") {
    const std::string input(256, 'x');
    const std::string result = log_escape(input);

    // Result should start with 255 x's
    CHECK(result.length() >= 255);
    // And should end with "..."
    CHECK(result.length() >= 258);  // at least 255 + 3
    const size_t len = result.length();
    CHECK(result[len - 3] == '.');
    CHECK(result[len - 2] == '.');
    CHECK(result[len - 1] == '.');
}

TEST_CASE("log_escape doesn't truncate at 255 bytes exactly") {
    const std::string input(255, 'x');
    const std::string result = log_escape(input);

    // Result should be exactly 255 x's, no ellipsis
    CHECK(result.length() == 255);
    CHECK(result == input);
}

TEST_CASE("log_escape handles empty string") {
    CHECK(log_escape("") == "");
}

} // namespace keen_pbr3
