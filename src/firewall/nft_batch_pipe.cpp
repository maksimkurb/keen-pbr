#include "nft_batch_pipe.hpp"

#include <charconv>
#include <stdexcept>
#include <string>

namespace keen_pbr3 {

static nlohmann::json cidr_to_nft_prefix_json(std::string_view cidr) {
    const size_t slash = cidr.find('/');
    if (slash == std::string_view::npos || slash == 0 || slash + 1 >= cidr.size()) {
        throw std::invalid_argument("Invalid CIDR for nft prefix encoding: " + std::string(cidr));
    }

    int prefix_len = -1;
    const std::string_view prefix_part = cidr.substr(slash + 1);
    const auto [ptr, ec] = std::from_chars(
        prefix_part.data(), prefix_part.data() + prefix_part.size(), prefix_len);
    if (ec != std::errc{} || ptr != prefix_part.data() + prefix_part.size()) {
        throw std::invalid_argument("Invalid prefix length for nft prefix encoding: " +
                                    std::string(cidr));
    }

    return {
        {"prefix", {
            {"addr", std::string(cidr.substr(0, slash))},
            {"len", prefix_len},
        }},
    };
}

NftBatchVisitor::NftBatchVisitor(nlohmann::json& buffer, const std::string& set_name)
    : buffer_(buffer), set_name_(set_name) {}

void NftBatchVisitor::on_entry(EntryType type, std::string_view entry) {
    if (type == EntryType::Domain) {
        return; // Ignore domain entries
    }

    if (type == EntryType::Cidr) {
        buffer_.push_back(cidr_to_nft_prefix_json(entry));
    } else {
        buffer_.push_back(std::string(entry));
    }
    ++count_;
}

void NftBatchVisitor::finish() {
    // No-op: buffer is owned externally, applied later by Firewall::apply()
}

} // namespace keen_pbr3
