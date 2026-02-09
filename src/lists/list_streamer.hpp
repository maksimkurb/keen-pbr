#pragma once

#include "../cache/cache_manager.hpp"
#include "../config/config.hpp"
#include "list_entry_visitor.hpp"

#include <string>

namespace keen_pbr3 {

class ListStreamer {
public:
    explicit ListStreamer(const CacheManager& cache);

    // Stream all sources for a named list (cache file, local file, inline entries)
    // through the visitor. Calls visitor.on_list_complete(name) when done.
    void stream_list(const std::string& name, const ListConfig& config, ListEntryVisitor& visitor);

    // Stream only the cached file for a named list through the visitor.
    void stream_cache(const std::string& name, ListEntryVisitor& visitor);

private:
    // Open a file and stream its entries through the visitor.
    static void stream_file(const std::filesystem::path& path, ListEntryVisitor& visitor);

    const CacheManager& cache_;
};

} // namespace keen_pbr3
