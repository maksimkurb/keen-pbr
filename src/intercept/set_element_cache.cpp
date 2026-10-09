#include "set_element_cache.hpp"

#include <cstring>

namespace keen_pbr3 {

namespace {

std::size_t addr_len(uint8_t family) { return family == 6 ? 16 : 4; }

bool same_key(const std::array<uint8_t, 16>& a, const std::array<uint8_t, 16>& b, uint8_t family) {
    return std::memcmp(a.data(), b.data(), addr_len(family)) == 0;
}

} // namespace

SetElementCache::SetElementCache() : entries_(kSetCacheCapacity) {
    for (Entry& e : entries_) e = Entry{0, 0, {}, 0, 0};
}

std::size_t SetElementCache::hash(uint16_t slot, uint8_t family,
                                  const std::array<uint8_t, 16>& addr) {
    uint64_t h = 1469598103934665603ull;  // FNV-1a
    const auto mix = [&h](uint8_t byte) {
        h ^= byte;
        h *= 1099511628211ull;
    };
    mix(static_cast<uint8_t>(slot & 0xFF));
    mix(static_cast<uint8_t>(slot >> 8));
    mix(family);
    for (std::size_t i = 0; i < addr_len(family); ++i) mix(addr[i]);
    h ^= h >> 29;
    return static_cast<std::size_t>(h);
}

SetElementCache::Lookup SetElementCache::lookup(uint16_t slot, uint8_t family,
                                                const std::array<uint8_t, 16>& addr,
                                                int64_t now_ms) const {
    if (slot == kNoSlot || (family != 4 && family != 6)) return {};
    const std::size_t base = hash(slot, family, addr);
    std::lock_guard<std::mutex> lock(mutex_);
    for (std::size_t k = 0; k < kProbeWindow; ++k) {
        const Entry& e = entries_[(base + k) & (kSetCacheCapacity - 1)];
        if (e.family == 0) break;  // no deletion: an empty slot ends the chain
        if (e.slot != slot || e.family != family || !same_key(e.addr, addr, family)) continue;
        Lookup out;
        out.expires_at_ms = e.expires_at_ms;
        out.written_at_ms = e.written_at_ms;
        if (e.expires_at_ms != kPermanent && e.expires_at_ms <= now_ms + kExpiryMarginMs) {
            return {};  // expiring or expired: not cached (entry is overwritten on the next record)
        }
        out.state = now_ms - e.written_at_ms > kMaxTrustMs ? State::Stale : State::Fresh;
        return out;
    }
    return {};
}

SetElementCache::Lookup SetElementCache::lookup_evidence(
    uint16_t slot, uint8_t family, const std::array<uint8_t, 16>& addr,
    int64_t now_ms) const {
    if (slot == kNoSlot || (family != 4 && family != 6)) return {};
    const std::size_t base = hash(slot, family, addr);
    std::lock_guard<std::mutex> lock(mutex_);
    for (std::size_t k = 0; k < kProbeWindow; ++k) {
        const Entry& e = entries_[(base + k) & (kSetCacheCapacity - 1)];
        if (e.family == 0) break;
        if (e.slot != slot || e.family != family || !same_key(e.addr, addr, family)) continue;
        Lookup out;
        out.expires_at_ms = e.expires_at_ms;
        out.written_at_ms = e.written_at_ms;
        out.state = now_ms - e.written_at_ms > kMaxTrustMs ? State::Stale : State::Fresh;
        return out;
    }
    return {};
}

bool SetElementCache::record(uint16_t slot, uint8_t family, const std::array<uint8_t, 16>& addr,
                             uint32_t timeout_s, int64_t now_ms, uint64_t epoch) {
    if (slot == kNoSlot || (family != 4 && family != 6)) return false;
    const int64_t expires =
        timeout_s == 0 ? kPermanent : now_ms + static_cast<int64_t>(timeout_s) * 1000;
    const std::size_t base = hash(slot, family, addr);
    std::lock_guard<std::mutex> lock(mutex_);
    if (epoch != epoch_) return false;
    Entry* victim = nullptr;
    for (std::size_t k = 0; k < kProbeWindow; ++k) {
        Entry& e = entries_[(base + k) & (kSetCacheCapacity - 1)];
        if (e.family == 0) {
            victim = &e;
            ++count_;
            break;
        }
        if (e.slot == slot && e.family == family && same_key(e.addr, addr, family)) {
            victim = &e;
            break;
        }
        // Window full: the smallest expiry (expired entries first) goes.
        if (victim == nullptr || e.expires_at_ms < victim->expires_at_ms) victim = &e;
    }
    victim->expires_at_ms = expires;
    victim->written_at_ms = now_ms;
    victim->addr = addr;
    victim->slot = slot;
    victim->family = family;
    return true;
}

void SetElementCache::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (Entry& e : entries_) e.family = 0;
    count_ = 0;
    ++epoch_;
}

uint64_t SetElementCache::epoch() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return epoch_;
}

std::size_t SetElementCache::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return count_;
}

} // namespace keen_pbr3
