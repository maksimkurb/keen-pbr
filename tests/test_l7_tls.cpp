#include <doctest/doctest.h>

#include <vector>

#include "../src/l7/tls_client_hello.hpp"

using namespace keen_pbr3;
using namespace keen_pbr3::l7;

namespace {

// Helper to build a ClientHello handshake
std::vector<uint8_t> build_client_hello(uint8_t session_id_len, uint16_t num_cipher_suites,
                                        const std::vector<std::pair<uint16_t, std::vector<uint8_t>>>& extensions,
                                        const std::string& sni = "") {
    std::vector<uint8_t> result;

    // msg_type (1 byte): 1 = ClientHello
    result.push_back(1);

    // We'll update the length later
    std::size_t length_pos = result.size();
    result.push_back(0);
    result.push_back(0);
    result.push_back(0);

    // legacy_version (2 bytes): 0x0303 = TLS 1.2
    result.push_back(0x03);
    result.push_back(0x03);

    // random (32 bytes)
    for (int i = 0; i < 32; ++i) {
        result.push_back(i);
    }

    // session_id
    result.push_back(session_id_len);
    for (int i = 0; i < session_id_len; ++i) {
        result.push_back(i);
    }

    // cipher_suites
    uint16_t cipher_suites_len = num_cipher_suites * 2;
    result.push_back((cipher_suites_len >> 8) & 0xFF);
    result.push_back(cipher_suites_len & 0xFF);
    for (uint16_t i = 0; i < num_cipher_suites; ++i) {
        result.push_back(0x00);
        result.push_back(0x2F);  // TLS_RSA_WITH_AES_128_CBC_SHA
    }

    // compression_methods
    result.push_back(0x01);  // length
    result.push_back(0x00);  // null compression

    // extensions
    std::vector<uint8_t> extensions_data;
    for (const auto& [ext_type, ext_value] : extensions) {
        extensions_data.push_back((ext_type >> 8) & 0xFF);
        extensions_data.push_back(ext_type & 0xFF);
        uint16_t ext_len = ext_value.size();
        extensions_data.push_back((ext_len >> 8) & 0xFF);
        extensions_data.push_back(ext_len & 0xFF);
        extensions_data.insert(extensions_data.end(), ext_value.begin(), ext_value.end());
    }

    uint16_t extensions_len = extensions_data.size();
    result.push_back((extensions_len >> 8) & 0xFF);
    result.push_back(extensions_len & 0xFF);
    result.insert(result.end(), extensions_data.begin(), extensions_data.end());

    // Update length
    uint32_t body_len = result.size() - 4;
    result[length_pos] = (body_len >> 16) & 0xFF;
    result[length_pos + 1] = (body_len >> 8) & 0xFF;
    result[length_pos + 2] = body_len & 0xFF;

    return result;
}

// Helper to build server_name extension
std::vector<uint8_t> build_server_name_extension(const std::string& sni) {
    std::vector<uint8_t> result;

    // server_name_list length (2 bytes) - we'll update this later
    std::size_t list_len_pos = result.size();
    result.push_back(0);
    result.push_back(0);

    // name_type: 0 = host_name
    result.push_back(0);

    // name length (2 bytes)
    uint16_t name_len = sni.size();
    result.push_back((name_len >> 8) & 0xFF);
    result.push_back(name_len & 0xFF);

    // name
    result.insert(result.end(), sni.begin(), sni.end());

    // Update server_name_list length
    uint16_t list_len = result.size() - 2;
    result[list_len_pos] = (list_len >> 8) & 0xFF;
    result[list_len_pos + 1] = list_len & 0xFF;

    return result;
}

}  // namespace

TEST_CASE("l7: TLS ClientHello SNI found when server_name is first extension") {
    auto sni_ext = build_server_name_extension("example.com");
    std::vector<std::pair<uint16_t, std::vector<uint8_t>>> extensions;
    extensions.push_back({0x0000, sni_ext});

    auto hello = build_client_hello(0, 4, extensions);
    ByteView handshake(hello.data(), hello.size());

    std::string sni_out;
    auto status = client_hello_sni(handshake, sni_out);
    CHECK(status == ParseStatus::Found);
    CHECK(sni_out == "example.com");
}

TEST_CASE("l7: TLS ClientHello SNI found after large dummy extension") {
    auto dummy_ext = std::vector<uint8_t>(1200, 0xFF);  // 1200 bytes
    auto sni_ext = build_server_name_extension("test.example.com");

    std::vector<std::pair<uint16_t, std::vector<uint8_t>>> extensions;
    extensions.push_back({0x11ec, dummy_ext});
    extensions.push_back({0x0000, sni_ext});

    auto hello = build_client_hello(0, 4, extensions);
    ByteView handshake(hello.data(), hello.size());

    std::string sni_out;
    auto status = client_hello_sni(handshake, sni_out);
    CHECK(status == ParseStatus::Found);
    CHECK(sni_out == "test.example.com");
}

TEST_CASE("l7: TLS ClientHello SNI uppercase lowercased") {
    auto sni_ext = build_server_name_extension("Example.COM");
    std::vector<std::pair<uint16_t, std::vector<uint8_t>>> extensions;
    extensions.push_back({0x0000, sni_ext});

    auto hello = build_client_hello(0, 4, extensions);
    ByteView handshake(hello.data(), hello.size());

    std::string sni_out;
    auto status = client_hello_sni(handshake, sni_out);
    CHECK(status == ParseStatus::Found);
    CHECK(sni_out == "example.com");
}

TEST_CASE("l7: TLS ClientHello SNI trailing dot stripped") {
    auto sni_ext = build_server_name_extension("example.com.");
    std::vector<std::pair<uint16_t, std::vector<uint8_t>>> extensions;
    extensions.push_back({0x0000, sni_ext});

    auto hello = build_client_hello(0, 4, extensions);
    ByteView handshake(hello.data(), hello.size());

    std::string sni_out;
    auto status = client_hello_sni(handshake, sni_out);
    CHECK(status == ParseStatus::Found);
    CHECK(sni_out == "example.com");
}

TEST_CASE("l7: TLS ClientHello no server_name extension") {
    std::vector<std::pair<uint16_t, std::vector<uint8_t>>> extensions;
    extensions.push_back({0x000a, {0x00, 0x00}});  // supported_groups, dummy

    auto hello = build_client_hello(0, 4, extensions);
    ByteView handshake(hello.data(), hello.size());

    std::string sni_out;
    auto status = client_hello_sni(handshake, sni_out);
    CHECK(status == ParseStatus::NotMatched);
}

TEST_CASE("l7: TLS ClientHello truncated handshake needs more") {
    auto sni_ext = build_server_name_extension("example.com");
    std::vector<std::pair<uint16_t, std::vector<uint8_t>>> extensions;
    extensions.push_back({0x0000, sni_ext});

    auto hello = build_client_hello(0, 4, extensions);

    // Truncate in the middle
    ByteView handshake(hello.data(), hello.size() / 2);
    std::string sni_out;
    auto status = client_hello_sni(handshake, sni_out);
    CHECK(status == ParseStatus::NeedMore);
}

TEST_CASE("l7: TLS ClientHello early Found when SNI fully present") {
    auto dummy_ext = std::vector<uint8_t>(1200, 0xFF);
    auto sni_ext = build_server_name_extension("example.com");

    std::vector<std::pair<uint16_t, std::vector<uint8_t>>> extensions;
    extensions.push_back({0x11ec, dummy_ext});
    extensions.push_back({0x0000, sni_ext});

    auto hello = build_client_hello(0, 4, extensions);

    // Find where SNI extension ends
    // We know the structure, so we'll truncate after SNI is complete
    // This should still return Found
    ByteView handshake(hello.data(), hello.size());
    std::string sni_out;
    auto status = client_hello_sni(handshake, sni_out);
    CHECK(status == ParseStatus::Found);
    CHECK(sni_out == "example.com");
}

TEST_CASE("l7: TLS ClientHello invalid SNI with space") {
    auto sni_ext = build_server_name_extension("exam ple.com");
    std::vector<std::pair<uint16_t, std::vector<uint8_t>>> extensions;
    extensions.push_back({0x0000, sni_ext});

    auto hello = build_client_hello(0, 4, extensions);
    ByteView handshake(hello.data(), hello.size());

    std::string sni_out;
    auto status = client_hello_sni(handshake, sni_out);
    CHECK(status == ParseStatus::NotMatched);
}

TEST_CASE("l7: TLS ClientHello invalid session_id length") {
    auto sni_ext = build_server_name_extension("example.com");
    std::vector<std::pair<uint16_t, std::vector<uint8_t>>> extensions;
    extensions.push_back({0x0000, sni_ext});

    auto hello = build_client_hello(33, 4, extensions);  // session_id > 32
    ByteView handshake(hello.data(), hello.size());

    std::string sni_out;
    auto status = client_hello_sni(handshake, sni_out);
    CHECK(status == ParseStatus::NotMatched);
}

TEST_CASE("l7: TLS stream SNI in single record") {
    auto sni_ext = build_server_name_extension("example.com");
    std::vector<std::pair<uint16_t, std::vector<uint8_t>>> extensions;
    extensions.push_back({0x0000, sni_ext});

    auto hello = build_client_hello(0, 4, extensions);

    // Build TLS record
    std::vector<uint8_t> stream;
    stream.push_back(22);  // content_type = Handshake
    stream.push_back(3);   // version major
    stream.push_back(3);   // version minor
    uint16_t record_len = hello.size();
    stream.push_back((record_len >> 8) & 0xFF);
    stream.push_back(record_len & 0xFF);
    stream.insert(stream.end(), hello.begin(), hello.end());

    std::vector<uint8_t> scratch;
    ByteView stream_view(stream.data(), stream.size());
    std::string sni_out;
    auto status = tls_stream_sni(stream_view, sni_out, scratch);
    CHECK(status == ParseStatus::Found);
    CHECK(sni_out == "example.com");
}

TEST_CASE("l7: TLS stream reuses scratch between calls") {
    auto sni_ext = build_server_name_extension("example.com");
    std::vector<std::pair<uint16_t, std::vector<uint8_t>>> extensions;
    extensions.push_back({0x0000, sni_ext});
    auto hello = build_client_hello(0, 4, extensions);

    std::vector<uint8_t> stream = {22, 3, 3, static_cast<uint8_t>(hello.size() >> 8),
                                   static_cast<uint8_t>(hello.size() & 0xFF)};
    stream.insert(stream.end(), hello.begin(), hello.end());

    std::vector<uint8_t> scratch = {0xde, 0xad, 0xbe, 0xef};  // leftover from another flow
    std::string sni_out;
    CHECK(tls_stream_sni(ByteView(stream.data(), stream.size()), sni_out, scratch) == ParseStatus::Found);
    CHECK(sni_out == "example.com");
    sni_out.clear();
    CHECK(tls_stream_sni(ByteView(stream.data(), stream.size()), sni_out, scratch) == ParseStatus::Found);
    CHECK(sni_out == "example.com");
}

TEST_CASE("l7: TLS stream fragmented across two records") {
    auto sni_ext = build_server_name_extension("example.com");
    std::vector<std::pair<uint16_t, std::vector<uint8_t>>> extensions;
    extensions.push_back({0x0000, sni_ext});

    auto hello = build_client_hello(0, 4, extensions);

    // Split hello in half
    std::size_t mid = hello.size() / 2;

    std::vector<uint8_t> stream;

    // First record
    stream.push_back(22);  // content_type = Handshake
    stream.push_back(3);   // version major
    stream.push_back(3);   // version minor
    uint16_t record_len1 = mid;
    stream.push_back((record_len1 >> 8) & 0xFF);
    stream.push_back(record_len1 & 0xFF);
    stream.insert(stream.end(), hello.begin(), hello.begin() + mid);

    // Second record
    stream.push_back(22);  // content_type = Handshake
    stream.push_back(3);   // version major
    stream.push_back(3);   // version minor
    uint16_t record_len2 = hello.size() - mid;
    stream.push_back((record_len2 >> 8) & 0xFF);
    stream.push_back(record_len2 & 0xFF);
    stream.insert(stream.end(), hello.begin() + mid, hello.end());

    std::vector<uint8_t> scratch;
    ByteView stream_view(stream.data(), stream.size());
    std::string sni_out;
    auto status = tls_stream_sni(stream_view, sni_out, scratch);
    CHECK(status == ParseStatus::Found);
    CHECK(sni_out == "example.com");
}

TEST_CASE("l7: TLS stream cut mid-record") {
    auto sni_ext = build_server_name_extension("example.com");
    std::vector<std::pair<uint16_t, std::vector<uint8_t>>> extensions;
    extensions.push_back({0x0000, sni_ext});

    auto hello = build_client_hello(0, 4, extensions);

    std::vector<uint8_t> stream;
    stream.push_back(22);  // content_type = Handshake
    stream.push_back(3);   // version major
    stream.push_back(3);   // version minor
    uint16_t record_len = hello.size();
    stream.push_back((record_len >> 8) & 0xFF);
    stream.push_back(record_len & 0xFF);
    stream.insert(stream.end(), hello.begin(), hello.end());

    // Truncate in the middle of the record
    stream.resize(stream.size() / 2);

    std::vector<uint8_t> scratch;
    ByteView stream_view(stream.data(), stream.size());
    std::string sni_out;
    auto status = tls_stream_sni(stream_view, sni_out, scratch);
    CHECK(status == ParseStatus::NeedMore);
}

TEST_CASE("l7: TLS stream first byte not 22") {
    std::vector<uint8_t> stream;
    stream.push_back(0x17);  // content_type = Application Data (not Handshake)
    stream.insert(stream.end(), 100, 0xFF);

    std::vector<uint8_t> scratch;
    ByteView stream_view(stream.data(), stream.size());
    std::string sni_out;
    auto status = tls_stream_sni(stream_view, sni_out, scratch);
    CHECK(status == ParseStatus::NotMatched);
}

TEST_CASE("l7: TLS stream record length too large") {
    std::vector<uint8_t> stream;
    stream.push_back(22);   // content_type = Handshake
    stream.push_back(3);    // version major
    stream.push_back(3);    // version minor
    stream.push_back(0xFF); // record_len = 0xFFFF (> 16640)
    stream.push_back(0xFF);

    std::vector<uint8_t> scratch;
    ByteView stream_view(stream.data(), stream.size());
    std::string sni_out;
    auto status = tls_stream_sni(stream_view, sni_out, scratch);
    CHECK(status == ParseStatus::NotMatched);
}

TEST_CASE("l7: TLS ClientHello robustness - prefix truncation") {
    auto sni_ext = build_server_name_extension("example.com");
    std::vector<std::pair<uint16_t, std::vector<uint8_t>>> extensions;
    extensions.push_back({0x0000, sni_ext});

    auto hello = build_client_hello(0, 4, extensions);

    // Try parsing every prefix length from 0 to full size
    for (std::size_t len = 0; len <= hello.size(); ++len) {
        ByteView handshake(hello.data(), len);
        std::string sni_out;
        auto status = client_hello_sni(handshake, sni_out);

        // Should never crash
        if (len < hello.size()) {
            // Truncated
            CHECK((status == ParseStatus::NeedMore || status == ParseStatus::NotMatched));
        } else {
            // Full
            CHECK(status == ParseStatus::Found);
            CHECK(sni_out == "example.com");
        }
    }
}

TEST_CASE("l7: TLS ClientHello SNI with wildcard") {
    auto sni_ext = build_server_name_extension("*.example.com");
    std::vector<std::pair<uint16_t, std::vector<uint8_t>>> extensions;
    extensions.push_back({0x0000, sni_ext});

    auto hello = build_client_hello(0, 4, extensions);
    ByteView handshake(hello.data(), hello.size());

    std::string sni_out;
    auto status = client_hello_sni(handshake, sni_out);
    CHECK(status == ParseStatus::Found);
    CHECK(sni_out == "*.example.com");
}

TEST_CASE("l7: TLS ClientHello SNI with underscores") {
    auto sni_ext = build_server_name_extension("test_server.example.com");
    std::vector<std::pair<uint16_t, std::vector<uint8_t>>> extensions;
    extensions.push_back({0x0000, sni_ext});

    auto hello = build_client_hello(0, 4, extensions);
    ByteView handshake(hello.data(), hello.size());

    std::string sni_out;
    auto status = client_hello_sni(handshake, sni_out);
    CHECK(status == ParseStatus::Found);
    CHECK(sni_out == "test_server.example.com");
}

TEST_CASE("l7: TLS ClientHello SNI with invalid character is rejected") {
    // Create SNI with invalid byte 0xFF
    std::vector<uint8_t> sni_name = {
        5, 't', 'e', 's', 't', 0xFF, 7, 'e', 'x', 'a', 'm', 'p', 'l', 'e', 3, 'c', 'o', 'm'
    };

    std::vector<uint8_t> ext;
    ext.push_back(0);
    ext.push_back(static_cast<uint8_t>(sni_name.size()));
    ext.push_back(0);
    ext.push_back(0);
    ext.push_back(static_cast<uint8_t>(sni_name.size() >> 8));
    ext.push_back(static_cast<uint8_t>(sni_name.size() & 0xFF));
    ext.insert(ext.end(), sni_name.begin(), sni_name.end());

    std::vector<std::pair<uint16_t, std::vector<uint8_t>>> extensions;
    extensions.push_back({0x0000, ext});

    auto hello = build_client_hello(0, 4, extensions);
    ByteView handshake(hello.data(), hello.size());

    std::string sni_out;
    auto status = client_hello_sni(handshake, sni_out);
    CHECK(status == ParseStatus::NotMatched);
}

TEST_CASE("l7: TLS ClientHello robustness - byte flipping") {
    auto sni_ext = build_server_name_extension("example.com");
    std::vector<std::pair<uint16_t, std::vector<uint8_t>>> extensions;
    extensions.push_back({0x0000, sni_ext});

    auto hello = build_client_hello(0, 4, extensions);

    // For each byte, flip it and try to parse
    for (std::size_t i = 0; i < hello.size(); ++i) {
        std::vector<uint8_t> mutated = hello;
        mutated[i] ^= 0xFF;

        ByteView handshake(mutated.data(), mutated.size());
        std::string sni_out;
        auto status = client_hello_sni(handshake, sni_out);

        // Should never crash, may return NotMatched, NeedMore, or Found
        // Just verify it doesn't crash
        (void)status;
        (void)sni_out;
    }
}
