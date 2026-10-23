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
