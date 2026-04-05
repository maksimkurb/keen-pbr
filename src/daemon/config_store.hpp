#pragma once

#include "../config/config.hpp"
#include "../util/traced_mutex.hpp"

#include <optional>
#include <string>
#include <utility>

namespace keen_pbr3 {

struct ActiveConfigSnapshot {
    Config config;
    OutboundMarkMap outbound_marks;
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
    void stage_config(Config staged_config, std::string staged_config_json);
    std::optional<std::pair<Config, std::string>> staged_snapshot() const;
    void clear_staged();
    void clear_staged_if_matches(const std::string& staged_config_json);

private:
    mutable TracedSharedMutex mutex_;
    Config active_config_;
    OutboundMarkMap active_outbound_marks_;
    std::optional<Config> staged_config_;
    std::optional<std::string> staged_config_json_;
};

} // namespace keen_pbr3
