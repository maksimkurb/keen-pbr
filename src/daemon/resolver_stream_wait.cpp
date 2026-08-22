#include "resolver_stream_wait.hpp"

#include <limits>
#include <thread>

namespace keen_pbr3 {

namespace {

constexpr auto kResolverStreamPollInterval = std::chrono::milliseconds{10};

std::chrono::steady_clock::duration saturating_steady_duration(
    std::chrono::seconds timeout) noexcept {
    using ClockDuration = std::chrono::steady_clock::duration;
    using Conversion = std::ratio_divide<
        std::chrono::seconds::period, ClockDuration::period>;
    static_assert(Conversion::num > 0 && Conversion::den > 0);

    if (timeout <= std::chrono::seconds::zero()) {
        return ClockDuration::zero();
    }

    using Unsigned = std::uintmax_t;
    const Unsigned seconds = static_cast<Unsigned>(timeout.count());
    const Unsigned numerator = static_cast<Unsigned>(Conversion::num);
    const Unsigned denominator = static_cast<Unsigned>(Conversion::den);
    const Unsigned max_ticks = static_cast<Unsigned>(
        std::numeric_limits<ClockDuration::rep>::max());
    const Unsigned whole_seconds = seconds / denominator;
    if (whole_seconds > max_ticks / numerator) {
        return ClockDuration::max();
    }

    Unsigned ticks = whole_seconds * numerator;
    const Unsigned remainder = seconds % denominator;
    if (remainder > std::numeric_limits<Unsigned>::max() / numerator) {
        return ClockDuration::max();
    }
    const Unsigned fractional_ticks = remainder * numerator / denominator;
    if (ticks > max_ticks - fractional_ticks) {
        return ClockDuration::max();
    }
    ticks += fractional_ticks;
    return ClockDuration{static_cast<ClockDuration::rep>(ticks)};
}

} // namespace

bool wait_for_resolver_stream_after(
    std::uint64_t baseline,
    std::chrono::seconds timeout,
    const ResolverStreamCompletionCount& completion_count,
    const ResolverStreamEventPump& pump_events,
    const ResolverStreamWait& wait,
    const ResolverStreamNow& now) {
    const auto started_at = now();
    const auto timeout_duration = saturating_steady_duration(timeout);
    const auto max_deadline = std::chrono::steady_clock::time_point::max();
    const auto deadline = timeout_duration > max_deadline - started_at
        ? max_deadline
        : started_at + timeout_duration;
    while (now() < deadline) {
        if (completion_count() > baseline) {
            return true;
        }
        pump_events();
        if (completion_count() > baseline) {
            return true;
        }
        wait(kResolverStreamPollInterval);
    }
    return completion_count() > baseline;
}

void wait_for_resolver_stream_poll(std::chrono::milliseconds duration) {
    std::this_thread::sleep_for(duration);
}

std::chrono::steady_clock::time_point resolver_stream_now() {
    return std::chrono::steady_clock::now();
}

} // namespace keen_pbr3
