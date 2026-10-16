#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <list>
#include <unordered_map>
#include <vector>

#include "../util/byte_view.hpp"

namespace keen_pbr3::l7 {

struct FlowKey {
    uint8_t family{0};  // 4 or 6
    std::array<uint8_t, 16> src{};
    std::array<uint8_t, 16> dst{};
    uint16_t sport{0};
    uint16_t dport{0};

    bool operator==(const FlowKey& other) const;
};

struct FlowKeyHash {
    std::size_t operator()(const FlowKey& key) const;
};

class FlowBuffers {
public:
    FlowBuffers(std::size_t max_flows = 512, std::size_t max_bytes_per_flow = 4096,
                std::chrono::milliseconds ttl = std::chrono::seconds(2));

    // Feeds one TCP segment payload with its sequence number.
    // The first segment seen for a key defines base seq (the flow is created on first payload-carrying segment).
    // Only data contiguous with the current end is appended; segments fully duplicate/old are ignored;
    // a gap (future segment) is ignored (dropped).
    // Returns the contiguous bytes buffered so far (view valid until next call that mutates this FlowBuffers).
    // Data beyond max_bytes_per_flow is truncated.
    ByteView feed(const FlowKey& key, uint32_t seq, ByteView payload, std::chrono::steady_clock::time_point now);

    void erase(const FlowKey& key);

    // Removes flows idle for longer than ttl. Called by feed() opportunistically too.
    void expire(std::chrono::steady_clock::time_point now);

    std::size_t size() const;

private:
    struct FlowState {
        std::vector<uint8_t> buffer;
        uint32_t base_seq{0};
        std::chrono::steady_clock::time_point last_seen;
        std::list<FlowKey>::iterator lru_pos;
    };

    std::size_t max_flows_;
    std::size_t max_bytes_per_flow_;
    std::chrono::milliseconds ttl_;
    std::unordered_map<FlowKey, FlowState, FlowKeyHash> flows_;
    std::list<FlowKey> lru_;  // least recently fed first
};

} // namespace keen_pbr3::l7
