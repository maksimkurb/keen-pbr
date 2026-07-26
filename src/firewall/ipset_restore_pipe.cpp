#include "ipset_restore_pipe.hpp"

#include <string>

namespace keen_pbr3 {

IpsetRestoreVisitor::IpsetRestoreVisitor(std::ostringstream& buffer, const std::string& set_name)
    : buffer_(buffer), set_name_(set_name) {}

void IpsetRestoreVisitor::on_entry(EntryType type, std::string_view entry) {
    if (type == EntryType::Domain) {
        return; // Ignore domain entries
    }

    // hash:net deliberately rejects a zero prefix.  Preserve the meaning of
    // an all-addresses CIDR by expressing it as the two /1 networks.
    if (type == EntryType::Cidr && entry.size() >= 2 &&
        entry.substr(entry.size() - 2) == "/0") {
        if (entry.find(':') != std::string_view::npos) {
            buffer_ << "add " << set_name_ << " ::/1 -exist\n";
            buffer_ << "add " << set_name_ << " 8000::/1 -exist\n";
        } else {
            buffer_ << "add " << set_name_ << " 0.0.0.0/1 -exist\n";
            buffer_ << "add " << set_name_ << " 128.0.0.0/1 -exist\n";
        }
        count_ += 2;
        return;
    }

    buffer_ << "add " << set_name_ << " " << entry << " -exist\n";
    ++count_;
}

void IpsetRestoreVisitor::finish() {
    // No-op: buffer is owned externally, applied later by Firewall::apply()
}

} // namespace keen_pbr3
