#include <doctest/doctest.h>
#include "util/cron.hpp"
#include "config/config.hpp"
#include <chrono>
#include <ctime>

using namespace keen_pbr3;

// =============================================================================
// cron_validate — valid expressions
// =============================================================================

TEST_CASE("cron_validate: valid expressions") {
    CHECK_NOTHROW(cron_validate("* * * * *"));
    CHECK_NOTHROW(cron_validate("0 4 * * *"));
    CHECK_NOTHROW(cron_validate("*/5 * * * *"));
    CHECK_NOTHROW(cron_validate("0,30 9-17 * * 1-5"));
    CHECK_NOTHROW(cron_validate("59 23 31 12 6"));
    CHECK_NOTHROW(cron_validate("0 0 1 1 *"));
    CHECK_NOTHROW(cron_validate("0 0 1 * *"));
}

// =============================================================================
// cron_validate — invalid expressions
// =============================================================================

TEST_CASE("cron_validate: invalid expressions") {
    CHECK_THROWS_AS(cron_validate("* * * *"),           std::invalid_argument);
    CHECK_THROWS_AS(cron_validate("* * * * * *"),       std::invalid_argument);
    CHECK_THROWS_AS(cron_validate("60 * * * *"),        std::invalid_argument);
    CHECK_THROWS_AS(cron_validate("* 24 * * *"),        std::invalid_argument);
    CHECK_THROWS_AS(cron_validate("* * 0 * *"),         std::invalid_argument);
    CHECK_THROWS_AS(cron_validate("* * 32 * *"),        std::invalid_argument);
    CHECK_THROWS_AS(cron_validate("* * * 0 *"),         std::invalid_argument);
    CHECK_THROWS_AS(cron_validate("* * * 13 *"),        std::invalid_argument);
    CHECK_THROWS_AS(cron_validate("* * * * 7"),         std::invalid_argument);
    CHECK_THROWS_AS(cron_validate("*/0 * * * *"),       std::invalid_argument);
    CHECK_THROWS_AS(cron_validate("5-3 * * * *"),       std::invalid_argument);
    CHECK_THROWS_AS(cron_validate("a * * * *"),         std::invalid_argument);
    CHECK_THROWS_AS(cron_validate(""),                  std::invalid_argument);
}

// =============================================================================
// cron_next — deterministic calendar tests
// =============================================================================

TEST_CASE("cron_next: deterministic calendar tests") {
    auto make_local = [](int y, int mon, int d, int h, int min, int s = 0) {
        std::tm tm{};
        tm.tm_year = y - 1900; tm.tm_mon = mon - 1; tm.tm_mday = d;
        tm.tm_hour = h; tm.tm_min = min; tm.tm_sec = s;
        tm.tm_isdst = -1;
        return std::chrono::system_clock::from_time_t(mktime(&tm));
    };
    auto to_tm = [](std::chrono::system_clock::time_point tp) {
        time_t t = std::chrono::system_clock::to_time_t(tp);
        std::tm tm{}; localtime_r(&t, &tm); return tm;
    };

    SUBCASE("every-minute advances 1 min") {
        auto from = make_local(2024, 1, 15, 3, 30, 0);
        auto next = cron_next("* * * * *", from);
        auto tm = to_tm(next);
        CHECK(tm.tm_year == 2024 - 1900);
        CHECK(tm.tm_mon  == 0);
        CHECK(tm.tm_mday == 15);
        CHECK(tm.tm_hour == 3);
        CHECK(tm.tm_min  == 31);
        CHECK(tm.tm_sec  == 0);
    }

    SUBCASE("every-minute still advances past from (mid-minute)") {
        auto from = make_local(2024, 1, 15, 3, 30, 30);
        auto next = cron_next("* * * * *", from);
        auto tm = to_tm(next);
        CHECK(tm.tm_hour == 3);
        CHECK(tm.tm_min  == 31);
        CHECK(tm.tm_sec  == 0);
    }

    SUBCASE("daily 04:00, before") {
        auto from = make_local(2024, 1, 15, 3, 59, 0);
        auto next = cron_next("0 4 * * *", from);
        auto tm = to_tm(next);
        CHECK(tm.tm_mday == 15);
        CHECK(tm.tm_hour == 4);
        CHECK(tm.tm_min  == 0);
    }

    SUBCASE("daily 04:00, after") {
        auto from = make_local(2024, 1, 15, 4, 1, 0);
        auto next = cron_next("0 4 * * *", from);
        auto tm = to_tm(next);
        CHECK(tm.tm_mday == 16);
        CHECK(tm.tm_hour == 4);
        CHECK(tm.tm_min  == 0);
    }

    SUBCASE("quarter-hour steps") {
        auto from = make_local(2024, 1, 15, 10, 0, 0);
        auto next = cron_next("*/15 * * * *", from);
        auto tm = to_tm(next);
        CHECK(tm.tm_hour == 10);
        CHECK(tm.tm_min  == 15);
    }

    SUBCASE("weekly Monday midnight") {
        // 2024-01-16 is a Tuesday
        auto from = make_local(2024, 1, 16, 0, 1, 0);
        auto next = cron_next("0 0 * * 1", from);
        auto tm = to_tm(next);
        // Next Monday is 2024-01-22
        CHECK(tm.tm_year == 2024 - 1900);
        CHECK(tm.tm_mon  == 0);
        CHECK(tm.tm_mday == 22);
        CHECK(tm.tm_hour == 0);
        CHECK(tm.tm_min  == 0);
    }

    SUBCASE("1st of month midnight") {
        auto from = make_local(2024, 1, 15, 12, 0, 0);
        auto next = cron_next("0 0 1 * *", from);
        auto tm = to_tm(next);
        CHECK(tm.tm_mon  == 1); // February
        CHECK(tm.tm_mday == 1);
        CHECK(tm.tm_hour == 0);
        CHECK(tm.tm_min  == 0);
    }

    SUBCASE("result always > from") {
        auto from = make_local(2024, 6, 1, 12, 0, 0);
        auto next = cron_next("* * * * *", from);
        CHECK(next > from);
    }

    SUBCASE("result is at second=0") {
        auto from = make_local(2024, 6, 1, 12, 0, 0);
        auto next = cron_next("* * * * *", from);
        auto tm = to_tm(next);
        CHECK(tm.tm_sec == 0);
    }

    SUBCASE("invalid expr throws") {
        auto from = make_local(2024, 1, 1, 0, 0, 0);
        CHECK_THROWS_AS(cron_next("99 * * * *", from), std::invalid_argument);
    }
}

// =============================================================================
// parse_config: lists_autoupdate section
// =============================================================================

TEST_CASE("parse_config: lists_autoupdate") {
    SUBCASE("absent key → defaults") {
        auto cfg = parse_config("{}");
        CHECK(cfg.lists_autoupdate.value_or(ListsAutoupdateConfig{}).enabled.value_or(false) == false);
        CHECK(cfg.lists_autoupdate.value_or(ListsAutoupdateConfig{}).cron.value_or("")    == "");
    }

    SUBCASE("enabled false, no cron") {
        auto cfg = parse_config(R"({"lists_autoupdate":{"enabled":false}})");
        CHECK(cfg.lists_autoupdate->enabled.value_or(false) == false);
        CHECK(cfg.lists_autoupdate->cron.value_or("")    == "");
    }

    SUBCASE("enabled true, valid cron") {
        auto cfg = parse_config(R"({"lists_autoupdate":{"enabled":true,"cron":"0 4 * * *"}})");
        CHECK(cfg.lists_autoupdate->enabled.value_or(false) == true);
        CHECK(cfg.lists_autoupdate->cron.value_or("") == "0 4 * * *");
    }

    SUBCASE("enabled true, missing cron key") {
        CHECK_THROWS_AS(
            parse_config(R"({"lists_autoupdate":{"enabled":true}})"),
            ConfigError);
    }

    SUBCASE("enabled true, empty cron") {
        CHECK_THROWS_AS(
            parse_config(R"({"lists_autoupdate":{"enabled":true,"cron":""}})"),
            ConfigError);
    }

    SUBCASE("invalid cron string") {
        CHECK_THROWS_AS(
            parse_config(R"({"lists_autoupdate":{"cron":"99 * * * *"}})"),
            ConfigError);
    }

    SUBCASE("invalid cron even when disabled") {
        CHECK_THROWS_AS(
            parse_config(R"({"lists_autoupdate":{"enabled":false,"cron":"99 * * * *"}})"),
            ConfigError);
    }
}
