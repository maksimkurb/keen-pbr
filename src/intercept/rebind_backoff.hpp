#pragma once

#include <algorithm>
#include <chrono>
#include <optional>

namespace keen_pbr3 {

// Retry schedule for re-binding a listener that died at runtime: the first
// attempt comes 1 s after the failure was noticed, every further one waits
// twice as long (capped at 60 s).  The schedule only restarts once the
// listener has stayed healthy for a full stability window, so a bind that
// succeeds and dies again immediately keeps backing off.  Pure bookkeeping:
// the caller owns the clock and does the actual re-bind.
class RebindBackoff {
public:
    using Clock = std::chrono::steady_clock;
    static constexpr std::chrono::seconds kInitial{1};
    static constexpr std::chrono::seconds kMax{60};
    static constexpr std::chrono::seconds kStable{60};

    // Called on every tick with the current listener health.
    void observe(bool healthy, Clock::time_point now) {
        if (!healthy) {
            healthy_since_.reset();
            if (!pending_) {
                pending_ = true;
                next_due_ = now + delay_for(attempts_);
            }
            return;
        }
        if (!pending_ && attempts_ == 0) return;
        pending_ = false;
        if (!healthy_since_) healthy_since_ = now;
        if (now - *healthy_since_ >= kStable) reset();
    }

    // A re-bind attempt is due while the listener is unhealthy and its
    // delay has elapsed.
    bool due(Clock::time_point now) const { return pending_ && now >= next_due_; }

    // Records an attempt (successful or not) and schedules the next one.
    void attempted(Clock::time_point now) {
        ++attempts_;
        next_due_ = now + delay_for(attempts_);
    }

    // An explicit apply took over the listeners.
    void reset() {
        pending_ = false;
        attempts_ = 0;
        healthy_since_.reset();
    }

    bool pending() const { return pending_; }
    unsigned attempts() const { return attempts_; }
    Clock::time_point next_due() const { return next_due_; }

    static std::chrono::seconds delay_for(unsigned attempts) {
        auto delay = kInitial;
        for (unsigned i = 0; i < attempts && delay < kMax; ++i) delay *= 2;
        return std::min(delay, kMax);
    }

private:
    bool pending_{false};
    unsigned attempts_{0};
    Clock::time_point next_due_{};
    std::optional<Clock::time_point> healthy_since_;
};

} // namespace keen_pbr3
