#include <doctest/doctest.h>

#include "../src/cmd/download_cmd.hpp"

#include <nlohmann/json.hpp>

using namespace keen_pbr3;

TEST_CASE("download --json lists every refreshed list with its outcome") {
    const nlohmann::json result = {
        {"refreshed_lists", {"google", "apple", "broken"}},
        {"changed_lists", {"google"}},
        {"unchanged_lists", {"apple"}},
        {"failed_lists", {"broken"}},
        {"failure_messages", {{"broken", "HTTP 503"}}},
        {"reloaded", true}};

    const auto json = format_download_json(result);

    REQUIRE(json.at("lists").size() == 3);
    CHECK(json.at("lists")[0] == nlohmann::json{{"list", "google"}, {"status", "changed"}});
    CHECK(json.at("lists")[1] == nlohmann::json{{"list", "apple"}, {"status", "unchanged"}});
    CHECK(json.at("lists")[2] ==
          nlohmann::json{{"list", "broken"}, {"status", "failed"}, {"error", "HTTP 503"}});
    CHECK(json.at("summary") == nlohmann::json{{"refreshed", 3},
                                               {"changed", 1},
                                               {"unchanged", 1},
                                               {"failed", 1}});
    CHECK(json.at("reloaded") == true);
}

TEST_CASE("download --json copes with an answer from a daemon without the new fields") {
    const nlohmann::json result = {{"refreshed_lists", {"google", "broken"}},
                                   {"changed_lists", nlohmann::json::array()},
                                   {"failed_lists", {"broken"}},
                                   {"reloaded", false}};

    const auto json = format_download_json(result);

    CHECK(json.at("lists")[0].at("status") == "unchanged");
    CHECK(json.at("lists")[1].at("error") == "unknown error");
    CHECK(json.at("reloaded") == false);
    CHECK(format_download_json(nlohmann::json::object()).at("lists").empty());
}

TEST_CASE("download prints one line per list and a summary") {
    const nlohmann::json result = {
        {"refreshed_lists", {"google", "apple", "broken"}},
        {"changed_lists", {"google"}},
        {"failed_lists", {"broken"}},
        {"failure_messages", {{"broken", "HTTP 503"}}},
        {"reloaded", true}};

    keen_pbr3::term::Style plain;
    CHECK(format_download_text(result, plain) ==
          "[ok] google  updated\n"
          "- apple   unchanged\n"
          "[xx] broken  HTTP 503\n"
          "\n"
          "3 lists: 1 updated, 1 unchanged, 1 failed\n"
          "The service is using the updated lists.\n");

    keen_pbr3::term::Style colored;
    colored.color = true;
    const auto text = format_download_text(result, colored);
    CHECK(text.find("\033[32m[ok]\033[0m google") == 0);
    CHECK(text.find("\033[31mHTTP 503\033[0m") != std::string::npos);

    CHECK(format_download_text(nlohmann::json::object(), plain) ==
          "No lists with a URL to download.\n");
}
