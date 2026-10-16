#pragma once

#include <string>

#include "../util/byte_view.hpp"
#include "l7_status.hpp"

namespace keen_pbr3::l7 {

// Parses an HTTP request and extracts the Host header.
// `payload` = start of client→server TCP stream.
// Must start with one of: "GET ", "POST ", "HEAD ", "PUT ", "DELETE ", "OPTIONS ", "PATCH ", "CONNECT "
// else NotMatched.
// Finds the "Host:" header (case-insensitive header name, at line start, after the request line),
// trims spaces/tabs, strips ":port" (also for "[v6]:port" → NotMatched for IP literals;
// a bare IPv4 literal → NotMatched too), lowercases, strips trailing dot.
// If headers end ("\r\n\r\n") without Host → NotMatched.
// If buffer ends before Host and before end of headers → NeedMore.
// Limit search to first 4096 bytes (beyond → NotMatched).
ParseStatus http_host(ByteView payload, std::string& host_out);

} // namespace keen_pbr3::l7
