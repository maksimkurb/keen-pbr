#pragma once

#include "../config/config.hpp"
#include "../util/traced_mutex.hpp"

#include <cstdint>
#include <functional>
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
    void replace_active_and_clear_staged_if_revision(
        Config active_config,
        OutboundMarkMap outbound_marks,
        std::uint64_t staged_revision);
    void stage_config(Config staged_config);
    std::optional<StagedConfigSnapshot> staged_snapshot() const;
    void clear_staged();
    void clear_staged_if_revision(std::uint64_t revision);

    // Monotonic within the process, seeded from the clock so that a restarted
    // daemon does not repeat a value a client has already seen. Bumped on every
    // change of the active config, the staged draft, or (via touch()) of
    // state shown next to them such as the list refresh state.
    std::uint64_t revision() const;
    void touch();

    // Called after each bump, with no store lock held. Set before concurrent
    // use of the store starts.
    using ChangeCallback = std::function<void()>;
    void set_change_callback(ChangeCallback callback);

private:
    mutable TracedSharedMutex mutex_;
    Config active_config_ GUARDED_BY(mutex_);
    OutboundMarkMap active_outbound_marks_ GUARDED_BY(mutex_);
    std::optional<Config> staged_config_ GUARDED_BY(mutex_);
    std::uint64_t staged_revision_ GUARDED_BY(mutex_){0};
    std::uint64_t revision_ GUARDED_BY(mutex_){0};
    ChangeCallback change_callback_ GUARDED_BY(mutex_);

    // Bumps the revision and runs the callback; the lock must not be held.
    void notify_changed();
};

} // namespace keen_pbr3
