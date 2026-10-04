#include "tls_client_hello.hpp"

#include <algorithm>

namespace keen_pbr3::l7 {

namespace {

// Checks if a character is valid in a domain name after lowercasing
bool is_valid_domain_char(uint8_t c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_';
}

// Lowercases and validates a domain name
bool validate_and_lowercase(std::string& name) {
    if (name.empty() || name.size() > 253) {
        return false;
    }

    for (auto& c : name) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        } else if (!is_valid_domain_char(c)) {
            return false;
        }
    }

    // Strip trailing dot
    if (!name.empty() && name.back() == '.') {
        name.pop_back();
    }

    return !name.empty() && name.size() <= 253;
}

}  // namespace

ParseStatus client_hello_sni(ByteView handshake, std::string& sni_out) {
    const uint8_t* data = handshake.data();
    std::size_t size = handshake.size();

    // Need at least handshake header (1 msg_type + 3 length)
    if (size < 4) {
        return ParseStatus::NeedMore;
    }

    // Check msg_type is ClientHello (1)
    if (data[0] != 1) {
        return ParseStatus::NotMatched;
    }

    // Parse 3-byte length
    uint32_t msg_len = (static_cast<uint32_t>(data[1]) << 16) | (static_cast<uint32_t>(data[2]) << 8) | data[3];

    // Total needed is 4 (header) + msg_len
    std::size_t total_needed = 4 + msg_len;
    if (size >= total_needed) {
        size = total_needed;  // ignore bytes after the ClientHello
    }
    // A shorter buffer is still walked: the SNI may already be complete.

    // Now parse ClientHello body (starts at offset 4)
    std::size_t offset = 4;
    std::size_t body_size = size - 4;

    // legacy_version (2 bytes)
    if (body_size < 2) {
        return ParseStatus::NeedMore;
    }
    offset += 2;
    body_size -= 2;

    // random (32 bytes)
    if (body_size < 32) {
        return ParseStatus::NeedMore;
    }
    offset += 32;
    body_size -= 32;

    // session_id (1-byte len + data)
    if (body_size < 1) {
        return ParseStatus::NeedMore;
    }
    uint8_t session_id_len = data[offset];
    offset += 1;
    body_size -= 1;

    if (session_id_len > 32) {
        return ParseStatus::NotMatched;  // Invalid session_id length
    }

    if (body_size < session_id_len) {
        return ParseStatus::NeedMore;
    }
    offset += session_id_len;
    body_size -= session_id_len;

    // cipher_suites (2-byte len + data)
    if (body_size < 2) {
        return ParseStatus::NeedMore;
    }
    uint16_t cipher_suites_len = (static_cast<uint16_t>(data[offset]) << 8) | data[offset + 1];
    offset += 2;
    body_size -= 2;

    if (cipher_suites_len < 2 || cipher_suites_len % 2 != 0) {
        return ParseStatus::NotMatched;  // Invalid cipher_suites length
    }

    if (body_size < cipher_suites_len) {
        return ParseStatus::NeedMore;
    }
    offset += cipher_suites_len;
    body_size -= cipher_suites_len;

    // compression_methods (1-byte len + data)
    if (body_size < 1) {
        return ParseStatus::NeedMore;
    }
    uint8_t compression_methods_len = data[offset];
    offset += 1;
    body_size -= 1;

    if (compression_methods_len < 1) {
        return ParseStatus::NotMatched;  // Must have at least null compression
    }

    if (body_size < compression_methods_len) {
        return ParseStatus::NeedMore;
    }
    offset += compression_methods_len;
    body_size -= compression_methods_len;

    // extensions (2-byte len)
    if (body_size < 2) {
        return ParseStatus::NeedMore;
    }
    uint16_t extensions_len = (static_cast<uint16_t>(data[offset]) << 8) | data[offset + 1];
    offset += 2;
    body_size -= 2;

    // Parse extensions
    std::size_t ext_offset = offset;
    std::size_t ext_remaining = std::min(static_cast<std::size_t>(extensions_len), body_size);

    while (ext_remaining >= 4) {  // Need at least type(2) + len(2)
        uint16_t ext_type = (static_cast<uint16_t>(data[ext_offset]) << 8) | data[ext_offset + 1];
        uint16_t ext_len = (static_cast<uint16_t>(data[ext_offset + 2]) << 8) | data[ext_offset + 3];
        ext_offset += 4;
        ext_remaining -= 4;

        if (ext_len > ext_remaining) {
            // Not enough data for this extension
            if (ext_type == 0x0000) {
                // This is the server_name extension but it's truncated
                return ParseStatus::NeedMore;
            }
            // For other extensions, we can skip them
            break;
        }

        if (ext_type == 0x0000) {
            // Found server_name extension
            if (ext_len < 2) {
                return ParseStatus::NotMatched;
            }

            uint16_t server_name_list_len = (static_cast<uint16_t>(data[ext_offset]) << 8) |
                                           data[ext_offset + 1];
            ext_offset += 2;

            if (server_name_list_len > ext_len - 2) {
                return ParseStatus::NotMatched;
            }

            // Parse server_name_list entries
            std::size_t sn_offset = ext_offset;
            std::size_t sn_remaining = server_name_list_len;

            while (sn_remaining >= 3) {  // Need at least name_type(1) + name_len(2)
                uint8_t name_type = data[sn_offset];
                uint16_t name_len = (static_cast<uint16_t>(data[sn_offset + 1]) << 8) |
                                   data[sn_offset + 2];
                sn_offset += 3;
                sn_remaining -= 3;

                if (name_len > sn_remaining) {
                    return ParseStatus::NotMatched;
                }

                if (name_type == 0) {
                    // This is a host_name entry
                    std::string name(reinterpret_cast<const char*>(data) + sn_offset, name_len);
                    if (!validate_and_lowercase(name)) {
                        return ParseStatus::NotMatched;
                    }
                    sni_out = name;
                    return ParseStatus::Found;
                }

                sn_offset += name_len;
                sn_remaining -= name_len;
            }

            return ParseStatus::NotMatched;  // No host_name found in server_name_list
        }

        ext_offset += ext_len;
        ext_remaining -= ext_len;
    }

    // Extensions list is incomplete, check if we reached the end
    if (body_size < extensions_len) {
        // Not all extensions received, and we didn't find SNI
        return ParseStatus::NeedMore;
    }

    return ParseStatus::NotMatched;  // No server_name extension found
}

ParseStatus tls_stream_sni(ByteView stream, std::string& sni_out, std::vector<uint8_t>& scratch) {
    const uint8_t* data = stream.data();
    std::size_t size = stream.size();
    scratch.clear();

    if (size == 0) {
        return ParseStatus::NeedMore;
    }

    // First byte must be TLS record type 22 (Handshake)
    if (data[0] != 22) {
        return ParseStatus::NotMatched;
    }

    std::size_t offset = 0;

    while (offset < size) {
        // Need at least TLS record header (5 bytes)
        if (offset + 5 > size) {
            // Incomplete record header
            if (scratch.empty()) {
                return ParseStatus::NeedMore;
            }
            break;
        }

        uint8_t content_type = data[offset];
        if (content_type != 22) {
            // Not a Handshake record
            if (scratch.empty()) {
                return ParseStatus::NotMatched;
            }
            break;
        }

        // Legacy record version major must be 3
        uint8_t version_major = data[offset + 1];
        if (version_major != 3) {
            if (scratch.empty()) {
                return ParseStatus::NotMatched;
            }
            break;
        }

        uint16_t record_length = (static_cast<uint16_t>(data[offset + 3]) << 8) | data[offset + 4];

        // Validate record length
        if (record_length > 16384 + 256) {
            return ParseStatus::NotMatched;
        }

        // Need the complete record
        if (offset + 5 + record_length > size) {
            // Incomplete record, try to parse what we have
            if (offset + 5 <= size) {
                // Append what we have to scratch
                std::size_t available = size - (offset + 5);
                scratch.insert(scratch.end(), data + offset + 5, data + offset + 5 + available);
            }
            break;
        }

        // Append record payload to scratch
        scratch.insert(scratch.end(), data + offset + 5, data + offset + 5 + record_length);
        offset += 5 + record_length;

        // Try to parse ClientHello from scratch buffer
        ByteView handshake_view(scratch.data(), scratch.size());
        ParseStatus status = client_hello_sni(handshake_view, sni_out);
        if (status == ParseStatus::Found) {
            return ParseStatus::Found;
        }
        if (status == ParseStatus::NotMatched) {
            return ParseStatus::NotMatched;
        }
        // If NeedMore, continue collecting more records
    }

    // Try one more time with accumulated scratch
    if (!scratch.empty()) {
        ByteView handshake_view(scratch.data(), scratch.size());
        return client_hello_sni(handshake_view, sni_out);
    }

    return ParseStatus::NeedMore;
}

}  // namespace keen_pbr3::l7
