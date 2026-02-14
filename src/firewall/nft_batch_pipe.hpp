#pragma once

#include "../lists/list_entry_visitor.hpp"
#include "firewall.hpp"

#include <cstddef>
#include <sstream>
#include <string>

namespace keen_pbr3 {

// Visitor that buffers nftables element additions into an ostringstream
// for later atomic application via 'nft -f -'.
class NftBatchVisitor : public ListEntryVisitor {
public:
    // buffer: reference to the ostringstream to append element lines to
    // set_name: target nft set name (within table inet KeenPbrTable)
    // static_timeout: per-entry timeout in seconds (-1 = use set default, 0 = permanent)
    explicit NftBatchVisitor(std::ostringstream& buffer, const std::string& set_name,
                             int32_t static_timeout = -1);

    // Appends 'add element inet KeenPbrTable <setname> { <entry> [timeout Ns] }\n'
    // for Ip and Cidr types. Domain entries are ignored.
    void on_entry(EntryType type, std::string_view entry) override;

    // No-op (buffer is owned externally, applied later by Firewall::apply())
    void finish() override;

    // Returns number of entries written to the buffer.
    size_t count() const { return count_; }

    // Non-copyable (holds a reference)
    NftBatchVisitor(const NftBatchVisitor&) = delete;
    NftBatchVisitor& operator=(const NftBatchVisitor&) = delete;

private:
    std::ostringstream& buffer_;
    std::string set_name_;
    int32_t static_timeout_;
    size_t count_ = 0;
};

} // namespace keen_pbr3
