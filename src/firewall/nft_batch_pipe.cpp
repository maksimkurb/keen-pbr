#include "nft_batch_pipe.hpp"

#include <string>

namespace keen_pbr3 {

NftBatchVisitor::NftBatchVisitor(std::ostringstream& buffer, const std::string& set_name,
                                 int32_t static_timeout)
    : buffer_(buffer), set_name_(set_name), static_timeout_(static_timeout) {}

void NftBatchVisitor::on_entry(EntryType type, std::string_view entry) {
    if (type == EntryType::Domain) {
        return; // Ignore domain entries
    }

    // Format: add element inet KeenPbrTable <setname> { <entry> [timeout Ns] }
    buffer_ << "add element inet KeenPbrTable " << set_name_ << " { " << entry;
    if (static_timeout_ >= 0) {
        buffer_ << " timeout " << static_timeout_ << "s";
    }
    buffer_ << " }\n";
    ++count_;
}

void NftBatchVisitor::finish() {
    // No-op: buffer is owned externally, applied later by Firewall::apply()
}

} // namespace keen_pbr3
