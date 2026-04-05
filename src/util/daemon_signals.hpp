#pragma once

#include <pthread.h>
#include <signal.h>

#include <cstring>
#include <stdexcept>
#include <string>

namespace keen_pbr3 {

inline sigset_t daemon_signal_mask() {
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGUSR1);
    sigaddset(&mask, SIGHUP);
    return mask;
}

inline void block_daemon_signals_for_current_thread() {
    sigset_t mask = daemon_signal_mask();
    const int rc = pthread_sigmask(SIG_BLOCK, &mask, nullptr);
    if (rc != 0) {
        throw std::runtime_error("pthread_sigmask(SIG_BLOCK) failed: " + std::string(std::strerror(rc)));
    }
}

inline void unblock_daemon_signals_for_current_thread() {
    sigset_t mask = daemon_signal_mask();
    const int rc = pthread_sigmask(SIG_UNBLOCK, &mask, nullptr);
    if (rc != 0) {
        throw std::runtime_error("pthread_sigmask(SIG_UNBLOCK) failed: " + std::string(std::strerror(rc)));
    }
}

inline bool is_signal_blocked_for_current_thread(int signum) {
    sigset_t current_mask;
    const int rc = pthread_sigmask(SIG_BLOCK, nullptr, &current_mask);
    if (rc != 0) {
        throw std::runtime_error("pthread_sigmask(read mask) failed: " + std::string(std::strerror(rc)));
    }
    return sigismember(&current_mask, signum) == 1;
}

class ScopedDaemonSignalMask {
public:
    ScopedDaemonSignalMask() {
        block_daemon_signals_for_current_thread();
    }

    ~ScopedDaemonSignalMask() {
        try {
            unblock_daemon_signals_for_current_thread();
        } catch (...) {
        }
    }

    ScopedDaemonSignalMask(const ScopedDaemonSignalMask&) = delete;
    ScopedDaemonSignalMask& operator=(const ScopedDaemonSignalMask&) = delete;
};

} // namespace keen_pbr3
