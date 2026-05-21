#include "list_streamer.hpp"
#include "../config/list_parser.hpp"

#include <fstream>
#include <stdexcept>

namespace keen_pbr3 {

ListStreamer::ListStreamer(const CacheManager& cache) : cache_(cache) {}

void ListStreamer::stream_list(const std::string& name, const ListConfig& config, ListEntryVisitor& visitor) {
    // The cached URL file is used only while the list still declares a URL
    // source; a stale cache for a removed URL is ignored here.
    const bool include_cache = config.url.has_value() && cache_.has_cache(name);
    stream_all_sources(name, config, visitor, include_cache);
}

void ListStreamer::stream_list_preferring_cache(const std::string& name,
                                                const ListConfig& config,
                                                ListEntryVisitor& visitor) {
    // Use the cached file whenever it exists, even if the URL source was
    // removed from the config. The local file and inline entries are always
    // streamed too — they must not be dropped just because a cache exists.
    stream_all_sources(name, config, visitor, cache_.has_cache(name));
}

void ListStreamer::stream_all_sources(const std::string& name,
                                      const ListConfig& config,
                                      ListEntryVisitor& visitor,
                                      bool include_cache) {
    // 1. Cached URL file
    if (include_cache) {
        stream_file(cache_.cache_path(name), visitor);
    }

    // 2. Local file (if configured)
    if (config.file.has_value()) {
        stream_file(config.file.value(), visitor);
    }

    // 3. Inline ip_cidrs via classify_entry()
    for (const auto& entry : config.ip_cidrs.value_or(std::vector<std::string>{})) {
        ListParser::classify_entry(entry, visitor);
    }

    // 4. Inline domains as Domain entries
    for (const auto& domain : config.domains.value_or(std::vector<std::string>{})) {
        visitor.on_entry(EntryType::Domain, domain);
    }

    // Signal that all sources for this list have been processed
    visitor.on_list_complete(name);
}

void ListStreamer::stream_cache(const std::string& name, ListEntryVisitor& visitor) {
    if (cache_.has_cache(name)) {
        stream_file(cache_.cache_path(name), visitor);
    }
}

void ListStreamer::stream_file(const std::filesystem::path& path, ListEntryVisitor& visitor) {
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        throw std::runtime_error("Failed to open file: " + path.string());
    }
    ListParser::stream_parse(ifs, visitor);
}

} // namespace keen_pbr3
