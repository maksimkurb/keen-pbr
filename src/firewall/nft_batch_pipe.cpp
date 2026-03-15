#include "nft_batch_pipe.hpp"

#include <string>

namespace keen_pbr3 {

NftBatchVisitor::NftBatchVisitor(nlohmann::json& buffer, const std::string& set_name,
                                 int32_t static_timeout)
    : buffer_(buffer), set_name_(set_name), static_timeout_(static_timeout) {}

void NftBatchVisitor::on_entry(EntryType type, std::string_view entry) {
    if (type == EntryType::Domain) {
        return; // Ignore domain entries
    }

    nlohmann::json elem = {
        {"elem", {{"val", std::string(entry)}}},
    };

    if (static_timeout_ >= 0) {
        elem["elem"]["timeout"] = static_timeout_;
    }

    buffer_.push_back(std::move(elem));
    ++count_;
}

void NftBatchVisitor::finish() {
    // No-op: buffer is owned externally, applied later by Firewall::apply()
}

} // namespace keen_pbr3
