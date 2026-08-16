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

void ConfigStore::stage_config(Config staged_config) {
    KPBR_SHARED_UNIQUE_LOCK(lock, mutex_);
    staged_config_ = std::move(staged_config);
    ++staged_revision_;
}

std::optional<StagedConfigSnapshot> ConfigStore::staged_snapshot() const {
    KPBR_SHARED_LOCK(lock, mutex_);
    if (!staged_config_.has_value()) return std::nullopt;
    return StagedConfigSnapshot{*staged_config_, staged_revision_};
}

void ConfigStore::clear_staged() {
    KPBR_SHARED_UNIQUE_LOCK(lock, mutex_);
    staged_config_.reset();
}

void ConfigStore::clear_staged_if_revision(std::uint64_t revision) {
    KPBR_SHARED_UNIQUE_LOCK(lock, mutex_);
    if (staged_config_.has_value() && staged_revision_ == revision) {
        staged_config_.reset();
    }
}

} // namespace keen_pbr3
