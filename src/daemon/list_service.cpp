#include "list_service.hpp"

#include "../log/logger.hpp"

namespace keen_pbr3 {

namespace {

const std::map<std::string, ListConfig>& config_lists(const Config& config) {
    static const std::map<std::string, ListConfig> empty_lists;
    return config.lists ? *config.lists : empty_lists;
}

std::string refresh_flight_key(const Config& config,
                               const OutboundMarkMap& outbound_marks,
                               bool only_uncached,
                               const std::set<std::string>* relevant_lists,
                               const std::set<std::string>* target_lists) {
    nlohmann::json key;
    key["config"] = config;
    key["marks"] = outbound_marks;
    key["only_uncached"] = only_uncached;
    key["relevant"] = relevant_lists ? nlohmann::json(*relevant_lists) : nlohmann::json(nullptr);
    key["targets"] = target_lists ? nlohmann::json(*target_lists) : nlohmann::json(nullptr);
    return key.dump();
}

} // namespace

RemoteListTargetSelection select_remote_list_targets(const Config& config,
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
        if (!route_rule_enabled(rule)) {
            continue;
        }
        const auto& route_lists = route_rule_lists(rule);
        relevant_lists.insert(route_lists.begin(), route_lists.end());
    }

    for (const auto& rule : config.dns.value_or(DnsConfig{}).rules.value_or(std::vector<DnsRule>{})) {
        if (!dns_rule_enabled(rule)) {
            continue;
        }
        relevant_lists.insert(rule.list.begin(), rule.list.end());
    }

    return relevant_lists;
}

bool should_reload_runtime_after_list_refresh(bool routing_runtime_active,
                                              const RemoteListsRefreshResult& refresh_result) {
    return routing_runtime_active && refresh_result.any_relevant_changed();
}

std::map<std::string, api::ListRefreshStateValue> build_list_refresh_state_map(const Config& config,
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

ListService::ListService(const std::filesystem::path& cache_dir, size_t max_file_size_bytes)
    : cache_manager_(cache_dir, max_file_size_bytes) {
}

void ListService::ensure_dir() {
    KPBR_LOCK_GUARD(mutex_);
    cache_manager_.ensure_dir();
}

const CacheManager& ListService::cache_manager() const {
    return cache_manager_;
}

RemoteListsRefreshResult ListService::download_uncached(const Config& config,
                                                        const OutboundMarkMap& outbound_marks,
                                                        const std::set<std::string>* relevant_lists) {
    return download_remote_lists(config, outbound_marks, true, relevant_lists, nullptr);
}

RemoteListsRefreshResult ListService::refresh_remote_lists(const Config& config,
                                                           const OutboundMarkMap& outbound_marks,
                                                           const std::set<std::string>* relevant_lists,
                                                           const std::set<std::string>* target_lists) {
    return download_remote_lists(config, outbound_marks, false, relevant_lists, target_lists);
}

RemoteListsRefreshResult ListService::download_remote_lists(const Config& config,
                                                            const OutboundMarkMap& outbound_marks,
                                                            bool only_uncached,
                                                            const std::set<std::string>* relevant_lists,
                                                            const std::set<std::string>* target_lists) {
    // All entry points converge here. Matching callers join one flight and
    // receive its result; different scopes wait so deterministic cache temp
    // paths are never shared by API, scheduled, and startup refreshes.
    const std::string flight_key =
        refresh_flight_key(config, outbound_marks, only_uncached, relevant_lists, target_lists);
    std::shared_ptr<RefreshFlight> flight;
    bool owner = false;
    {
        std::unique_lock<std::mutex> lock(refresh_mutex_);
        while (refresh_flight_ && refresh_flight_->key != flight_key) {
            refresh_available_.wait(lock);
        }
        if (refresh_flight_) {
            flight = refresh_flight_;
        } else {
            flight = std::make_shared<RefreshFlight>();
            flight->key = flight_key;
            refresh_flight_ = flight;
            owner = true;
        }
    }

    if (!owner) {
        std::unique_lock<std::mutex> lock(refresh_mutex_);
        flight->completed.wait(lock, [&flight] { return flight->done; });
        if (flight->error)
            std::rethrow_exception(flight->error);
        return flight->result;
    }

    RemoteListsRefreshResult result;
    try {
        for (const auto& [name, list_cfg] : config_lists(config)) {
            if (!list_cfg.url.has_value()) {
                continue;
            }
            if (target_lists && target_lists->count(name) == 0) {
                continue;
            }

            if (only_uncached) {
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
                    Logger::instance().warn("List '{}': detour outbound '{}' not found, "
                                            "using default routing",
                                            name,
                                            *list_cfg.detour);
                }
            }

            const auto download_result = cache_manager_.download(name, *list_cfg.url, CacheDownloadOptions{fwmark});

            if (download_result.failed()) {
                result.failed_lists.push_back(name);
                Logger::instance().warn("List '{}': failed to refresh {}: {}",
                                        name,
                                        *list_cfg.url,
                                        download_result.error_message.empty() ? std::string("unknown error")
                                                                              : download_result.error_message);
                continue;
            }

            if (!download_result.updated()) {
                continue;
            }

            result.changed_lists.push_back(name);
            if (relevant_lists && relevant_lists->count(name) > 0) {
                result.relevant_changed_lists.push_back(name);
            }
        }
    } catch (...) {
        std::unique_lock<std::mutex> lock(refresh_mutex_);
        flight->error = std::current_exception();
        flight->done = true;
        refresh_flight_.reset();
        lock.unlock();
        flight->completed.notify_all();
        refresh_available_.notify_all();
        throw;
    }

    {
        std::unique_lock<std::mutex> lock(refresh_mutex_);
        flight->result = result;
        flight->done = true;
        refresh_flight_.reset();
    }
    flight->completed.notify_all();
    refresh_available_.notify_all();
    return result;
}

} // namespace keen_pbr3
