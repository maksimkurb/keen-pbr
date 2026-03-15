#include "scheduler.hpp"
#include <algorithm>
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
    int id = 0;
    {
        std::lock_guard<std::mutex> lock(entries_mutex_);
        id = next_id_++;
    }

    daemon_.add_fd(fd, EPOLLIN, [this, fd](uint32_t events) {
        on_timer(fd, events);
    });

    {
        std::lock_guard<std::mutex> lock(entries_mutex_);
        entries_.push_back({id, fd, std::move(cb), true});
    }
    return id;
}

int Scheduler::schedule_oneshot(std::chrono::seconds delay, TaskCallback cb) {
    int fd = create_timerfd(delay, std::chrono::seconds{0});
    int id = 0;
    {
        std::lock_guard<std::mutex> lock(entries_mutex_);
        id = next_id_++;
    }

    daemon_.add_fd(fd, EPOLLIN, [this, fd](uint32_t events) {
        on_timer(fd, events);
    });

    {
        std::lock_guard<std::mutex> lock(entries_mutex_);
        entries_.push_back({id, fd, std::move(cb), false});
    }
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
    bool repeating = false;
    TaskCallback cb;
    {
        std::lock_guard<std::mutex> lock(entries_mutex_);
        for (const auto& entry : entries_) {
            if (entry.timer_fd == timer_fd) {
                cb = entry.callback;
                repeating = entry.repeating;
                break;
            }
        }
    }
    if (!cb) {
        return;
    }
    if (!repeating) {
        // One-shot: remove after firing
        remove_entry(timer_fd);
    }
    cb();
}

void Scheduler::cancel(int task_id) {
    int fd = -1;
    {
        std::lock_guard<std::mutex> lock(entries_mutex_);
        for (auto it = entries_.begin(); it != entries_.end(); ++it) {
            if (it->id == task_id) {
                fd = it->timer_fd;
                entries_.erase(it);
                break;
            }
        }
    }
    if (fd >= 0) {
        daemon_.remove_fd(fd);
        close(fd);
    }
}

void Scheduler::cancel_all() {
    std::vector<int> timer_fds;
    {
        std::lock_guard<std::mutex> lock(entries_mutex_);
        timer_fds.reserve(entries_.size());
        for (const auto& entry : entries_) {
            timer_fds.push_back(entry.timer_fd);
        }
        entries_.clear();
    }

    for (int timer_fd : timer_fds) {
        daemon_.remove_fd(timer_fd);
        close(timer_fd);
    }
}

void Scheduler::remove_entry(int timer_fd) {
    {
        std::lock_guard<std::mutex> lock(entries_mutex_);
        entries_.erase(
            std::remove_if(entries_.begin(), entries_.end(),
                           [timer_fd](const TimerEntry& e) { return e.timer_fd == timer_fd; }),
            entries_.end());
    }
    daemon_.remove_fd(timer_fd);
    close(timer_fd);
}

size_t Scheduler::size() const {
    std::lock_guard<std::mutex> lock(entries_mutex_);
    return entries_.size();
}

} // namespace keen_pbr3
