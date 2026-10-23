#include "download_cmd.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace keen_pbr3 {

namespace {

std::vector<std::string> string_array(const nlohmann::json& result, const char* key) {
    std::vector<std::string> out;
    const auto it = result.find(key);
    if (it == result.end() || !it->is_array()) return out;
    for (const auto& item : *it) {
        if (item.is_string()) out.push_back(item.get<std::string>());
    }
    return out;
}

bool contains(const std::vector<std::string>& names, const std::string& name) {
    return std::find(names.begin(), names.end(), name) != names.end();
}

} // namespace

nlohmann::json format_download_json(const nlohmann::json& result) {
    const auto refreshed = string_array(result, "refreshed_lists");
    const auto changed = string_array(result, "changed_lists");
    const auto failed = string_array(result, "failed_lists");
    const auto messages = result.value("failure_messages", nlohmann::json::object());

    nlohmann::json lists = nlohmann::json::array();
    int changed_count = 0;
    int failed_count = 0;
    for (const auto& name : refreshed) {
        nlohmann::json item = {{"list", name}};
        if (contains(failed, name)) {
            ++failed_count;
            item["status"] = "failed";
            item["error"] = messages.value(name, std::string("unknown error"));
        } else if (contains(changed, name)) {
            ++changed_count;
            item["status"] = "changed";
        } else {
            item["status"] = "unchanged";
        }
        lists.push_back(std::move(item));
    }

    const int total = static_cast<int>(refreshed.size());
    return {{"lists", std::move(lists)},
            {"summary",
             {{"refreshed", total},
              {"changed", changed_count},
              {"unchanged", total - changed_count - failed_count},
              {"failed", failed_count}}},
            {"reloaded", result.value("reloaded", false)}};
}

std::string format_download_text(const nlohmann::json& result, const term::Style& style) {
    const auto json = format_download_json(result);
    const auto& lists = json.at("lists");
    const auto& summary = json.at("summary");
    if (lists.empty()) return "No lists with a URL to download.\n";

    size_t width = 0;
    for (const auto& item : lists) {
        width = std::max(width, item.at("list").get<std::string>().size());
    }
    std::string out;
    for (const auto& item : lists) {
        const std::string name = item.at("list").get<std::string>();
        const std::string status = item.at("status").get<std::string>();
        std::string mark;
        std::string text;
        if (status == "failed") {
            mark = term::painted_glyph(style, term::Glyph::Error);
            text = style.paint(term::Color::Red, item.value("error", std::string("unknown error")));
        } else if (status == "changed") {
            mark = term::painted_glyph(style, term::Glyph::Ok);
            text = "updated";
        } else {
            mark = term::painted_glyph(style, term::Glyph::Dot);
            text = style.paint(term::Color::Dim, "unchanged");
        }
        out += mark + " " + name + std::string(width - name.size() + 2, ' ') + text + "\n";
    }

    const int failed = summary.at("failed").get<int>();
    const int total = summary.at("refreshed").get<int>();
    out += "\n" + std::to_string(total) + (total == 1 ? " list: " : " lists: ") +
           std::to_string(summary.at("changed").get<int>()) + " updated, " +
           std::to_string(summary.at("unchanged").get<int>()) + " unchanged, " +
           (failed > 0 ? style.paint(term::Color::Red, std::to_string(failed) + " failed")
                       : std::to_string(failed) + " failed") +
           "\n";
    if (json.at("reloaded").get<bool>()) out += "The service is using the updated lists.\n";
    return out;
}

} // namespace keen_pbr3
