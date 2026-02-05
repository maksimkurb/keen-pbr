#include "list_manager.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace keen_pbr3 {

ListManager::ListManager(const std::map<std::string, ListConfig>& lists,
                         const std::filesystem::path& cache_dir)
    : list_configs_(lists), cache_dir_(cache_dir) {}

void ListManager::load() {
    std::filesystem::create_directories(cache_dir_);

    for (const auto& [name, config] : list_configs_) {
        loaded_lists_[name] = load_list(name, config);
    }
}

void ListManager::reload() {
    loaded_lists_.clear();
    load();
}

const ParsedList* ListManager::get(const std::string& name) const {
    auto it = loaded_lists_.find(name);
    if (it == loaded_lists_.end()) {
        return nullptr;
    }
    return &it->second;
}

const std::map<std::string, ParsedList>& ListManager::lists() const {
    return loaded_lists_;
}

ParsedList ListManager::load_list(const std::string& name,
                                  const ListConfig& config) {
    ParsedList merged;

    // 1. Download from URL if specified
    if (config.url.has_value() && !config.url->empty()) {
        std::string content = download_or_cache(name, *config.url);
        ParsedList parsed = ListParser::parse(content);
        merged.ips.insert(merged.ips.end(), parsed.ips.begin(), parsed.ips.end());
        merged.cidrs.insert(merged.cidrs.end(), parsed.cidrs.begin(), parsed.cidrs.end());
        merged.domains.insert(merged.domains.end(), parsed.domains.begin(), parsed.domains.end());
    }

    // 2. Read from local file if specified
    if (config.file.has_value() && !config.file->empty()) {
        std::string content = read_file(*config.file);
        ParsedList parsed = ListParser::parse(content);
        merged.ips.insert(merged.ips.end(), parsed.ips.begin(), parsed.ips.end());
        merged.cidrs.insert(merged.cidrs.end(), parsed.cidrs.begin(), parsed.cidrs.end());
        merged.domains.insert(merged.domains.end(), parsed.domains.begin(), parsed.domains.end());
    }

    // 3. Add inline ip_cidrs entries
    for (const auto& entry : config.ip_cidrs) {
        // Inline entries can be IPs or CIDRs; parse them to classify
        ParsedList parsed = ListParser::parse(entry);
        merged.ips.insert(merged.ips.end(), parsed.ips.begin(), parsed.ips.end());
        merged.cidrs.insert(merged.cidrs.end(), parsed.cidrs.begin(), parsed.cidrs.end());
    }

    // 4. Add inline domain entries
    for (const auto& domain : config.domains) {
        merged.domains.push_back(domain);
    }

    return merged;
}

std::string ListManager::download_or_cache(const std::string& name,
                                           const std::string& url) {
    std::filesystem::path cache_file = cache_dir_ / (name + ".txt");

    try {
        std::string content = http_client_.download(url);

        // Write to cache
        std::ofstream ofs(cache_file, std::ios::binary);
        if (ofs) {
            ofs << content;
        }

        return content;
    } catch (const std::exception&) {
        // Fall back to cached copy if available
        if (std::filesystem::exists(cache_file)) {
            return read_file(cache_file);
        }
        throw; // No cache available, propagate the error
    }
}

std::string ListManager::read_file(const std::filesystem::path& path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) {
        throw std::runtime_error("Failed to open file: " + path.string());
    }
    std::ostringstream ss;
    ss << ifs.rdbuf();
    return ss.str();
}

} // namespace keen_pbr3
