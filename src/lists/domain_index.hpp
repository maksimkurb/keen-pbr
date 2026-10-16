#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace keen_pbr3 {

// In-memory domain → list matcher. Given a domain name (possibly with uppercase
// and trailing dot), efficiently returns which configured lists contain it via
// suffix matching at label boundaries.
//
// Example: list A has "example.com" and list B has "a.example.com":
//   lookup("www.example.com") returns {A} (matches via "example.com" suffix)
//   lookup("x.a.example.com") returns {A, B} (matches via both suffixes)
//   lookup("badexample.com") returns {} (no label boundary match)
class DomainIndex {
public:
    using ListId = uint16_t;   // index into list_names()

    class Builder {
    public:
        // Registers a list; returns its id. Calling twice with same name returns same id.
        ListId add_list(const std::string& name);

        // Adds a domain for list `id`. Lowercases ASCII; strips one leading "*." and one trailing '.';
        // ignores empty strings. Duplicates are fine.
        void add_domain(ListId id, std::string_view domain);

        // Consumes the builder.
        DomainIndex build() &&;

    private:
        std::vector<std::string> list_names_;
        // Temporary storage: map from list name to its id
        std::vector<std::pair<std::string, ListId>> name_to_id_;
        // Temporary storage: list of (hash, list_id) pairs
        std::vector<std::pair<uint64_t, ListId>> domain_list_pairs_;
    };

    // Returns list ids whose domains match `name` or any parent suffix at label boundaries.
    // `name` may have uppercase and a trailing dot; handle both without allocating (hash bytes
    // while lowercasing on the fly). Result is sorted ascending, unique. `out` is cleared first.
    void lookup(std::string_view name, std::vector<ListId>& out) const;

    bool empty() const;
    std::size_t domain_count() const;      // unique domains stored
    const std::vector<std::string>& list_names() const;
    std::size_t memory_bytes() const;      // approximate heap usage

private:
    // FNV-1a hash computation
    static uint64_t fnv1a_hash(std::string_view str);

    // Find a domain hash in the table; returns table index if found, npos if not.
    std::size_t find_hash(uint64_t hash) const;

    std::vector<uint64_t> hashes_;               // parallel vector: hash values
    std::vector<uint16_t> groups_;               // parallel vector: group indices (into groups_table_)
    std::vector<std::vector<ListId>> groups_table_;  // groups: each entry is sorted, unique list of ids
    std::vector<std::string> list_names_;        // list names, indexed by ListId
    std::size_t domain_count_;                   // count of unique domains stored
};

}  // namespace keen_pbr3
