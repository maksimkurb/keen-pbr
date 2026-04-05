#include "list_service.hpp"

#include "../log/logger.hpp"

namespace keen_pbr3 {

ListService::ListService(const std::filesystem::path& cache_dir,
                         size_t max_file_size_bytes)
    : cache_manager_(cache_dir, max_file_size_bytes) {}

void ListService::ensure_dir() {
    std::lock_guard<std::mutex> lock(mutex_);
    cache_manager_.ensure_dir();
}

const CacheManager& ListService::cache_manager() const {
    return cache_manager_;
}

void ListService::download_uncached(const Config& config,
                                    const OutboundMarkMap& outbound_marks) {
    (void)download_remote_lists(config, outbound_marks, true, nullptr);
}

bool ListService::refresh_remote_lists(const Config& config,
                                       const OutboundMarkMap& outbound_marks,
                                       const std::set<std::string>* relevant_lists) {
    return download_remote_lists(config, outbound_marks, false, relevant_lists);
}

bool ListService::download_remote_lists(const Config& config,
                                        const OutboundMarkMap& outbound_marks,
                                        bool only_uncached,
                                        const std::set<std::string>* relevant_lists) {
    bool any_relevant_changed = false;

    for (const auto& [name, list_cfg] : config.lists.value_or(std::map<std::string, ListConfig>{})) {
        if (!list_cfg.url.has_value()) {
            continue;
        }

        if (only_uncached) {
            std::lock_guard<std::mutex> lock(mutex_);
            if (cache_manager_.has_cache(name)) {
                continue;
            }
        }

        uint32_t fwmark = 0;
        if (list_cfg.detour.has_value()) {
            auto it = outbound_marks.find(*list_cfg.detour);
            if (it != outbound_marks.end()) {
                fwmark = it->second;
            } else {
                Logger::instance().warn(
                    "List '{}': detour outbound '{}' not found, using default routing",
                    name,
                    *list_cfg.detour);
            }
        }

        bool changed = false;
        changed = cache_manager_.download(
            name,
            *list_cfg.url,
            CacheDownloadOptions{fwmark});

        if (changed && relevant_lists && relevant_lists->count(name) > 0) {
            any_relevant_changed = true;
        }
    }

    return any_relevant_changed;
}

} // namespace keen_pbr3
