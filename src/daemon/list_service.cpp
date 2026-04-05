#include "list_service.hpp"

#include "../log/logger.hpp"

namespace keen_pbr3 {

namespace {

const std::map<std::string, ListConfig>& config_lists(const Config& config) {
    static const std::map<std::string, ListConfig> empty_lists;
    return config.lists ? *config.lists : empty_lists;
}

} // namespace

RemoteListTargetSelection select_remote_list_targets(
    const Config& config,
    const std::optional<std::string>& requested_name) {
    RemoteListTargetSelection selection;
    const auto& lists = config_lists(config);

    if (requested_name.has_value()) {
        auto it = lists.find(*requested_name);
        if (it == lists.end()) {
            selection.error = RemoteListTargetSelectionError::NotFound;
            return selection;
        }
        if (!it->second.url.has_value()) {
            selection.error = RemoteListTargetSelectionError::NotRemote;
            return selection;
        }

        selection.list_names.push_back(it->first);
        return selection;
    }

    for (const auto& [name, list_cfg] : lists) {
        if (list_cfg.url.has_value()) {
            selection.list_names.push_back(name);
        }
    }

    return selection;
}

std::set<std::string> collect_relevant_list_names(const Config& config) {
    std::set<std::string> relevant_lists;

    for (const auto& rule : config.route.value_or(RouteConfig{}).rules.value_or(std::vector<RouteRule>{})) {
        relevant_lists.insert(rule.list.begin(), rule.list.end());
    }

    for (const auto& rule : config.dns.value_or(DnsConfig{}).rules.value_or(std::vector<DnsRule>{})) {
        relevant_lists.insert(rule.list.begin(), rule.list.end());
    }

    return relevant_lists;
}

bool should_reload_runtime_after_list_refresh(
    bool routing_runtime_active,
    const RemoteListsRefreshResult& refresh_result) {
    return routing_runtime_active && refresh_result.any_relevant_changed();
}

std::map<std::string, api::ListRefreshStateValue> build_list_refresh_state_map(
    const Config& config,
    const CacheManager& cache_manager) {
    std::map<std::string, api::ListRefreshStateValue> refresh_state;

    for (const auto& [name, list_cfg] : config_lists(config)) {
        if (!list_cfg.url.has_value()) {
            continue;
        }

        api::ListRefreshStateValue state;
        const auto metadata = cache_manager.load_metadata(name);
        state.last_updated = metadata.download_time;
        refresh_state.emplace(name, std::move(state));
    }

    return refresh_state;
}

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
    (void)download_remote_lists(config, outbound_marks, true, nullptr, nullptr);
}

RemoteListsRefreshResult ListService::refresh_remote_lists(
    const Config& config,
    const OutboundMarkMap& outbound_marks,
    const std::set<std::string>* relevant_lists,
    const std::set<std::string>* target_lists) {
    return download_remote_lists(config, outbound_marks, false, relevant_lists, target_lists);
}

RemoteListsRefreshResult ListService::download_remote_lists(
    const Config& config,
    const OutboundMarkMap& outbound_marks,
    bool only_uncached,
    const std::set<std::string>* relevant_lists,
    const std::set<std::string>* target_lists) {
    RemoteListsRefreshResult result;

    for (const auto& [name, list_cfg] : config_lists(config)) {
        if (!list_cfg.url.has_value()) {
            continue;
        }
        if (target_lists && target_lists->count(name) == 0) {
            continue;
        }

        if (only_uncached) {
            std::lock_guard<std::mutex> lock(mutex_);
            if (cache_manager_.has_cache(name)) {
                continue;
            }
        }

        result.refreshed_lists.push_back(name);

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

        if (!changed) {
            continue;
        }

        result.changed_lists.push_back(name);
        if (relevant_lists && relevant_lists->count(name) > 0) {
            result.relevant_changed_lists.push_back(name);
        }
    }

    return result;
}

} // namespace keen_pbr3
