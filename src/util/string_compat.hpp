#pragma once

#include <map>
#include <set>
#include <string_view>

namespace keen_pbr3 {

inline bool has_prefix(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() &&
           value.compare(0, prefix.size(), prefix) == 0;
}

template<typename T, typename Compare, typename Allocator>
bool contains(const std::set<T, Compare, Allocator>& set, const T& value) {
    return set.find(value) != set.end();
}

template<typename Key, typename Value, typename Compare, typename Allocator>
bool contains(const std::map<Key, Value, Compare, Allocator>& map, const Key& key) {
    return map.find(key) != map.end();
}

} // namespace keen_pbr3
