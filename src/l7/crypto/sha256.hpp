#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "../../util/byte_view.hpp"

namespace keen_pbr3::l7::crypto {

using Sha256Digest = std::array<uint8_t, 32>;

// Incremental SHA-256 (FIPS 180-4).
class Sha256 {
public:
    Sha256();
    void update(const uint8_t* data, std::size_t len);
    Sha256Digest final();

private:
    void process_block_(const uint8_t* block);

    uint32_t state_[8];
    uint8_t buffer_[64];
    std::size_t buffer_len_{0};
    uint64_t total_len_{0};
};

Sha256Digest sha256(ByteView data);

// HMAC-SHA256 (RFC 2104 / RFC 4231).
Sha256Digest hmac_sha256(ByteView key, ByteView message);

// HKDF-Extract with SHA-256 (RFC 5869 section 2.2).
Sha256Digest hkdf_extract(ByteView salt, ByteView ikm);

// HKDF-Expand with SHA-256 (RFC 5869 section 2.3). Returns false if out_len > 255 * 32.
bool hkdf_expand(ByteView prk, ByteView info, uint8_t* out, std::size_t out_len);

// TLS 1.3 HKDF-Expand-Label (RFC 8446 section 7.1) with the "tls13 " prefix and an empty context.
bool hkdf_expand_label(ByteView secret, const char* label, uint8_t* out, std::size_t out_len);

} // namespace keen_pbr3::l7::crypto
