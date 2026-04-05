#include "config_store.hpp"

#include "../config/config.hpp"

namespace keen_pbr3 {

ConfigStore::ConfigStore(Config active_config)
    : active_config_(std::move(active_config))
    , active_outbound_marks_(allocate_outbound_marks(
          active_config_.fwmark.value_or(FwmarkConfig{}),
          active_config_.outbounds.value_or(std::vector<Outbound>{}))) {}

ActiveConfigSnapshot ConfigStore::active_snapshot() const {
    KPBR_SHARED_LOCK(lock, mutex_);
    return ActiveConfigSnapshot{active_config_, active_outbound_marks_};
}

Config ConfigStore::active_config() const {
    KPBR_SHARED_LOCK(lock, mutex_);
    return active_config_;
}

OutboundMarkMap ConfigStore::outbound_marks() const {
    KPBR_SHARED_LOCK(lock, mutex_);
    return active_outbound_marks_;
}

Config ConfigStore::visible_config() const {
    KPBR_SHARED_LOCK(lock, mutex_);
    return staged_config_.has_value() ? *staged_config_ : active_config_;
}

bool ConfigStore::config_is_draft() const {
    KPBR_SHARED_LOCK(lock, mutex_);
    return staged_config_.has_value();
}

void ConfigStore::replace_active(Config active_config, OutboundMarkMap outbound_marks) {
    KPBR_SHARED_UNIQUE_LOCK(lock, mutex_);
    active_config_ = std::move(active_config);
    active_outbound_marks_ = std::move(outbound_marks);
}

void ConfigStore::stage_config(Config staged_config, std::string staged_config_json) {
    KPBR_SHARED_UNIQUE_LOCK(lock, mutex_);
    staged_config_ = std::move(staged_config);
    staged_config_json_ = std::move(staged_config_json);
}

std::optional<std::pair<Config, std::string>> ConfigStore::staged_snapshot() const {
    KPBR_SHARED_LOCK(lock, mutex_);
    if (!staged_config_.has_value() || !staged_config_json_.has_value()) {
        return std::nullopt;
    }
    return std::make_optional(std::make_pair(*staged_config_, *staged_config_json_));
}

void ConfigStore::clear_staged() {
    KPBR_SHARED_UNIQUE_LOCK(lock, mutex_);
    staged_config_.reset();
    staged_config_json_.reset();
}

void ConfigStore::clear_staged_if_matches(const std::string& staged_config_json) {
    KPBR_SHARED_UNIQUE_LOCK(lock, mutex_);
    if (staged_config_json_.has_value() && *staged_config_json_ == staged_config_json) {
        staged_config_.reset();
        staged_config_json_.reset();
    }
}

} // namespace keen_pbr3
