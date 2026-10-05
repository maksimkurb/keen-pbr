#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <thread>
#include <type_traits>
#include <vector>

namespace keen_pbr3 {

// Fixed-capacity ring of trivially copyable records for one reader and
// producers that almost never overlap (each push is a few dozen loads/stores).
// Nothing is allocated after construction and no mutex is taken.
//
// Memory model.  Each slot is a seqlock: `stamp` is a version counter (odd =
// being written).  The payload is stored as an array of std::atomic<uint64_t>
// words that are only ever accessed with relaxed loads/stores, so a reader that
// races with a writer is not a data race (no UB, ThreadSanitizer-clean), and
// the torn copy it may read is discarded by re-validating the stamp:
//
//   writer:  CAS stamp even -> odd (acq_rel)      claims the slot
//            fence(release)                       odd stamp is visible before any word
//            relaxed word stores
//            stamp.store(even + 2, release)       words are visible before the new stamp
//   reader:  s1 = stamp.load(acquire)             pairs with the writer's release store
//            relaxed word loads
//            fence(acquire)                       word loads are not moved after the re-read
//            s2 = stamp.load(relaxed); valid iff s1 == s2 and s1 is even
//
// (the fence form is Boehm's "Can seqlocks get along with programming language
// memory models?"; the stamp grows on every write, in-place updates included,
// so a copy that overlapped any write is always detected.)
//
// `Rec::seq` (first member) is the event number, assigned from a global counter
// by push().  A record is addressed by `seq & (N - 1)`; the slot's own `seq`
// tells whether it still holds that event (older: not written yet, newer: it
// was overwritten).  push() waits for a concurrent writer of the same slot
// and never drops a claimed sequence number, so a reader that finds an
// unwritten slot may simply retry later.
template <class Rec, std::size_t N>
class SeqlockRing {
    static_assert(N != 0 && (N & (N - 1)) == 0, "capacity must be a power of two");
    static_assert(std::is_trivially_copyable_v<Rec>, "records are copied word by word");
    static_assert(sizeof(Rec) % sizeof(uint64_t) == 0 && alignof(Rec) <= alignof(uint64_t),
                  "record must be a whole number of 64-bit words");
    static_assert(std::is_standard_layout_v<Rec> && offsetof(Rec, seq) == 0,
                  "Rec::seq must be the first member");
    static_assert(std::atomic<uint64_t>::is_always_lock_free);
    static constexpr std::size_t kWords = sizeof(Rec) / sizeof(uint64_t);

public:
    enum class Peek { Ok, Pending, Lost };

    SeqlockRing() : slots_(new Slot[N]) {}

    // Claims the next sequence number, stores `rec` there (rec.seq is set) and
    // returns the number.  Never blocks beyond a concurrent writer of the same
    // slot (producers N events apart; practically never).
    uint64_t push(Rec& rec) {
        const uint64_t seq = next_.fetch_add(1, std::memory_order_relaxed);
        rec.seq = seq;
        Slot& slot = slots_[seq & (N - 1)];
        const uint64_t version = lock(slot);
        if (load_seq(slot) > seq) {
            // A newer event already took the slot (we were N events behind):
            // ours is lost, which readers report as a gap.
            slot.stamp.store(version, std::memory_order_release);
            return seq;
        }
        store(slot, rec);
        slot.stamp.store(version + 2, std::memory_order_release);
        return seq;
    }

    // Rewrites the record `seq` in place when it is still in the ring.
    // `mutate(Rec&)` returns whether it changed the record.
    template <class F>
    bool update(uint64_t seq, F&& mutate) {
        Slot& slot = slots_[seq & (N - 1)];
        const uint64_t version = lock(slot);
        if (load_seq(slot) != seq) {
            slot.stamp.store(version, std::memory_order_release);
            return false;
        }
        Rec rec;
        load(slot, rec);  // we own the slot: nobody else writes it
        if (!mutate(rec)) {
            slot.stamp.store(version, std::memory_order_release);  // unchanged payload
            return true;
        }
        store(slot, rec);
        slot.stamp.store(version + 2, std::memory_order_release);
        return true;
    }

    // Copies record `seq` if it is complete and still in the ring.
    //   Pending: not written yet (or being written): try again later.
    //   Lost:    overwritten by a newer event.
    Peek peek(uint64_t seq, Rec& out) const {
        const Slot& slot = slots_[seq & (N - 1)];
        for (int attempt = 0; attempt < 4; ++attempt) {
            const uint64_t before = slot.stamp.load(std::memory_order_acquire);
            if (before & 1) continue;  // writer inside: retry
            load(slot, out);
            std::atomic_thread_fence(std::memory_order_acquire);
            if (slot.stamp.load(std::memory_order_relaxed) != before) continue;  // torn
            if (out.seq > seq) return Peek::Lost;
            return out.seq == seq ? Peek::Ok : Peek::Pending;
        }
        return Peek::Pending;
    }

    // Appends records with seq > after_seq (oldest first, at most `max`).  A
    // run overwritten before it could be read is skipped at the front only: once
    // a record was returned, the first gap ends the batch so the caller's next
    // call (from the last returned seq) reports it as a leading gap.
    void read(uint64_t after_seq, std::size_t max, std::vector<Rec>& out) const {
        const uint64_t end = next_.load(std::memory_order_acquire);
        uint64_t first = after_seq + 1;
        if (end > first && end - first > N) first = end - N;
        const std::size_t base = out.size();
        Rec rec;
        for (uint64_t seq = first; seq < end && out.size() - base < max; ++seq) {
            const Peek status = peek(seq, rec);
            if (status == Peek::Ok) {
                out.push_back(rec);
            } else if (status == Peek::Pending || out.size() != base) {
                break;
            }
        }
    }

    // Number of the newest claimed event (0 when none).
    uint64_t last_seq() const { return next_.load(std::memory_order_acquire) - 1; }

private:
    struct Slot {
        std::atomic<uint64_t> stamp{0};
        std::atomic<uint64_t> words[kWords];
        Slot() {
            for (auto& w : words) w.store(0, std::memory_order_relaxed);
        }
    };

    // Waits out a concurrent writer, claims the slot and returns its (even)
    // version before the claim.
    static uint64_t lock(Slot& slot) {
        uint64_t cur = slot.stamp.load(std::memory_order_relaxed);
        for (;;) {
            if (!(cur & 1) &&
                slot.stamp.compare_exchange_weak(cur, cur + 1, std::memory_order_acq_rel,
                                                 std::memory_order_relaxed)) {
                break;
            }
            if (cur & 1) {
                // The other writer may be preempted (single-core routers):
                // give it the CPU instead of spinning out our timeslice.
                std::this_thread::yield();
                cur = slot.stamp.load(std::memory_order_relaxed);
            }
        }
        std::atomic_thread_fence(std::memory_order_release);
        return cur;
    }

    static uint64_t load_seq(const Slot& slot) { return slot.words[0].load(std::memory_order_relaxed); }

    static void store(Slot& slot, const Rec& rec) {
        const auto* bytes = reinterpret_cast<const unsigned char*>(&rec);
        for (std::size_t i = 0; i < kWords; ++i) {
            uint64_t word;
            std::memcpy(&word, bytes + i * sizeof(uint64_t), sizeof(word));
            slot.words[i].store(word, std::memory_order_relaxed);
        }
    }

    static void load(const Slot& slot, Rec& rec) {
        auto* bytes = reinterpret_cast<unsigned char*>(&rec);
        for (std::size_t i = 0; i < kWords; ++i) {
            const uint64_t word = slot.words[i].load(std::memory_order_relaxed);
            std::memcpy(bytes + i * sizeof(uint64_t), &word, sizeof(word));
        }
    }

    std::unique_ptr<Slot[]> slots_;
    std::atomic<uint64_t> next_{1};
};

} // namespace keen_pbr3
