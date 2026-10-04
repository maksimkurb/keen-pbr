#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace keen_pbr3 {

// Remembers which (set, address) elements the daemon itself wrote recently and
// when they expire, so a DNS verdict never has to wait for an address that is
// already routed.  The cache is advisory: a miss only costs the old, slower
// path; a wrong hit would skip a needed write, so every doubtful condition
// (expiry within the margin, old entry, set recreated) reports "not cached".
//
// Fixed-capacity open-addressing table (linear probing, 16-slot probe window).
// No allocation after construction.  Memory: kSetCacheCapacity * sizeof(Entry)
// = 16384 * 40 B = 640 KiB.  Thread-safe (one mutex, no I/O under the lock):
// used by the DNS hot thread and the L7 worker thread.
class SetElementCache {
public:
    // Fixed capacity (power of two).
    static constexpr std::size_t kSetCacheCapacity = 16384;
    static constexpr std::size_t kProbeWindow = 16;
    // An element expiring within this margin is reported as not cached.
    static constexpr int64_t kExpiryMarginMs = 2000;
    // A cached entry older than this is not trusted (reported Stale).
    static constexpr int64_t kMaxTrustMs = 300000;
    static constexpr int64_t kPermanent = INT64_MAX;
    static constexpr uint16_t kNoSlot = 0xFFFF;

    enum class State : uint8_t {
        Unknown,  // not cached / expiring soon: must be written before the verdict
        Fresh,    // present, trusted, not expiring within the margin
        Stale,    // present per cache but the trust age was exceeded
    };

    struct Lookup {
        State state{State::Unknown};
        int64_t expires_at_ms{0};  // kPermanent for timeout 0
        int64_t written_at_ms{0};
    };

    SetElementCache();

    // `addr` holds 4 (family 4) or 16 (family 6) significant bytes.
    Lookup lookup(uint16_t slot, uint8_t family, const std::array<uint8_t, 16>& addr,
                  int64_t now_ms) const;

    // Records a successful write at `now_ms` (taken BEFORE the write so the
    // expiry is never over-estimated).  Ignored when `epoch` is not the current
    // epoch (the cache was cleared after the write started) or slot == kNoSlot.
    // Returns true when recorded.
    bool record(uint16_t slot, uint8_t family, const std::array<uint8_t, 16>& addr,
                uint32_t timeout_s, int64_t now_ms, uint64_t epoch);

    // Drops everything and starts a new epoch.
    void clear();
    // Epoch to capture before a write begins; see record().
    uint64_t epoch() const;
    std::size_t size() const;

    // Decides whether a cached Fresh element needs a post-verdict refresh.
    // Returns true if: remaining lifetime < half of the desired timeout,
    // OR desired_timeout is permanent (0) while cached is finite.
    // Stale entries (outside the trust age) are always refreshed by the caller.
    // remaining_ms = cached.expires_at - now (in milliseconds).
    static bool needs_refresh(int64_t remaining_ms, uint32_t desired_timeout_s,
                              bool cached_permanent, bool desired_permanent) {
        // If the new timeout is permanent but cached is finite, refresh.
        if (desired_permanent && !cached_permanent) return true;
        // If cached is already permanent and desired is finite, no refresh needed.
        if (cached_permanent) return false;
        // Both finite: refresh if less than half of the desired timeout remains.
        const int64_t half_timeout_ms = static_cast<int64_t>(desired_timeout_s) * 500;
        return remaining_ms < half_timeout_ms;
    }

private:
    struct Entry {
        int64_t expires_at_ms;
        int64_t written_at_ms;
        std::array<uint8_t, 16> addr;
        uint16_t slot;
        uint8_t family;  // 0 = empty slot
    };

    static std::size_t hash(uint16_t slot, uint8_t family, const std::array<uint8_t, 16>& addr);

    mutable std::mutex mutex_;
    std::vector<Entry> entries_;
    std::size_t count_{0};
    uint64_t epoch_{1};
};

} // namespace keen_pbr3
