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

} // namespace keen_pbr3
