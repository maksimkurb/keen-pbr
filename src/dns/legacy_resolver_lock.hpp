#pragma once

#include <mutex>

namespace keen_pbr3 {

// The legacy BIND resolver routines (res_query, res_mkquery, ...) operate on
// the process-global `_res` state and are not thread-safe. keen-pbr issues
// such queries from several threads — resolver health probes on the blocking
// executor and test-routing requests on API worker threads — so every call
// that touches `_res` must be serialized on this shared mutex.
//
// The res_n* (per-state) variants would avoid the lock, but they are absent
// on some target C libraries (musl). A shared mutex is portable, and the
// affected queries are infrequent diagnostics where serialization is harmless.
inline std::mutex& legacy_resolver_mutex() {
    static std::mutex mutex;
    return mutex;
}

} // namespace keen_pbr3
