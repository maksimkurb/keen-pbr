#pragma once

#include "../l7/flow_buffer.hpp"
#include "../util/byte_view.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace keen_pbr3 {

// Reassembles the length-prefixed DNS messages of DNS-over-TCP replies (server
// -> client direction, src port 53) from the segments the DNS hold queue sees.
//
// State is kept per reply flow (family, src, dst, sport, dport) in a fixed table:
// the next expected TCP sequence number, the position inside the 2-byte length
// prefix, and, only while a message spans segments, a buffer for its bytes.  A
// message that fits entirely in one segment is returned as a view into that
// segment (no copy, no allocation); several messages in one segment are all
// returned.  Segments are accepted in order; a retransmission or overlap is
// trimmed to its new bytes, and a gap (out-of-order segment, lost packet)
// poisons the flow: it is ignored until it idles out, closes or is evicted,
// because a mid-message position cannot be recovered.  The first payload segment
// seen for a flow is assumed to start on a message boundary.
//
// Bounds: kMaxFlows tracked flows (least recently fed evicted first), flows
// idle for ttl are dropped, at most kMaxBufferedFlows flows buffer a partial
// message at once (each at most 65535 bytes, so < 2.1 MiB in total plus two
// recycled spare buffers).  A multi-segment message allocates its buffer when it
// starts unless a recycled one is large enough; the zero-copy and steady
// multi-segment paths do not allocate.
//
// Single-threaded.
class DnsTcpReassembler {
public:
    static constexpr std::size_t kMaxFlows = 256;
    static constexpr std::size_t kMaxBufferedFlows = 32;
    static constexpr std::size_t kMaxMessagesPerSegment = 16;
    static constexpr std::size_t kMinMessage = 12;  // DNS header
    static constexpr std::size_t kMaxMessage = 65535;

    // Complete messages found by one feed().  Views stay valid until the next
    // feed()/close() of the reassembler.
    struct Messages {
        std::array<ByteView, kMaxMessagesPerSegment> view;
        std::size_t count{0};
        std::size_t dropped{0};  // complete messages beyond kMaxMessagesPerSegment
    };

    explicit DnsTcpReassembler(std::chrono::milliseconds ttl = std::chrono::seconds(2),
                               std::size_t max_flows = kMaxFlows,
                               std::size_t max_buffered_flows = kMaxBufferedFlows,
                               std::size_t max_message = kMaxMessage);

    // Feeds one segment payload.  `out` is cleared first.
    void feed(const l7::FlowKey& key, uint32_t seq, ByteView payload,
              std::chrono::steady_clock::time_point now, Messages& out);

    // FIN, RST or SYN seen for the flow: forgets it.
    void close(const l7::FlowKey& key);

    std::size_t flows() const { return used_; }
    std::size_t buffered_flows() const { return buffered_; }

private:
    enum class Phase : uint8_t { prefix0, prefix1, body };
    struct Slot {
        bool used{false};
        bool poisoned{false};
        bool buffering{false};
        Phase phase{Phase::prefix0};
        uint8_t prefix_hi{0};
        l7::FlowKey key;
        uint32_t next_seq{0};
        std::size_t msg_len{0};
        std::chrono::steady_clock::time_point last_seen;
        std::vector<uint8_t> buf;
    };
    using Time = std::chrono::steady_clock::time_point;

    Slot* find(const l7::FlowKey& key, Time now);
    Slot& allocate(const l7::FlowKey& key, uint32_t seq, Time now);
    void release(Slot& slot);
    void poison(Slot& slot);
    void begin_buffer(Slot& slot);
    void evict_oldest_buffered(const Slot& keep);
    void emit(Messages& out, ByteView message);

    std::chrono::milliseconds ttl_;
    std::size_t max_buffered_;
    std::size_t max_message_;
    std::vector<Slot> slots_;  // fixed size
    std::size_t used_{0};
    std::size_t buffered_{0};
    std::vector<uint8_t> done_;   // holds the message returned by the last feed()
    std::vector<uint8_t> spare_;  // recycled buffer for the next multi-segment message
};

}  // namespace keen_pbr3
