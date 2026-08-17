#pragma once

#include "../config/config.hpp"
#include "../util/traced_mutex.hpp"

#include <optional>

namespace keen_pbr3 {

struct ActiveConfigSnapshot {
    Config config;
    OutboundMarkMap outbound_marks;
};

struct StagedConfigSnapshot {
    Config config;
    std::uint64_t revision{0};
};

class ConfigStore {
public:
    explicit ConfigStore(Config active_config = {});

    ActiveConfigSnapshot active_snapshot() const;
    Config active_config() const;
    OutboundMarkMap outbound_marks() const;
    Config visible_config() const;
    bool config_is_draft() const;

    void replace_active(Config active_config, OutboundMarkMap outbound_marks);
    void stage_config(Config staged_config);
    std::optional<StagedConfigSnapshot> staged_snapshot() const;
    void clear_staged();
    void clear_staged_if_revision(std::uint64_t revision);

private:
    mutable TracedSharedMutex mutex_;
    Config active_config_ GUARDED_BY(mutex_);
    OutboundMarkMap active_outbound_marks_ GUARDED_BY(mutex_);
    std::optional<Config> staged_config_ GUARDED_BY(mutex_);
    std::uint64_t staged_revision_ GUARDED_BY(mutex_){0};
};

} // namespace keen_pbr3
