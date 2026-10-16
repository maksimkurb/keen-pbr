#include "dns_tcp_reassembly.hpp"

#include <algorithm>
#include <utility>

namespace keen_pbr3 {

DnsTcpReassembler::DnsTcpReassembler(std::chrono::milliseconds ttl, std::size_t max_flows,
                                     std::size_t max_buffered_flows, std::size_t max_message)
    : ttl_(ttl),
      max_buffered_(std::max<std::size_t>(1, max_buffered_flows)),
      max_message_(std::min(max_message, kMaxMessage)),
      slots_(std::max<std::size_t>(1, max_flows)) {}

DnsTcpReassembler::Slot* DnsTcpReassembler::find(const l7::FlowKey& key, Time now) {
    Slot* found = nullptr;
    for (Slot& slot : slots_) {
        if (!slot.used) continue;
        if (now - slot.last_seen > ttl_) {
            release(slot);  // idle flow: drop (also covers the matching key)
            continue;
        }
        if (slot.key == key) found = &slot;
    }
    return found;
}

DnsTcpReassembler::Slot& DnsTcpReassembler::allocate(const l7::FlowKey& key, uint32_t seq,
                                                     Time now) {
    Slot* target = nullptr;
    for (Slot& slot : slots_) {
        if (!slot.used) {
            target = &slot;
            break;
        }
    }
    if (target == nullptr) {  // table full: evict the least recently fed flow
        target = &*std::min_element(slots_.begin(), slots_.end(), [](const Slot& a, const Slot& b) {
            return a.last_seen < b.last_seen;
        });
        release(*target);
    }
    target->used = true;
    target->poisoned = false;
    target->buffering = false;
    target->phase = Phase::prefix0;
    target->key = key;
    target->next_seq = seq;
    target->last_seen = now;
    ++used_;
    return *target;
}

void DnsTcpReassembler::release(Slot& slot) {
    if (!slot.used) return;
    if (slot.buffering) {
        --buffered_;
        slot.buf.clear();
        if (slot.buf.capacity() > spare_.capacity()) spare_.swap(slot.buf);
        else std::vector<uint8_t>().swap(slot.buf);
    }
    slot.buffering = false;
    slot.used = false;
    --used_;
}

void DnsTcpReassembler::poison(Slot& slot) {
    if (slot.buffering) {
        --buffered_;
        slot.buffering = false;
        slot.buf.clear();
        if (slot.buf.capacity() > spare_.capacity()) spare_.swap(slot.buf);
        else std::vector<uint8_t>().swap(slot.buf);
    }
    slot.poisoned = true;
}

void DnsTcpReassembler::evict_oldest_buffered(const Slot& keep) {
    Slot* oldest = nullptr;
    for (Slot& slot : slots_) {
        if (!slot.used || !slot.buffering || &slot == &keep) continue;
        if (oldest == nullptr || slot.last_seen < oldest->last_seen) oldest = &slot;
    }
    if (oldest != nullptr) release(*oldest);
}

void DnsTcpReassembler::begin_buffer(Slot& slot) {
    if (buffered_ >= max_buffered_) evict_oldest_buffered(slot);
    slot.buf = std::move(spare_);
    spare_ = std::vector<uint8_t>();
    slot.buf.clear();
    slot.buf.reserve(slot.msg_len);  // allocates only when no large enough spare was recycled
    slot.buffering = true;
    ++buffered_;
}

void DnsTcpReassembler::emit(Messages& out, ByteView message) {
    if (out.count < kMaxMessagesPerSegment) out.view[out.count++] = message;
    else ++out.dropped;
}

void DnsTcpReassembler::close(const l7::FlowKey& key) {
    for (Slot& slot : slots_) {
        if (slot.used && slot.key == key) {
            release(slot);
            return;
        }
    }
}

void DnsTcpReassembler::feed(const l7::FlowKey& key, uint32_t seq, ByteView payload, Time now,
                             Messages& out) {
    out.count = 0;
    out.dropped = 0;
    if (done_.capacity() != 0) {  // the previous feed()'s message is dead now: recycle it
        done_.clear();
        if (done_.capacity() > spare_.capacity()) spare_.swap(done_);
    }
    if (payload.size() == 0) return;

    Slot* found = find(key, now);
    Slot& slot = found != nullptr ? *found : allocate(key, seq, now);
    slot.last_seen = now;
    if (slot.poisoned) return;

    const int32_t delta = static_cast<int32_t>(seq - slot.next_seq);
    const uint8_t* data = payload.data();
    std::size_t size = payload.size();
    if (delta < 0) {  // retransmission or overlap: keep only the bytes not seen yet
        const std::size_t skip = static_cast<std::size_t>(-static_cast<int64_t>(delta));
        if (skip >= size) return;
        data += skip;
        size -= skip;
    } else if (delta > 0) {  // gap: out of order or lost; the stream position is gone
        poison(slot);
        return;
    }
    slot.next_seq += static_cast<uint32_t>(size);

    std::size_t pos = 0;
    while (pos < size) {
        if (slot.phase == Phase::prefix0) {
            slot.prefix_hi = data[pos++];
            slot.phase = Phase::prefix1;
            continue;
        }
        if (slot.phase == Phase::prefix1) {
            const std::size_t len = (static_cast<std::size_t>(slot.prefix_hi) << 8) | data[pos++];
            if (len < kMinMessage || len > max_message_) {  // not a DNS message / oversize
                poison(slot);
                return;
            }
            slot.msg_len = len;
            slot.phase = Phase::body;
            continue;
        }
        const std::size_t avail = size - pos;
        if (!slot.buffering) {
            if (avail >= slot.msg_len) {  // whole message inside this segment: zero copy
                emit(out, ByteView(data + pos, slot.msg_len));
                pos += slot.msg_len;
                slot.phase = Phase::prefix0;
                continue;
            }
            begin_buffer(slot);
        }
        const std::size_t take = std::min(avail, slot.msg_len - slot.buf.size());
        slot.buf.insert(slot.buf.end(), data + pos, data + pos + take);
        pos += take;
        if (slot.buf.size() == slot.msg_len) {
            // Complete: hand the buffer out through done_, recycle the old one.
            done_.swap(slot.buf);
            slot.buf = std::vector<uint8_t>();
            slot.buffering = false;
            --buffered_;
            slot.phase = Phase::prefix0;
            emit(out, ByteView(done_.data(), done_.size()));
        }
    }
}

}  // namespace keen_pbr3
