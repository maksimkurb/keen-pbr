#pragma once

#include "../config/config.hpp"
#include "../config/list_parser.hpp"
#include "../http/http_client.hpp"

#include <filesystem>
#include <map>
#include <string>

namespace keen_pbr3 {

class ListManager {
public:
    // cache_dir: directory where downloaded lists are cached on disk
    explicit ListManager(const std::map<std::string, ListConfig>& lists,
                         const std::filesystem::path& cache_dir);

    // Load all lists: download URLs, read files, merge inline entries.
    // Uses cached copies if download fails and cache exists.
    void load();

    // Re-download and refresh all lists.
    void reload();

    // Get parsed list data for a given list name.
    // Returns nullptr if the list name is not found.
    const ParsedList* get(const std::string& name) const;

    // Get all loaded lists.
    const std::map<std::string, ParsedList>& lists() const;

private:
    // Load a single list definition, merging all sources.
    ParsedList load_list(const std::string& name, const ListConfig& config);

    // Download from URL, caching the result. Falls back to cache on failure.
    std::string download_or_cache(const std::string& name, const std::string& url);

    // Read a local file's contents.
    static std::string read_file(const std::filesystem::path& path);

    std::map<std::string, ListConfig> list_configs_;
    std::filesystem::path cache_dir_;
    std::map<std::string, ParsedList> loaded_lists_;
    HttpClient http_client_;
};

} // namespace keen_pbr3
