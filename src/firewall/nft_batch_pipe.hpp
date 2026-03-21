#pragma once

#include "../lists/list_entry_visitor.hpp"
#include "firewall.hpp"

#include <cstddef>
#include <nlohmann/json.hpp>
#include <string>

namespace keen_pbr3 {

// Visitor that buffers nftables element additions into a JSON array
// for later atomic application via 'nft -j -f -'.
class NftBatchVisitor : public ListEntryVisitor {
public:
    // buffer: reference to the JSON array to append elements to
    // set_name: target nft set name (within table inet KeenPbrTable)
    explicit NftBatchVisitor(nlohmann::json& buffer, const std::string& set_name);

    // Appends element values for Ip and Cidr types. Domain entries are ignored.
    void on_entry(EntryType type, std::string_view entry) override;

    // No-op (buffer is owned externally, applied later by Firewall::apply())
    void finish() override;

    // Returns number of entries written to the buffer.
    size_t count() const { return count_; }

    // Non-copyable (holds a reference)
    NftBatchVisitor(const NftBatchVisitor&) = delete;
    NftBatchVisitor& operator=(const NftBatchVisitor&) = delete;

private:
    nlohmann::json& buffer_;
    std::string set_name_;
    size_t count_ = 0;
};

} // namespace keen_pbr3
