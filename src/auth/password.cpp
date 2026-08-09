#include "password.hpp"

#include <monocypher.h>

#include <array>
#include <cerrno>
#include <cstdint>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <sys/random.h>
#include <vector>

namespace keen_pbr3::auth {
namespace {

using Salt = std::array<std::uint8_t, 16>;
using Digest = std::array<std::uint8_t, 32>;

constexpr std::uint32_t kArgon2MemoryBlocks = 19'456; // 19 MiB
constexpr std::uint32_t kArgon2Passes = 2;
constexpr std::uint32_t kArgon2Lanes = 1;
constexpr std::string_view kVerifierPrefix = "argon2id$v=19$m=19456,t=2,p=1$";
constexpr char kHex[] = "0123456789abcdef";

struct Argon2WorkArea {
    std::vector<std::uint64_t> words = std::vector<std::uint64_t>(
        static_cast<std::size_t>(kArgon2MemoryBlocks) * 1024 / sizeof(std::uint64_t));

    ~Argon2WorkArea() {
        crypto_wipe(words.data(), words.size() * sizeof(std::uint64_t));
    }
};

template <std::size_t N>
std::string hex_encode(const std::array<std::uint8_t, N>& bytes) {
    std::string encoded;
    encoded.reserve(N * 2);
    for (const auto byte : bytes) {
        encoded.push_back(kHex[byte >> 4]);
        encoded.push_back(kHex[byte & 0x0f]);
    }
    return encoded;
}

int hex_value(char character) {
    if (character >= '0' && character <= '9') return character - '0';
    if (character >= 'a' && character <= 'f') return character - 'a' + 10;
    return -1;
}

template <std::size_t N>
bool hex_decode(std::string_view encoded, std::array<std::uint8_t, N>& bytes) {
    if (encoded.size() != N * 2) return false;
    for (std::size_t index = 0; index < N; ++index) {
        const auto high = hex_value(encoded[index * 2]);
        const auto low = hex_value(encoded[index * 2 + 1]);
        if (high < 0 || low < 0) return false;
        bytes[index] = static_cast<std::uint8_t>((high << 4) | low);
    }
    return true;
}

template <std::size_t N>
std::array<std::uint8_t, N> random_bytes() {
    std::array<std::uint8_t, N> bytes{};
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto count = ::getrandom(bytes.data() + offset, bytes.size() - offset, 0);
        if (count < 0) {
            if (errno == EINTR) continue;
            if (errno == ENOSYS) {
                const auto remaining = bytes.size() - offset;
                if (remaining > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max())) {
                    throw std::length_error("random byte request is too large");
                }
                std::ifstream source("/dev/urandom", std::ios::binary);
                source.read(reinterpret_cast<char*>(bytes.data() + offset),
                            static_cast<std::streamsize>(remaining));
                if (!source) throw std::runtime_error("failed to read secure random bytes");
                return bytes;
            }
            throw std::runtime_error("failed to read secure random bytes");
        }
        if (count == 0) throw std::runtime_error("secure random source returned no data");
        offset += static_cast<std::size_t>(count);
    }
    return bytes;
}

bool password_size_supported(std::string_view password) {
    return password.size() <= std::numeric_limits<std::uint32_t>::max();
}

Digest derive(std::string_view password, const Salt& salt) {
    if (!password_size_supported(password)) {
        throw std::length_error("password is too large");
    }

    Argon2WorkArea work;
    Digest digest{};
    const crypto_argon2_config config{
        CRYPTO_ARGON2_ID, kArgon2MemoryBlocks, kArgon2Passes, kArgon2Lanes};
    const crypto_argon2_inputs inputs{
        reinterpret_cast<const std::uint8_t*>(password.data()), salt.data(),
        static_cast<std::uint32_t>(password.size()), static_cast<std::uint32_t>(salt.size())};
    crypto_argon2(digest.data(), static_cast<std::uint32_t>(digest.size()), work.words.data(),
                  config, inputs, crypto_argon2_no_extras);
    return digest;
}

bool parse(std::string_view encoded, Salt& salt, Digest& digest) {
    if (encoded.substr(0, kVerifierPrefix.size()) != kVerifierPrefix) return false;
    encoded.remove_prefix(kVerifierPrefix.size());
    const auto separator = encoded.find('$');
    if (separator == std::string_view::npos || encoded.find('$', separator + 1) != std::string_view::npos) {
        return false;
    }
    return hex_decode(encoded.substr(0, separator), salt) &&
           hex_decode(encoded.substr(separator + 1), digest);
}

} // namespace

std::string generate_password_hash(std::string_view password) {
    const auto salt = random_bytes<16>();
    auto digest = derive(password, salt);
    auto encoded = std::string(kVerifierPrefix) + hex_encode(salt) + '$' + hex_encode(digest);
    crypto_wipe(digest.data(), digest.size());
    return encoded;
}

bool valid_password_hash(std::string_view encoded) {
    Salt salt{};
    Digest digest{};
    return parse(encoded, salt, digest);
}

bool verify_password(std::string_view password, std::string_view encoded) {
    Salt salt{};
    Digest expected{};
    if (!password_size_supported(password) || !parse(encoded, salt, expected)) return false;
    auto actual = derive(password, salt);
    const bool matches = crypto_verify32(actual.data(), expected.data()) == 0;
    crypto_wipe(actual.data(), actual.size());
    crypto_wipe(expected.data(), expected.size());
    return matches;
}

std::string random_token() {
    return hex_encode(random_bytes<32>());
}

std::string blake2b_hex(std::string_view value) {
    Digest digest{};
    crypto_blake2b(digest.data(), digest.size(),
                   reinterpret_cast<const std::uint8_t*>(value.data()), value.size());
    auto encoded = hex_encode(digest);
    crypto_wipe(digest.data(), digest.size());
    return encoded;
}

bool constant_time_equal(std::string_view lhs, std::string_view rhs) {
    if (lhs.size() != 64 || rhs.size() != 64) return false;
    return crypto_verify64(reinterpret_cast<const std::uint8_t*>(lhs.data()),
                           reinterpret_cast<const std::uint8_t*>(rhs.data())) == 0;
}

} // namespace keen_pbr3::auth
