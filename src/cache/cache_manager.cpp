#include "cache_manager.hpp"

#include <chrono>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <string_view>
#include <utility>

namespace keen_pbr3 {

namespace {

static std::string current_time_iso() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_r(&time_t, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

CacheDownloadResult download_failed(std::string message,
                                    std::optional<long> http_status_code = std::nullopt) {
    CacheDownloadResult result;
    result.status = CacheDownloadStatus::Failed;
    result.error_message = std::move(message);
    result.http_status_code = http_status_code;
    return result;
}

std::string clean_download_error_message(const std::exception& error) {
    constexpr std::string_view prefix = "HTTP request failed: ";
    std::string message = error.what();
    if (message.rfind(prefix, 0) == 0) {
        message.erase(0, prefix.size());
    }
    return message;
}

bool cache_contents_equal(const std::filesystem::path& path, const std::string& body) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }
    const std::string existing((std::istreambuf_iterator<char>(input)),
                               std::istreambuf_iterator<char>());
    return existing == body;
}

} // namespace

CacheManager::CacheManager(const std::filesystem::path& cache_dir,
                           size_t max_file_size_bytes)
    : cache_dir_(cache_dir)
    , max_file_size_bytes_(max_file_size_bytes) {
    http_client_.set_max_response_size(max_file_size_bytes);
}

void CacheManager::ensure_dir() {
    std::filesystem::create_directories(cache_dir_);
}

void CacheManager::set_max_file_size(size_t bytes) {
    max_file_size_bytes_ = bytes;
    http_client_.set_max_response_size(bytes);
}

CacheDownloadResult CacheManager::download(const std::string& name,
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
    } catch (const HttpError& e) {
        if (e.status_code() > 0) {
            return download_failed("HTTP " + std::to_string(e.status_code()), e.status_code());
        }
        return download_failed(clean_download_error_message(e));
    } catch (const std::exception& e) {
        return download_failed(e.what());
    }

    if (result.not_modified) {
        CacheDownloadResult not_modified;
        not_modified.status = CacheDownloadStatus::NotModified;
        return not_modified;
    }

    std::filesystem::path final_path = cache_path(name);
    // The cache version is the raw payload, not transport metadata. A server
    // may return an equivalent 200 response with a new ETag; keep both the
    // file and metadata intact so callers do not restart or reconcile.
    if (cache_contents_equal(final_path, result.body)) {
        CacheDownloadResult unchanged;
        unchanged.status = CacheDownloadStatus::NotModified;
        return unchanged;
    }

    std::filesystem::path final_meta = meta_path(name);
    std::filesystem::path tmp_path = cache_dir_ / (name + ".txt.tmp");
    std::filesystem::path tmp_meta = cache_dir_ / (name + ".meta.json.tmp");

    {
        std::ofstream ofs(tmp_path, std::ios::binary);
        if (!ofs) return download_failed("failed to open temporary cache file for writing");
        ofs << result.body;
        if (!ofs) {
            std::filesystem::remove(tmp_path);
            return download_failed("failed to write temporary cache file");
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
            return download_failed("failed to open temporary cache metadata for writing");
        }
        ofs << nlohmann::json(meta).dump(2) << '\n';
        if (!ofs) {
            std::filesystem::remove(tmp_path);
            std::filesystem::remove(tmp_meta);
            return download_failed("failed to write temporary cache metadata");
        }
    }

    // Rename body first: on crash here, old meta triggers a re-download (safe).
    // Rename meta second: once both succeed the cache is fully consistent.
    try {
        std::filesystem::rename(tmp_path, final_path);
        std::filesystem::rename(tmp_meta, final_meta);
    } catch (const std::exception& e) {
        std::filesystem::remove(tmp_path);
        std::filesystem::remove(tmp_meta);
        return download_failed(e.what());
    }

    CacheDownloadResult updated;
    updated.status = CacheDownloadStatus::Updated;
    return updated;
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
