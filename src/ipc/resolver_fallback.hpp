#pragma once

#include <cstdint>
#include <iosfwd>
#include <string>

namespace keen_pbr3::ipc {

// Stream a static resolver fallback without retaining its body in memory.
// Returns false when the file cannot be opened or the reason code is unsafe.
bool emit_resolver_fallback(std::ostream& output,
                            const std::string& fallback_path,
                            const std::string& reason_code,
                            std::int64_t timestamp);

} // namespace keen_pbr3::ipc
