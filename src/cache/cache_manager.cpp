#include "cache_manager.hpp"

#include <chrono>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>

namespace keen_pbr3 {

static std::string current_time_iso() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_r(&time_t, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

CacheManager::CacheManager(const std::filesystem::path& cache_dir)
    : cache_dir_(cache_dir) {}

void CacheManager::ensure_dir() {
    std::filesystem::create_directories(cache_dir_);
}

bool CacheManager::download(const std::string& name, const std::string& url) {
    // Load existing metadata for conditional request headers
    CacheMetadata existing = load_metadata(name);

    ConditionalDownloadResult result;
    try {
        result = http_client_.download_conditional(
            url, existing.etag, existing.last_modified);
    } catch (const std::exception&) {
        // On failure, do not overwrite existing cache
        return false;
    }

    if (result.not_modified) {
        // Update metadata with potentially refreshed ETag/Last-Modified
        CacheMetadata meta = existing;
        if (!result.etag.empty()) {
            meta.etag = result.etag;
        }
        if (!result.last_modified.empty()) {
            meta.last_modified = result.last_modified;
        }
        meta.download_time = current_time_iso();
        save_metadata(name, meta);
        return false;
    }

    // Write new content to cache file
    std::filesystem::path path = cache_path(name);
    {
        std::ofstream ofs(path, std::ios::binary);
        if (!ofs) {
            return false;
        }
        ofs << result.body;
    }

    // Save metadata
    CacheMetadata meta;
    meta.etag = result.etag;
    meta.last_modified = result.last_modified;
    meta.url = url;
    meta.download_time = current_time_iso();
    // counts are left as nullopt; caller can update via save_metadata after counting
    save_metadata(name, meta);

    return true;
}

bool CacheManager::has_cache(const std::string& name) const {
    return std::filesystem::exists(cache_path(name));
}

std::filesystem::path CacheManager::cache_path(const std::string& name) const {
    return cache_dir_ / (name + ".txt");
}

std::filesystem::path CacheManager::meta_path(const std::string& name) const {
    return cache_dir_ / (name + ".meta.json");
}

CacheMetadata CacheManager::load_metadata(const std::string& name) const {
    CacheMetadata meta;
    std::filesystem::path path = meta_path(name);

    if (!std::filesystem::exists(path)) {
        return meta;
    }

    std::ifstream ifs(path);
    if (!ifs) {
        return meta;
    }

    try {
        nlohmann::json j;
        ifs >> j;

        if (j.contains("etag")) {
            meta.etag = j["etag"].get<std::string>();
        }
        if (j.contains("last_modified")) {
            meta.last_modified = j["last_modified"].get<std::string>();
        }
        if (j.contains("url")) {
            meta.url = j["url"].get<std::string>();
        }
        if (j.contains("download_time")) {
            meta.download_time = j["download_time"].get<std::string>();
        }
        if (j.contains("ips")) {
            meta.ips = j["ips"].get<size_t>();
        }
        if (j.contains("cidrs")) {
            meta.cidrs = j["cidrs"].get<size_t>();
        }
        if (j.contains("domains")) {
            meta.domains = j["domains"].get<size_t>();
        }
    } catch (const nlohmann::json::exception&) {
        // Corrupted metadata, return empty
        return CacheMetadata{};
    }

    return meta;
}

void CacheManager::save_metadata(const std::string& name,
                                 const CacheMetadata& meta) {
    nlohmann::json j;
    j["etag"] = meta.etag;
    j["last_modified"] = meta.last_modified;
    j["url"] = meta.url;
    j["download_time"] = meta.download_time;

    if (meta.ips.has_value()) {
        j["ips"] = *meta.ips;
    }
    if (meta.cidrs.has_value()) {
        j["cidrs"] = *meta.cidrs;
    }
    if (meta.domains.has_value()) {
        j["domains"] = *meta.domains;
    }

    std::ofstream ofs(meta_path(name));
    if (ofs) {
        ofs << j.dump(2) << '\n';
    }
}

} // namespace keen_pbr3
