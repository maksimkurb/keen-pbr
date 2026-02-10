#pragma once

#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace keen_pbr3 {

class DaemonError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Callback for file descriptor events
using FdCallback = std::function<void(uint32_t events)>;

// Epoll-based daemon event loop with signal handling.
// Handles SIGUSR1, SIGTERM, SIGINT via signalfd.
class Daemon {
public:
    // Callback invoked on SIGUSR1 (immediate re-check trigger)
    using SignalCallback = std::function<void()>;

    Daemon();
    ~Daemon();

    // Non-copyable, non-movable
    Daemon(const Daemon&) = delete;
    Daemon& operator=(const Daemon&) = delete;
    Daemon(Daemon&&) = delete;
    Daemon& operator=(Daemon&&) = delete;

    // Set callback for SIGUSR1 signal
    void on_sigusr1(SignalCallback cb);

    // Set callback for SIGHUP signal (full config reload)
    void on_sighup(SignalCallback cb);

    // Register an additional file descriptor for epoll monitoring.
    // The callback will be invoked with the epoll events.
    // Returns the fd for later removal.
    void add_fd(int fd, uint32_t events, FdCallback cb);

    // Remove a previously registered file descriptor.
    void remove_fd(int fd);

    // Run the event loop. Blocks until stop() is called or
    // SIGTERM/SIGINT is received.
    void run();

    // Request the event loop to stop.
    void stop();

    // Returns true if the daemon is currently running.
    bool running() const;

private:
    void setup_signals();
    void handle_signal();

    int epoll_fd_{-1};
    int signal_fd_{-1};
    bool running_{false};

    SignalCallback sigusr1_cb_;
    SignalCallback sighup_cb_;

    struct FdEntry {
        int fd;
        FdCallback callback;
    };
    std::vector<FdEntry> fd_entries_;
};

} // namespace keen_pbr3
