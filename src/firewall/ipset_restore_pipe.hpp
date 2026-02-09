#pragma once

#include "../lists/list_entry_visitor.hpp"
#include "firewall.hpp"

#include <cstddef>
#include <cstdio>
#include <string>

namespace keen_pbr3 {

// Visitor that pipes IP/CIDR entries to 'ipset restore -exist' via popen
// for batch loading without one system() call per entry.
class IpsetRestoreVisitor : public ListEntryVisitor {
public:
    // Opens popen("ipset restore -exist", "w").
    // set_name: target ipset name
    // static_timeout: per-entry timeout in seconds (-1 = use set default, 0 = permanent)
    explicit IpsetRestoreVisitor(const std::string& set_name, int32_t static_timeout = -1);
    ~IpsetRestoreVisitor() override;

    // Writes 'add <setname> <entry> [timeout N]\n' for Ip and Cidr types.
    // Domain entries are ignored.
    void on_entry(EntryType type, std::string_view entry) override;

    // Calls pclose() and throws FirewallError if exit code is non-zero.
    void finish() override;

    // Returns number of entries written to the pipe.
    size_t count() const { return count_; }

    // Non-copyable, non-movable (owns a FILE*)
    IpsetRestoreVisitor(const IpsetRestoreVisitor&) = delete;
    IpsetRestoreVisitor& operator=(const IpsetRestoreVisitor&) = delete;

private:
    std::string set_name_;
    int32_t static_timeout_;
    FILE* pipe_ = nullptr;
    size_t count_ = 0;
    bool finished_ = false;
};

} // namespace keen_pbr3
