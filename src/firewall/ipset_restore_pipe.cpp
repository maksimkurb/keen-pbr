#include "ipset_restore_pipe.hpp"

#include <string>

namespace keen_pbr3 {

IpsetRestoreVisitor::IpsetRestoreVisitor(std::ostringstream& buffer, const std::string& set_name,
                                         int32_t static_timeout)
    : buffer_(buffer), set_name_(set_name), static_timeout_(static_timeout) {}

void IpsetRestoreVisitor::on_entry(EntryType type, std::string_view entry) {
    if (type == EntryType::Domain) {
        return; // Ignore domain entries
    }

    buffer_ << "add " << set_name_ << " " << entry;
    if (static_timeout_ >= 0) {
        buffer_ << " timeout " << static_timeout_;
    }
    buffer_ << "\n";
    ++count_;
}

void IpsetRestoreVisitor::finish() {
    // No-op: buffer is owned externally, applied later by Firewall::apply()
}

} // namespace keen_pbr3
