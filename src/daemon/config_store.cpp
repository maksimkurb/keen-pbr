#include "config_store.hpp"

#include "../config/config.hpp"

#include <chrono>

namespace keen_pbr3 {

ConfigStore::ConfigStore(Config active_config)
    : active_config_(std::move(active_config))
    , active_outbound_marks_(allocate_outbound_marks(active_config_))
    , revision_(static_cast<std::uint64_t>(
          std::chrono::duration_cast<std::chrono::seconds>(
              std::chrono::system_clock::now().time_since_epoch()).count())) {}

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
    {
        KPBR_SHARED_UNIQUE_LOCK(lock, mutex_);
        active_config_ = std::move(active_config);
        active_outbound_marks_ = std::move(outbound_marks);
    }
    notify_changed();
}

void ConfigStore::replace_active_and_clear_staged_if_revision(
    Config active_config,
    OutboundMarkMap outbound_marks,
    std::uint64_t staged_revision) {
    {
        KPBR_SHARED_UNIQUE_LOCK(lock, mutex_);
        active_config_ = std::move(active_config);
        active_outbound_marks_ = std::move(outbound_marks);
        if (staged_config_.has_value() && staged_revision_ == staged_revision) {
            staged_config_.reset();
        }
    }
    notify_changed();
}

void ConfigStore::stage_config(Config staged_config) {
    {
        KPBR_SHARED_UNIQUE_LOCK(lock, mutex_);
        staged_config_ = std::move(staged_config);
        ++staged_revision_;
    }
    notify_changed();
}

std::optional<StagedConfigSnapshot> ConfigStore::staged_snapshot() const {
    KPBR_SHARED_LOCK(lock, mutex_);
    if (!staged_config_.has_value()) return std::nullopt;
    return StagedConfigSnapshot{*staged_config_, staged_revision_};
}

void ConfigStore::clear_staged() {
    bool cleared = false;
    {
        KPBR_SHARED_UNIQUE_LOCK(lock, mutex_);
        cleared = staged_config_.has_value();
        staged_config_.reset();
    }
    if (cleared) notify_changed();
}

void ConfigStore::clear_staged_if_revision(std::uint64_t revision) {
    bool cleared = false;
    {
        KPBR_SHARED_UNIQUE_LOCK(lock, mutex_);
        if (staged_config_.has_value() && staged_revision_ == revision) {
            staged_config_.reset();
            cleared = true;
        }
    }
    if (cleared) notify_changed();
}

std::uint64_t ConfigStore::revision() const {
    KPBR_SHARED_LOCK(lock, mutex_);
    return revision_;
}

void ConfigStore::touch() {
    notify_changed();
}

void ConfigStore::set_change_callback(ChangeCallback callback) {
    KPBR_SHARED_UNIQUE_LOCK(lock, mutex_);
    change_callback_ = std::move(callback);
}

void ConfigStore::notify_changed() {
    ChangeCallback callback;
    {
        KPBR_SHARED_UNIQUE_LOCK(lock, mutex_);
        ++revision_;
        callback = change_callback_;
    }
    if (callback) callback();
}

} // namespace keen_pbr3
