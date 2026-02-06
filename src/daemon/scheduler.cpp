#include "scheduler.hpp"
#include "daemon.hpp"

#include <cerrno>
#include <cstring>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <unistd.h>

namespace keen_pbr3 {

Scheduler::Scheduler(Daemon& daemon) : daemon_(daemon) {}

Scheduler::~Scheduler() {
    // Best-effort cleanup: cancel all timers
    try {
        cancel_all();
    } catch (...) {
        // Suppress exceptions in destructor
    }
}

int Scheduler::create_timerfd(std::chrono::seconds initial, std::chrono::seconds interval) {
    int fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (fd < 0) {
        throw SchedulerError("timerfd_create failed: " + std::string(strerror(errno)));
    }

    struct itimerspec spec{};
    spec.it_value.tv_sec = initial.count();
    // interval of {0,0} means one-shot (no repeat)
    spec.it_interval.tv_sec = interval.count();

    if (timerfd_settime(fd, 0, &spec, nullptr) < 0) {
        close(fd);
        throw SchedulerError("timerfd_settime failed: " + std::string(strerror(errno)));
    }

    return fd;
}

int Scheduler::schedule_repeating(std::chrono::seconds interval, TaskCallback cb) {
    int fd = create_timerfd(interval, interval);
    int id = next_id_++;

    daemon_.add_fd(fd, EPOLLIN, [this, fd](uint32_t events) {
        on_timer(fd, events);
    });

    entries_.push_back({id, fd, std::move(cb), true});
    return id;
}

int Scheduler::schedule_oneshot(std::chrono::seconds delay, TaskCallback cb) {
    int fd = create_timerfd(delay, std::chrono::seconds{0});
    int id = next_id_++;

    daemon_.add_fd(fd, EPOLLIN, [this, fd](uint32_t events) {
        on_timer(fd, events);
    });

    entries_.push_back({id, fd, std::move(cb), false});
    return id;
}

void Scheduler::on_timer(int timer_fd, uint32_t /*events*/) {
    // Read the timerfd to acknowledge the expiration
    uint64_t expirations = 0;
    ssize_t n = read(timer_fd, &expirations, sizeof(expirations));
    if (n != sizeof(expirations)) {
        return;
    }

    // Find the entry and invoke its callback
    for (auto& entry : entries_) {
        if (entry.timer_fd == timer_fd) {
            TaskCallback cb = entry.callback; // copy before potential removal
            if (!entry.repeating) {
                // One-shot: remove after firing
                remove_entry(timer_fd);
            }
            cb();
            return;
        }
    }
}

void Scheduler::cancel(int task_id) {
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
        if (it->id == task_id) {
            int fd = it->timer_fd;
            daemon_.remove_fd(fd);
            close(fd);
            entries_.erase(it);
            return;
        }
    }
}

void Scheduler::cancel_all() {
    // Copy fds first since remove_fd modifies nothing we iterate,
    // but close + erase should be done carefully
    while (!entries_.empty()) {
        auto& entry = entries_.back();
        daemon_.remove_fd(entry.timer_fd);
        close(entry.timer_fd);
        entries_.pop_back();
    }
}

void Scheduler::remove_entry(int timer_fd) {
    daemon_.remove_fd(timer_fd);
    close(timer_fd);
    entries_.erase(
        std::remove_if(entries_.begin(), entries_.end(),
                       [timer_fd](const TimerEntry& e) { return e.timer_fd == timer_fd; }),
        entries_.end());
}

size_t Scheduler::size() const {
    return entries_.size();
}

} // namespace keen_pbr3
