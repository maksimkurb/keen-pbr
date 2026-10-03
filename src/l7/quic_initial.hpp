#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../util/byte_view.hpp"

namespace keen_pbr3::l7 {

enum class QuicVersion : uint32_t { V1 = 0x00000001, V2 = 0x6b3343cf };

struct QuicInitialKeys {
    std::array<uint8_t, 16> key;
    std::array<uint8_t, 12> iv;
    std::array<uint8_t, 16> hp;
};

// Client Initial keys for the given DCID (RFC 9001 section 5.2; v2 per RFC 9369 section 3.3).
bool derive_client_initial_keys(QuicVersion v, ByteView dcid, QuicInitialKeys& out);

struct QuicInitialPacketInfo {
    QuicVersion version{QuicVersion::V1};
    std::array<uint8_t, 20> dcid{};
    uint8_t dcid_len{0};
};

// Decrypts every client Initial packet in a UDP datagram (coalesced packets allowed; stops at the
// first non-Initial or short-header packet) and reports each CRYPTO frame via on_crypto(offset, data).
// The data view points into `scratch` and is only valid during the callback.
// The AEAD tag is NOT verified (Initial keys are public; we only read, never trust).
// Returns false if the datagram is not a client Initial of a supported version (first packet) or the
// first packet is malformed. `info` is filled from the first packet before any callback.
bool decrypt_initial_datagram(ByteView udp_payload, QuicInitialPacketInfo& info,
                              const std::function<void(uint64_t offset, ByteView data)>& on_crypto,
                              std::vector<uint8_t>& scratch);

// Reassembles CRYPTO data per connection (keyed by DCID bytes) across datagrams.
class QuicCryptoAssembler {
public:
    QuicCryptoAssembler(std::size_t max_conns = 256, std::size_t max_bytes = 8192,
                        std::chrono::milliseconds ttl = std::chrono::seconds(2));

    // Feeds one UDP payload. Returns the contiguous CRYPTO stream prefix starting at offset 0 for this
    // connection (empty view if offset 0 not yet received or not a QUIC Initial). Valid until next call.
    ByteView feed(ByteView udp_payload, std::chrono::steady_clock::time_point now);

    // Erases the connection of the last fed datagram (call once the SNI was found).
    void erase_last();

    std::size_t size() const;

private:
    struct Conn {
        std::string key;
        std::vector<uint8_t> buf;
        std::vector<std::pair<uint64_t, uint64_t>> ranges;  // sorted, merged [start, end)
        std::chrono::steady_clock::time_point last_seen;
    };

    void add_fragment_(Conn& c, uint64_t offset, ByteView data);
    void expire_(std::chrono::steady_clock::time_point now);

    std::size_t max_conns_;
    std::size_t max_bytes_;
    std::chrono::milliseconds ttl_;
    std::list<Conn> lru_;  // front = most recently used
    std::unordered_map<std::string, std::list<Conn>::iterator> index_;
    std::string last_key_;
    std::vector<uint8_t> scratch_;
};

} // namespace keen_pbr3::l7
