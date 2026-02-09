#pragma once

#include "../http/http_client.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>

namespace keen_pbr3 {

struct CacheMetadata {
    std::string etag;
    std::string last_modified;
    std::string url;
    std::string download_time;
    std::optional<size_t> ips;
    std::optional<size_t> cidrs;
    std::optional<size_t> domains;
};

class CacheManager {
public:
    explicit CacheManager(const std::filesystem::path& cache_dir);

    // Create cache directory if it doesn't exist.
    void ensure_dir();

    // Download a list from URL using conditional requests (ETag/If-Modified-Since).
    // Returns true if content was updated, false if 304 Not Modified.
    // On failure, does not overwrite existing cache.
    bool download(const std::string& name, const std::string& url);

    // Check if a cached file exists for the given list name.
    bool has_cache(const std::string& name) const;

    // Path to the cached list file: <cache_dir>/<name>.txt
    std::filesystem::path cache_path(const std::string& name) const;

    // Path to the metadata file: <cache_dir>/<name>.meta.json
    std::filesystem::path meta_path(const std::string& name) const;

    // Load metadata from .meta.json file. Returns empty metadata if file doesn't exist.
    CacheMetadata load_metadata(const std::string& name) const;

    // Save metadata to .meta.json file.
    void save_metadata(const std::string& name, const CacheMetadata& meta);

private:
    std::filesystem::path cache_dir_;
    HttpClient http_client_;
};

} // namespace keen_pbr3
