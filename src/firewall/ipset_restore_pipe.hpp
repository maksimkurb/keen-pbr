#pragma once

#include "../lists/list_entry_visitor.hpp"
#include "firewall.hpp"

#include <cstddef>
#include <sstream>
#include <string>

namespace keen_pbr3 {

// Visitor that buffers ipset add commands into an ostringstream
// for later atomic application via 'ipset restore -exist'.
class IpsetRestoreVisitor : public ListEntryVisitor {
public:
    // buffer: reference to the ostringstream to append add lines to
    // set_name: target ipset name
    // static_timeout: per-entry timeout in seconds (-1 = use set default, 0 = permanent)
    explicit IpsetRestoreVisitor(std::ostringstream& buffer, const std::string& set_name,
                                 int32_t static_timeout = -1);

    // Appends 'add <setname> <entry> [timeout N]\n' for Ip and Cidr types.
    // Domain entries are ignored.
    void on_entry(EntryType type, std::string_view entry) override;

    // No-op (buffer is owned externally, applied later by Firewall::apply())
    void finish() override;

    // Returns number of entries written to the buffer.
    size_t count() const { return count_; }

    // Non-copyable (holds a reference)
    IpsetRestoreVisitor(const IpsetRestoreVisitor&) = delete;
    IpsetRestoreVisitor& operator=(const IpsetRestoreVisitor&) = delete;

private:
    std::ostringstream& buffer_;
    std::string set_name_;
    int32_t static_timeout_;
    size_t count_ = 0;
};

} // namespace keen_pbr3
