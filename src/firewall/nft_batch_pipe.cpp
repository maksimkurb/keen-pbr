#include "nft_batch_pipe.hpp"

#include <string>

namespace keen_pbr3 {

NftBatchVisitor::NftBatchVisitor(const std::string& set_name, int32_t static_timeout)
    : set_name_(set_name), static_timeout_(static_timeout) {
    pipe_ = popen("nft -f -", "w");
    if (!pipe_) {
        throw FirewallError("Failed to open pipe to 'nft -f -'");
    }
}

NftBatchVisitor::~NftBatchVisitor() {
    if (!finished_) {
        try {
            finish();
        } catch (...) {
            // Best-effort cleanup in destructor
        }
    }
}

void NftBatchVisitor::on_entry(EntryType type, std::string_view entry) {
    if (type == EntryType::Domain) {
        return; // Ignore domain entries
    }

    // Format: add element inet keen_pbr3 <setname> { <entry> [timeout Ns] }
    std::string line = "add element inet keen_pbr3 " + set_name_ + " { " + std::string(entry);
    if (static_timeout_ >= 0) {
        line += " timeout " + std::to_string(static_timeout_) + "s";
    }
    line += " }\n";

    if (std::fwrite(line.data(), 1, line.size(), pipe_) != line.size()) {
        throw FirewallError("Failed to write to nft batch pipe");
    }
    ++count_;
}

void NftBatchVisitor::finish() {
    if (finished_) {
        return;
    }
    finished_ = true;

    if (pipe_) {
        int status = pclose(pipe_);
        pipe_ = nullptr;
        if (status != 0) {
            throw FirewallError("nft batch load exited with status " + std::to_string(status));
        }
    }
}

} // namespace keen_pbr3
