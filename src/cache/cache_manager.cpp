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

CacheManager::CacheManager(const std::filesystem::path& cache_dir,
                           size_t max_file_size_bytes)
    : cache_dir_(cache_dir) {
    http_client_.set_max_response_size(max_file_size_bytes);
}

void CacheManager::ensure_dir() {
    std::filesystem::create_directories(cache_dir_);
}

void CacheManager::set_max_file_size(size_t bytes) {
    http_client_.set_max_response_size(bytes);
}

bool CacheManager::download(const std::string& name,
                            const std::string& url,
                            const CacheDownloadOptions& options) {
    CacheMetadata existing = load_metadata(name);

    ConditionalDownloadResult result;
    try {
        result = http_client_.download_conditional(
            url,
            existing.etag.value_or(""),
            existing.last_modified.value_or(""),
            HttpRequestOptions{options.fwmark});
    } catch (const std::exception&) {
        return false;
    }

    if (result.not_modified) {
        CacheMetadata meta = existing;
        if (!result.etag.empty()) meta.etag = result.etag;
        if (!result.last_modified.empty()) meta.last_modified = result.last_modified;
        meta.download_time = current_time_iso();
        save_metadata(name, meta);
        return false;
    }

    std::filesystem::path final_path = cache_path(name);
    std::filesystem::path final_meta = meta_path(name);
    std::filesystem::path tmp_path = cache_dir_ / (name + ".txt.tmp");
    std::filesystem::path tmp_meta = cache_dir_ / (name + ".meta.json.tmp");

    {
        std::ofstream ofs(tmp_path, std::ios::binary);
        if (!ofs) return false;
        ofs << result.body;
        if (!ofs) {
            std::filesystem::remove(tmp_path);
            return false;
        }
    }

    CacheMetadata meta;
    meta.etag = result.etag;
    meta.last_modified = result.last_modified;
    meta.url = url;
    meta.download_time = current_time_iso();

    {
        std::ofstream ofs(tmp_meta);
        if (!ofs) {
            std::filesystem::remove(tmp_path);
            return false;
        }
        ofs << nlohmann::json(meta).dump(2) << '\n';
        if (!ofs) {
            std::filesystem::remove(tmp_path);
            std::filesystem::remove(tmp_meta);
            return false;
        }
    }

    // Rename body first: on crash here, old meta triggers a re-download (safe).
    // Rename meta second: once both succeed the cache is fully consistent.
    std::filesystem::rename(tmp_path, final_path);
    std::filesystem::rename(tmp_meta, final_meta);

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
    std::ifstream ifs(meta_path(name));
    if (!ifs.is_open()) return {};
    try {
        return nlohmann::json::parse(ifs).get<CacheMetadata>();
    } catch (const nlohmann::json::exception&) {
        return {};
    }
}

void CacheManager::save_metadata(const std::string& name, const CacheMetadata& meta) {
    const std::filesystem::path final_meta = meta_path(name);
    const std::filesystem::path tmp_meta = cache_dir_ / (name + ".meta.json.tmp");

    {
        std::ofstream ofs(tmp_meta);
        if (!ofs) {
            return;
        }
        ofs << nlohmann::json(meta).dump(2) << '\n';
        if (!ofs) {
            std::filesystem::remove(tmp_meta);
            return;
        }
    }

    std::filesystem::rename(tmp_meta, final_meta);
}

} // namespace keen_pbr3
