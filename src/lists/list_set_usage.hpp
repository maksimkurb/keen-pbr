#pragma once

#include "../config/config.hpp"

#include <cstdint>
#include <string>

namespace keen_pbr3 {

class ListStreamer;

struct ListSetUsage {
    bool has_static_entries{false};
    bool has_domain_entries{false};
    uint32_t dynamic_timeout{0};
};

// Analyze a list's fully streamed content to determine which firewall sets are needed.
ListSetUsage analyze_list_set_usage(const std::string& list_name,
                                    const ListConfig& config,
                                    ListStreamer& list_streamer);

} // namespace keen_pbr3
