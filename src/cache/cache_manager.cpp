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

void CacheManager::set_fwmark(uint32_t mark) {
    http_client_.set_fwmark(mark);
}

void CacheManager::set_max_response_size(size_t bytes) {
    http_client_.set_max_response_size(bytes);
}

bool CacheManager::download(const std::string& name, const std::string& url) {
    CacheMetadata existing = load_metadata(name);

    ConditionalDownloadResult result;
    try {
        result = http_client_.download_conditional(
            url, existing.etag.value_or(""), existing.last_modified.value_or(""));
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

    std::filesystem::path path = cache_path(name);
    {
        std::ofstream ofs(path, std::ios::binary);
        if (!ofs) return false;
        ofs << result.body;
    }

    CacheMetadata meta;
    meta.etag = result.etag;
    meta.last_modified = result.last_modified;
    meta.url = url;
    meta.download_time = current_time_iso();
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
    std::ifstream ifs(meta_path(name));
    if (!ifs.is_open()) return {};
    try {
        return nlohmann::json::parse(ifs).get<CacheMetadata>();
    } catch (const nlohmann::json::exception&) {
        return {};
    }
}

void CacheManager::save_metadata(const std::string& name, const CacheMetadata& meta) {
    std::ofstream ofs(meta_path(name));
    if (ofs.is_open()) ofs << nlohmann::json(meta).dump(2) << '\n';
}

} // namespace keen_pbr3
