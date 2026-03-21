#include "list_set_usage.hpp"

#include "list_entry_visitor.hpp"
#include "list_streamer.hpp"

namespace keen_pbr3 {

ListSetUsage analyze_list_set_usage(const std::string& list_name,
                                    const ListConfig& config,
                                    ListStreamer& list_streamer) {
    EntryCounter counter;
    list_streamer.stream_list(list_name, config, counter);

    ListSetUsage usage;
    usage.has_static_entries = counter.ips() > 0 || counter.cidrs() > 0;
    usage.has_domain_entries = counter.domains() > 0;

    const int64_t ttl_ms = config.ttl_ms.value_or(0);
    if (ttl_ms >= 1000) {
        usage.dynamic_timeout = static_cast<uint32_t>(ttl_ms / 1000);
    }

    return usage;
}

} // namespace keen_pbr3
