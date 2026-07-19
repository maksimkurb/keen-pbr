#include "disk_config_state.hpp"

#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace keen_pbr3 {

DiskConfigState inspect_disk_config_state(const std::string& config_path,
                                          const Config& active_config) {
    std::ifstream input(config_path);
    if (!input.is_open()) {
        return {.matches_active = false, .error = "cannot open disk config"};
    }
    std::ostringstream contents;
    contents << input.rdbuf();
    try {
        const auto disk_json = nlohmann::json::parse(contents.str());
        const nlohmann::json active_json = active_config;
        return {.matches_active = disk_json == active_json};
    } catch (const std::exception& error) {
        return {.matches_active = false, .error = error.what()};
    }
}

} // namespace keen_pbr3
