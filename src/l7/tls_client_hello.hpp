#pragma once

#include <string>
#include <vector>

#include "../util/byte_view.hpp"
#include "l7_status.hpp"

namespace keen_pbr3::l7 {

// Parses a TLS ClientHello handshake record and extracts the SNI.
// `handshake` starts at the Handshake header (msg_type=1 ClientHello, 3-byte length).
// On Found, `sni_out` is set to the first host_name (type 0) entry of the server_name extension
// (0x0000), lowercased, with trailing dot stripped.
// Validates: name length 1..253, chars [a-z0-9.-_] after lowercasing, else NotMatched.
ParseStatus client_hello_sni(ByteView handshake, std::string& sni_out);

// Parses a TCP byte stream (client→server) for TLS and extracts the SNI.
// `stream` is the beginning of a TCP byte stream (client→server).
// Parses TLS records (content type 22, legacy record version major 3, record length ≤ 16384+256),
// concatenating record fragments of the handshake layer into a scratch buffer.
// ClientHello may span several records.
// Returns NeedMore when the stream ends before the ClientHello is complete.
// First byte != 22 → NotMatched.
// On Found, `sni_out` contains the extracted SNI.
ParseStatus tls_stream_sni(ByteView stream, std::string& sni_out, std::vector<uint8_t>& scratch);

} // namespace keen_pbr3::l7
