#include "domain_index.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <stdexcept>

namespace keen_pbr3 {

namespace {

constexpr uint64_t kFnvOffsetBasis = 0xcbf29ce484222325ULL;
constexpr uint64_t kFnvPrime = 0x100000001b3ULL;

inline uint8_t to_lower(char c) {
    const auto u = static_cast<uint8_t>(c);
    return (u >= 'A' && u <= 'Z') ? static_cast<uint8_t>(u + ('a' - 'A')) : u;
}

std::string_view strip_domain(std::string_view domain) {
    if (domain.size() >= 2 && domain[0] == '*' && domain[1] == '.') {
        domain.remove_prefix(2);
    }
    if (!domain.empty() && domain.back() == '.') {
        domain.remove_suffix(1);
    }
    return domain;
}

// Maps a hash onto [0, capacity) without requiring a power-of-two table, so the
// table can stay close to the 0.7 load factor (Lemire's fast range reduction).
inline std::size_t slot_for(uint64_t hash, std::size_t capacity) {
    return static_cast<std::size_t>(((hash >> 32) * static_cast<uint64_t>(capacity)) >> 32);
}

} // namespace

// 64-bit FNV-1a over lowercase bytes. Collisions between distinct domains are
// accepted: at 64 bits they are negligible for list sizes seen on routers.
uint64_t DomainIndex::fnv1a_hash(std::string_view str) {
    uint64_t hash = kFnvOffsetBasis;
    for (char c : str) {
        hash ^= to_lower(c);
        hash *= kFnvPrime;
    }
    return hash == 0 ? 1 : hash; // 0 marks an empty slot
}

std::size_t DomainIndex::find_hash(uint64_t hash) const {
    const std::size_t capacity = hashes_.size();
    if (capacity == 0) {
        return std::string::npos;
    }
    std::size_t index = slot_for(hash, capacity);
    for (std::size_t i = 0; i < capacity; ++i) {
        if (hashes_[index] == hash) {
            return index;
        }
        if (hashes_[index] == 0) {
            return std::string::npos;
        }
        if (++index == capacity) {
            index = 0;
        }
    }
    return std::string::npos;
}

DomainIndex::ListId DomainIndex::Builder::add_list(const std::string& name) {
    for (const auto& [existing_name, id] : name_to_id_) {
        if (existing_name == name) {
            return id;
        }
    }
    if (list_names_.size() >= std::numeric_limits<ListId>::max()) {
        throw std::length_error("DomainIndex: too many lists");
    }
    const auto id = static_cast<ListId>(list_names_.size());
    list_names_.push_back(name);
    name_to_id_.emplace_back(name, id);
    return id;
}

void DomainIndex::Builder::add_domain(ListId id, std::string_view domain) {
    domain = strip_domain(domain);
    if (domain.empty()) {
        return;
    }
    domain_list_pairs_.emplace_back(DomainIndex::fnv1a_hash(domain), id);
}

DomainIndex DomainIndex::Builder::build() && {
    DomainIndex index;
    index.list_names_ = std::move(list_names_);
    index.domain_count_ = 0;

    auto& pairs = domain_list_pairs_;
    std::sort(pairs.begin(), pairs.end());
    pairs.erase(std::unique(pairs.begin(), pairs.end()), pairs.end());
    if (pairs.empty()) {
        return index;
    }

    // Count unique hashes: pairs are sorted by (hash, list), so each run of
    // equal hashes carries that domain's sorted, unique list ids.
    std::size_t unique = 0;
    for (std::size_t i = 0; i < pairs.size(); ++i) {
        if (i == 0 || pairs[i].first != pairs[i - 1].first) {
            ++unique;
        }
    }
    index.domain_count_ = unique;

    const std::size_t capacity = std::max<std::size_t>(16, (unique * 10 + 6) / 7);
    index.hashes_.assign(capacity, 0);
    index.groups_.assign(capacity, 0);

    std::map<std::vector<ListId>, uint16_t> group_map;
    std::vector<ListId> run;
    for (std::size_t i = 0; i < pairs.size();) {
        const uint64_t hash = pairs[i].first;
        run.clear();
        for (; i < pairs.size() && pairs[i].first == hash; ++i) {
            run.push_back(pairs[i].second);
        }

        auto it = group_map.find(run);
        if (it == group_map.end()) {
            if (index.groups_table_.size() >= std::numeric_limits<uint16_t>::max()) {
                throw std::length_error("DomainIndex: too many distinct list groups");
            }
            const auto group_id = static_cast<uint16_t>(index.groups_table_.size());
            index.groups_table_.push_back(run);
            it = group_map.emplace(run, group_id).first;
        }

        std::size_t slot = slot_for(hash, capacity);
        while (index.hashes_[slot] != 0) {
            if (++slot == capacity) {
                slot = 0;
            }
        }
        index.hashes_[slot] = hash;
        index.groups_[slot] = it->second;
    }

    std::vector<std::pair<uint64_t, ListId>>().swap(pairs);
    index.groups_table_.shrink_to_fit();
    return index;
}

void DomainIndex::lookup(std::string_view name, std::vector<ListId>& out) const {
    out.clear();
    if (empty()) {
        return;
    }
    if (!name.empty() && name.back() == '.') {
        name.remove_suffix(1);
    }

    // Walk suffixes at label boundaries: "a.b.c", "b.c", "c".
    std::size_t pos = 0;
    while (pos < name.size()) {
        const std::size_t idx = find_hash(fnv1a_hash(name.substr(pos)));
        if (idx != std::string::npos) {
            const auto& lists = groups_table_[groups_[idx]];
            out.insert(out.end(), lists.begin(), lists.end());
        }
        const std::size_t dot = name.find('.', pos);
        if (dot == std::string_view::npos) {
            break;
        }
        pos = dot + 1;
    }

    if (out.size() > 1) {
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
    }
}

bool DomainIndex::empty() const {
    return domain_count_ == 0;
}

std::size_t DomainIndex::domain_count() const {
    return domain_count_;
}

const std::vector<std::string>& DomainIndex::list_names() const {
    return list_names_;
}

std::size_t DomainIndex::memory_bytes() const {
    std::size_t bytes = 0;

    // hashes_ vector
    bytes += hashes_.capacity() * sizeof(uint64_t);

    // groups_ vector
    bytes += groups_.capacity() * sizeof(uint16_t);

    // groups_table_ vectors
    for (const auto& group : groups_table_) {
        bytes += group.capacity() * sizeof(ListId);
    }
    bytes += groups_table_.capacity() * sizeof(std::vector<ListId>);

    // list_names_ strings
    for (const auto& name : list_names_) {
        bytes += name.capacity();
    }
    bytes += list_names_.capacity() * sizeof(std::string);

    return bytes;
}

}  // namespace keen_pbr3
