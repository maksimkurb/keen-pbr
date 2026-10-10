#pragma once

namespace keen_pbr3::l7 {

enum class ParseStatus {
    Found,       // Successfully found the target (e.g., SNI, Host)
    NeedMore,    // Looks like the protocol but data is truncated
    NotMatched   // Definitely not this protocol, malformed, or target not present
};

} // namespace keen_pbr3::l7
