#include "disk_config_state.hpp"

#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace keen_pbr3 {

DiskConfigState inspect_disk_config_state(const std::string& config_path,
                                          const Config& active_config) {
    std::ifstream input(config_path);
    if (!input.is_open()) {
        DiskConfigState result;
        result.error = "cannot open disk config";
        return result;
    }
    std::ostringstream contents;
    contents << input.rdbuf();
    try {
        const auto disk_json = nlohmann::json::parse(contents.str());
        const nlohmann::json active_json = active_config;
        DiskConfigState result;
        result.matches_active = disk_json == active_json;
        return result;
    } catch (const std::exception& error) {
        DiskConfigState result;
        result.error = error.what();
        return result;
    }
}

} // namespace keen_pbr3
