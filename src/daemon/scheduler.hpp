#pragma once

#include <chrono>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace keen_pbr3 {

class Daemon; // forward declaration

class SchedulerError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

using TaskCallback = std::function<void()>;

// Timerfd-based periodic task scheduler that integrates with the
// Daemon epoll event loop via add_fd/remove_fd.
class Scheduler {
public:
    // Takes a reference to the Daemon for fd registration.
    // Caller must ensure Daemon outlives this Scheduler.
    explicit Scheduler(Daemon& daemon);
    ~Scheduler();

    // Non-copyable, non-movable
    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;
    Scheduler(Scheduler&&) = delete;
    Scheduler& operator=(Scheduler&&) = delete;

    // Schedule a repeating task. Returns a task ID for later cancellation.
    // The first invocation fires after `interval` elapses.
    int schedule_repeating(std::chrono::seconds interval, TaskCallback cb);

    // Schedule a one-shot task. Returns a task ID for later cancellation.
    // The callback fires once after `delay` elapses.
    int schedule_oneshot(std::chrono::seconds delay, TaskCallback cb);

    // Cancel a previously scheduled task by ID.
    void cancel(int task_id);

    // Cancel all scheduled tasks.
    void cancel_all();

    // Number of active tasks.
    size_t size() const;

private:
    struct TimerEntry {
        int id;
        int timer_fd;
        TaskCallback callback;
        bool repeating;
    };

    int create_timerfd(std::chrono::seconds initial, std::chrono::seconds interval);
    void on_timer(int timer_fd, uint32_t events);
    void remove_entry(int timer_fd);

    Daemon& daemon_;
    std::vector<TimerEntry> entries_;
    mutable std::mutex entries_mutex_;
    int next_id_{1};
};

} // namespace keen_pbr3
