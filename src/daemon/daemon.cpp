#include "daemon.hpp"

#include <cerrno>
#include <cstring>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <unistd.h>

#include "../firewall/firewall.hpp"
#include "scheduler.hpp"
#include "../routing/urltest_manager.hpp"

#ifdef WITH_API
#include "../api/server.hpp"
#endif

namespace keen_pbr3 {

// Helper to get tag from any outbound variant
std::string get_outbound_tag(const Outbound& ob) {
    return std::visit([](const auto& o) -> std::string { return o.tag; }, ob);
}

// Find an outbound by tag, returning pointer or nullptr
const Outbound* find_outbound(const std::vector<Outbound>& outbounds,
                               const std::string& tag) {
    for (const auto& ob : outbounds) {
        if (get_outbound_tag(ob) == tag) {
            return &ob;
        }
    }
    return nullptr;
}

Daemon::Daemon(Config config, std::string config_path, DaemonOptions opts)
    : config_(std::move(config))
    , config_path_(std::move(config_path))
    , opts_(std::move(opts))
    , cache_(config_.daemon.cache_dir)
    , firewall_(create_firewall("auto"))
    , netlink_()
    , route_table_(netlink_)
    , policy_rules_(netlink_)
    , firewall_state_()
    , url_tester_()
    , outbound_marks_(allocate_outbound_marks(config_.fwmark, config_.outbounds))
{
    // Initialize epoll
    epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd_ < 0) {
        throw DaemonError("epoll_create1 failed: " + std::string(strerror(errno)));
    }

    setup_signals();

    // Set outbound marks in firewall state
    firewall_state_.set_outbound_marks(outbound_marks_);

    // Ensure cache directory exists
    cache_.ensure_dir();

    // Create scheduler (needs Daemon& for epoll fd registration)
    scheduler_ = std::make_unique<Scheduler>(*this);

    // UrltestManager created later during startup (register_urltest_outbounds)
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
        handle_sigusr1();
        break;
    case SIGHUP:
        handle_sighup();
        break;
    default:
        break;
    }
}

void Daemon::handle_sigusr1() {
    // Stub — will be implemented in US-059
}

void Daemon::handle_sighup() {
    // Stub — will be implemented in US-059
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

// Stubs for business logic — will be implemented in US-058
void Daemon::setup_static_routing() {}
void Daemon::apply_firewall() {}
void Daemon::download_uncached_lists() {}
void Daemon::register_urltest_outbounds() {}
void Daemon::full_reload() {}
void Daemon::write_pid_file() {}
void Daemon::remove_pid_file() {}

#ifdef WITH_API
void Daemon::setup_api() {}
#endif

} // namespace keen_pbr3
