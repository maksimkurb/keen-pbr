#include "daemon.hpp"

#include <cerrno>
#include <cstring>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <unistd.h>

namespace keen_pbr3 {

Daemon::Daemon() {
    epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd_ < 0) {
        throw DaemonError("epoll_create1 failed: " + std::string(strerror(errno)));
    }

    setup_signals();
}

Daemon::~Daemon() {
    if (signal_fd_ >= 0) {
        // Remove from epoll before closing
        epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, signal_fd_, nullptr);
        close(signal_fd_);
    }
    if (epoll_fd_ >= 0) {
        close(epoll_fd_);
    }

    // Restore default signal disposition
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGUSR1);
    sigaddset(&mask, SIGHUP);
    sigprocmask(SIG_UNBLOCK, &mask, nullptr);
}

void Daemon::setup_signals() {
    // Block SIGTERM, SIGINT, SIGUSR1, SIGHUP so they can be handled via signalfd
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGUSR1);
    sigaddset(&mask, SIGHUP);

    if (sigprocmask(SIG_BLOCK, &mask, nullptr) < 0) {
        throw DaemonError("sigprocmask failed: " + std::string(strerror(errno)));
    }

    // Create signalfd for the blocked signals
    signal_fd_ = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
    if (signal_fd_ < 0) {
        throw DaemonError("signalfd failed: " + std::string(strerror(errno)));
    }

    // Add signalfd to epoll
    struct epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = signal_fd_;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, signal_fd_, &ev) < 0) {
        throw DaemonError("epoll_ctl add signalfd failed: " + std::string(strerror(errno)));
    }
}

void Daemon::handle_signal() {
    struct signalfd_siginfo info{};
    ssize_t n = read(signal_fd_, &info, sizeof(info));
    if (n != sizeof(info)) {
        return;
    }

    switch (info.ssi_signo) {
    case SIGTERM:
    case SIGINT:
        running_ = false;
        break;
    case SIGUSR1:
        if (sigusr1_cb_) {
            sigusr1_cb_();
        }
        break;
    case SIGHUP:
        if (sighup_cb_) {
            sighup_cb_();
        }
        break;
    default:
        break;
    }
}

void Daemon::on_sigusr1(SignalCallback cb) {
    sigusr1_cb_ = std::move(cb);
}

void Daemon::on_sighup(SignalCallback cb) {
    sighup_cb_ = std::move(cb);
}

void Daemon::add_fd(int fd, uint32_t events, FdCallback cb) {
    struct epoll_event ev{};
    ev.events = events;
    ev.data.fd = fd;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) < 0) {
        throw DaemonError("epoll_ctl add fd failed: " + std::string(strerror(errno)));
    }

    fd_entries_.push_back({fd, std::move(cb)});
}

void Daemon::remove_fd(int fd) {
    epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);

    fd_entries_.erase(
        std::remove_if(fd_entries_.begin(), fd_entries_.end(),
                       [fd](const FdEntry& e) { return e.fd == fd; }),
        fd_entries_.end());
}

void Daemon::run() {
    running_ = true;

    constexpr int MAX_EVENTS = 16;
    struct epoll_event events[MAX_EVENTS];

    while (running_) {
        int nfds = epoll_wait(epoll_fd_, events, MAX_EVENTS, -1);
        if (nfds < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw DaemonError("epoll_wait failed: " + std::string(strerror(errno)));
        }

        for (int i = 0; i < nfds; ++i) {
            int fd = events[i].data.fd;

            if (fd == signal_fd_) {
                handle_signal();
                continue;
            }

            // Dispatch to registered fd callbacks
            for (auto& entry : fd_entries_) {
                if (entry.fd == fd) {
                    entry.callback(events[i].events);
                    break;
                }
            }
        }
    }
}

void Daemon::stop() {
    running_ = false;
}

bool Daemon::running() const {
    return running_;
}

} // namespace keen_pbr3
