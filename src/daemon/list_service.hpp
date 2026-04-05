#pragma once

#include "../cache/cache_manager.hpp"
#include "../config/config.hpp"

#include <mutex>
#include <set>
#include <vector>

namespace keen_pbr3 {

struct RemoteListsRefreshResult {
    std::vector<std::string> changed_lists;
    std::vector<std::string> relevant_changed_lists;

    bool any_changed() const {
        return !changed_lists.empty();
    }

    bool any_relevant_changed() const {
        return !relevant_changed_lists.empty();
    }
};

class ListService {
public:
    ListService(const std::filesystem::path& cache_dir,
                size_t max_file_size_bytes = kDefaultMaxFileSizeBytes);

    void ensure_dir();
    const CacheManager& cache_manager() const;

    void download_uncached(const Config& config, const OutboundMarkMap& outbound_marks);
    RemoteListsRefreshResult refresh_remote_lists(
        const Config& config,
        const OutboundMarkMap& outbound_marks,
        const std::set<std::string>* relevant_lists = nullptr);

private:
    RemoteListsRefreshResult download_remote_lists(
        const Config& config,
        const OutboundMarkMap& outbound_marks,
        bool only_uncached,
        const std::set<std::string>* relevant_lists);

    mutable std::mutex mutex_;
    CacheManager cache_manager_;
};

} // namespace keen_pbr3
