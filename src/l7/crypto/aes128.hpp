#pragma once

#include <cstddef>
#include <cstdint>

namespace keen_pbr3::l7::crypto {

// Expanded AES-128 key (11 round keys).
struct Aes128KeySchedule {
    uint8_t round_keys[176];
};

// FIPS-197 key expansion.
void aes128_expand_key(const uint8_t key[16], Aes128KeySchedule& ks);

// Encrypts exactly one 16-byte block (ECB). Decryption is not provided (not needed).
void aes128_encrypt_block(const Aes128KeySchedule& ks, const uint8_t in[16], uint8_t out[16]);

// XORs data with the AES-GCM CTR keystream: counter block = iv12 || be32(counter), where the
// counter starts at counter_start (GCM payload starts at 2; 1 is J0, used only for the tag) and
// increments (inc32) per 16-byte block. Applying it to GCM ciphertext yields the plaintext.
//
// SECURITY NOTE: the GCM authentication tag is INTENTIONALLY NOT verified anywhere in this
// code base. We only read QUIC Initial packets (whose keys are public, derived from the DCID)
// to extract the SNI as a routing hint; nothing here is a security decision.
void aes128_ctr_xor(const Aes128KeySchedule& ks, const uint8_t iv12[12], uint32_t counter_start,
                    uint8_t* data, std::size_t len);

} // namespace keen_pbr3::l7::crypto
