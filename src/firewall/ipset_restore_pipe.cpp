#include "ipset_restore_pipe.hpp"

#include <string>

namespace keen_pbr3 {

IpsetRestoreVisitor::IpsetRestoreVisitor(std::ostringstream& buffer, const std::string& set_name)
    : buffer_(buffer), set_name_(set_name) {}

void IpsetRestoreVisitor::on_entry(EntryType type, std::string_view entry) {
    if (type == EntryType::Domain) {
        return; // Ignore domain entries
    }

    buffer_ << "add " << set_name_ << " " << entry;
    buffer_ << "\n";
    ++count_;
}

void IpsetRestoreVisitor::finish() {
    // No-op: buffer is owned externally, applied later by Firewall::apply()
}

} // namespace keen_pbr3
