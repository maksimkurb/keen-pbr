#pragma once

#include "../api/generated/api_types.hpp"
#include "../config/config.hpp"
#include "../http/http_client.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

namespace keen_pbr3 {

// Use generated CacheMetadata from the API schema
using CacheMetadata = api::CacheMetadata;

struct CacheDownloadOptions {
    uint32_t fwmark{0};
};

class CacheManager {
public:
    explicit CacheManager(const std::filesystem::path& cache_dir,
                          size_t max_file_size_bytes = kDefaultMaxFileSizeBytes);

    // Create cache directory if it doesn't exist.
    void ensure_dir();

    // Set maximum allowed size for downloaded remote content.
    void set_max_file_size(size_t bytes);

    // Download a list from URL using conditional requests (ETag/If-Modified-Since).
    // Returns true if content was updated, false if 304 Not Modified.
    // On failure, does not overwrite existing cache.
    bool download(const std::string& name,
                  const std::string& url,
                  const CacheDownloadOptions& options = {});

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
