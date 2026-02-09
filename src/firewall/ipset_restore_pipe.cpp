#include "ipset_restore_pipe.hpp"

#include <string>

namespace keen_pbr3 {

IpsetRestoreVisitor::IpsetRestoreVisitor(const std::string& set_name, int32_t static_timeout)
    : set_name_(set_name), static_timeout_(static_timeout) {
    pipe_ = popen("ipset restore -exist", "w");
    if (!pipe_) {
        throw FirewallError("Failed to open pipe to 'ipset restore -exist'");
    }
}

IpsetRestoreVisitor::~IpsetRestoreVisitor() {
    if (!finished_) {
        try {
            finish();
        } catch (...) {
            // Best-effort cleanup in destructor
        }
    }
}

void IpsetRestoreVisitor::on_entry(EntryType type, std::string_view entry) {
    if (type == EntryType::Domain) {
        return; // Ignore domain entries
    }

    std::string line = "add " + set_name_ + " " + std::string(entry);
    if (static_timeout_ >= 0) {
        line += " timeout " + std::to_string(static_timeout_);
    }
    line += "\n";

    if (std::fwrite(line.data(), 1, line.size(), pipe_) != line.size()) {
        throw FirewallError("Failed to write to ipset restore pipe");
    }
    ++count_;
}

void IpsetRestoreVisitor::finish() {
    if (finished_) {
        return;
    }
    finished_ = true;

    if (pipe_) {
        int status = pclose(pipe_);
        pipe_ = nullptr;
        if (status != 0) {
            throw FirewallError("ipset restore exited with status " + std::to_string(status));
        }
    }
}

} // namespace keen_pbr3
