#pragma once

#include <chrono>
#include <cstdint>
#include <functional>

namespace keen_pbr3 {

using ResolverStreamCompletionCount = std::function<std::uint64_t()>;
using ResolverStreamEventPump = std::function<void()>;
using ResolverStreamWait = std::function<void(std::chrono::milliseconds)>;
using ResolverStreamNow =
    std::function<std::chrono::steady_clock::time_point()>;

// Wait for a resolver config stream completion after the supplied baseline.
// The wait and clock callbacks are injectable so tests can advance time and
// completion deterministically without sleeping for the production interval.
bool wait_for_resolver_stream_after(
    std::uint64_t baseline,
    std::chrono::seconds timeout,
    const ResolverStreamCompletionCount& completion_count,
    const ResolverStreamEventPump& pump_events,
    const ResolverStreamWait& wait,
    const ResolverStreamNow& now);

void wait_for_resolver_stream_poll(std::chrono::milliseconds duration);
std::chrono::steady_clock::time_point resolver_stream_now();

} // namespace keen_pbr3
