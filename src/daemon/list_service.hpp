#pragma once

#include "../cache/cache_manager.hpp"
#include "../config/config.hpp"

#include <mutex>
#include <set>

namespace keen_pbr3 {

class ListService {
public:
    ListService(const std::filesystem::path& cache_dir,
                size_t max_file_size_bytes = kDefaultMaxFileSizeBytes);

    void ensure_dir();
    const CacheManager& cache_manager() const;

    void download_uncached(const Config& config, const OutboundMarkMap& outbound_marks);
    bool refresh_remote_lists(const Config& config,
                              const OutboundMarkMap& outbound_marks,
                              const std::set<std::string>* relevant_lists = nullptr);

private:
    bool download_remote_lists(const Config& config,
                               const OutboundMarkMap& outbound_marks,
                               bool only_uncached,
                               const std::set<std::string>* relevant_lists);

    mutable std::mutex mutex_;
    CacheManager cache_manager_;
};

} // namespace keen_pbr3
