#include "flow_buffer.hpp"

#include <algorithm>
#include <functional>

namespace keen_pbr3::l7 {

bool FlowKey::operator==(const FlowKey& other) const {
    return family == other.family && src == other.src && dst == other.dst && sport == other.sport &&
           dport == other.dport;
}

std::size_t FlowKeyHash::operator()(const FlowKey& key) const {
    std::hash<uint8_t> hash_u8;
    std::hash<uint16_t> hash_u16;

    std::size_t h = hash_u8(key.family);
    for (std::size_t i = 0; i < key.src.size(); ++i) {
        h ^= hash_u8(key.src[i]) + 0x9e3779b9 + (h << 6) + (h >> 2);
    }
    for (std::size_t i = 0; i < key.dst.size(); ++i) {
        h ^= hash_u8(key.dst[i]) + 0x9e3779b9 + (h << 6) + (h >> 2);
    }
    h ^= hash_u16(key.sport) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= hash_u16(key.dport) + 0x9e3779b9 + (h << 6) + (h >> 2);

    return h;
}

FlowBuffers::FlowBuffers(std::size_t max_flows, std::size_t max_bytes_per_flow, std::chrono::milliseconds ttl)
    : max_flows_(max_flows == 0 ? 1 : max_flows), max_bytes_per_flow_(max_bytes_per_flow), ttl_(ttl) {}

ByteView FlowBuffers::feed(const FlowKey& key, uint32_t seq, ByteView payload,
                           std::chrono::steady_clock::time_point now) {
    expire(now);

    auto it = flows_.find(key);
    if (it == flows_.end()) {
        if (flows_.size() >= max_flows_) {
            flows_.erase(lru_.front());
            lru_.pop_front();
        }
        it = flows_.emplace(key, FlowState{}).first;
        FlowState& state = it->second;
        state.base_seq = seq;
        state.lru_pos = lru_.insert(lru_.end(), key);
        const std::size_t n = std::min(payload.size(), max_bytes_per_flow_);
        state.buffer.assign(payload.data(), payload.data() + n);
    } else {
        FlowState& state = it->second;
        const uint32_t end_seq = state.base_seq + static_cast<uint32_t>(state.buffer.size());
        // Signed distance keeps the comparison correct across 32-bit wraparound.
        const auto delta = static_cast<int32_t>(seq - end_seq);
        std::size_t skip = 0;
        bool append = false;
        if (delta == 0) {
            append = true;
        } else if (delta < 0) {
            const auto overlap = static_cast<std::size_t>(-static_cast<int64_t>(delta));
            if (overlap < payload.size()) {
                skip = overlap;
                append = true;
            }
        }
        // delta > 0 is a gap: the segment is dropped, later data cannot be contiguous.
        if (append && state.buffer.size() < max_bytes_per_flow_) {
            const std::size_t n = std::min(payload.size() - skip, max_bytes_per_flow_ - state.buffer.size());
            state.buffer.insert(state.buffer.end(), payload.data() + skip, payload.data() + skip + n);
        }
        lru_.splice(lru_.end(), lru_, state.lru_pos);
    }

    FlowState& state = it->second;
    state.last_seen = now;
    return ByteView(state.buffer.data(), state.buffer.size());
}

void FlowBuffers::erase(const FlowKey& key) {
    auto it = flows_.find(key);
    if (it == flows_.end()) {
        return;
    }
    lru_.erase(it->second.lru_pos);
    flows_.erase(it);
}

void FlowBuffers::expire(std::chrono::steady_clock::time_point now) {
    while (!lru_.empty()) {
        auto it = flows_.find(lru_.front());
        if (now - it->second.last_seen <= ttl_) {
            break;
        }
        flows_.erase(it);
        lru_.pop_front();
    }
}

std::size_t FlowBuffers::size() const {
    return flows_.size();
}

}  // namespace keen_pbr3::l7
