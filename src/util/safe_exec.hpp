#pragma once

#include "../log/logger.hpp"

#include <algorithm>
#include <chrono>
#include <atomic>
#include <cstdint>
#include <cerrno>
#include <fcntl.h>
#include <signal.h>
#include <poll.h>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace keen_pbr3 {

struct ExecCaptureResult {
    std::string stdout_output;
    int exit_code{-1};
    bool truncated{false};
    bool timed_out{false};
};

struct SafeExecTimeouts {
    std::chrono::milliseconds timeout{std::chrono::seconds{30}};
    std::chrono::milliseconds kill_grace{std::chrono::seconds{2}};
};

inline std::atomic<std::int64_t>& safe_exec_timeout_ms_storage() {
    static std::atomic<std::int64_t> value{30000};
    return value;
}

inline std::atomic<std::int64_t>& safe_exec_kill_grace_ms_storage() {
    static std::atomic<std::int64_t> value{2000};
    return value;
}

inline void set_safe_exec_timeouts(std::chrono::milliseconds timeout,
                                   std::chrono::milliseconds kill_grace) {
    safe_exec_timeout_ms_storage().store(std::max<std::int64_t>(1, timeout.count()),
                                         std::memory_order_release);
    safe_exec_kill_grace_ms_storage().store(std::max<std::int64_t>(0, kill_grace.count()),
                                            std::memory_order_release);
}

inline SafeExecTimeouts safe_exec_timeouts() {
    return {
        std::chrono::milliseconds{safe_exec_timeout_ms_storage().load(std::memory_order_acquire)},
        std::chrono::milliseconds{safe_exec_kill_grace_ms_storage().load(std::memory_order_acquire)},
    };
}

struct ChildWaitResult {
    int status{0};
    bool reaped{false};
    bool timed_out{false};
};

inline void prepare_child_process_group() {
    (void)setpgid(0, 0);
}

inline void prepare_parent_process_group(pid_t pid) {
    (void)setpgid(pid, pid);
}

inline void signal_child_process_group(pid_t pid, int signal_number) {
    if (kill(-pid, signal_number) != 0 && errno == ESRCH) {
        (void)kill(pid, signal_number);
    }
}

inline ChildWaitResult wait_for_child_until(
    pid_t pid,
    std::chrono::steady_clock::time_point deadline,
    std::chrono::milliseconds kill_grace) {
    ChildWaitResult result;
    auto wait_until = [&](std::chrono::steady_clock::time_point until) {
        while (true) {
            const pid_t waited = waitpid(pid, &result.status, WNOHANG);
            if (waited == pid) {
                result.reaped = true;
                return true;
            }
            if (waited < 0 && errno != EINTR) return true;
            const auto now = std::chrono::steady_clock::now();
            if (now >= until) return false;
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(until - now);
            const int delay = static_cast<int>(std::max<std::int64_t>(1,
                std::min<std::int64_t>(20, remaining.count())));
            (void)poll(nullptr, 0, delay);
        }
    };

    if (wait_until(deadline)) return result;
    result.timed_out = true;
    signal_child_process_group(pid, SIGTERM);
    if (wait_until(std::chrono::steady_clock::now() + kill_grace)) {
        // The direct child may exit while descendants keep running.
        signal_child_process_group(pid, SIGKILL);
        return result;
    }
    signal_child_process_group(pid, SIGKILL);
    while (waitpid(pid, &result.status, 0) < 0) {
        if (errno != EINTR) return result;
    }
    result.reaped = true;
    return result;
}

inline int child_exit_code(const ChildWaitResult& wait_result) {
    if (!wait_result.reaped || wait_result.timed_out || !WIFEXITED(wait_result.status)) return -1;
    return WEXITSTATUS(wait_result.status);
}

inline void reset_child_signal_mask() {
    sigset_t empty_mask;
    sigemptyset(&empty_mask);
    sigprocmask(SIG_SETMASK, &empty_mask, nullptr);
}

inline bool redirect_child_stdin_to_devnull() {
    const int devnull = open("/dev/null", O_RDONLY);
    if (devnull < 0) {
        return false;
    }
    if (dup2(devnull, STDIN_FILENO) < 0) {
        close(devnull);
        return false;
    }
    if (devnull != STDIN_FILENO) {
        close(devnull);
    }
    return true;
}

inline std::string safe_exec_command_string(const std::vector<std::string>& args) {
    std::ostringstream out;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i != 0) {
            out << ' ';
        }
        out << args[i];
    }
    return out.str();
}

inline void log_failed_pipe_input(const std::string& command, const std::string& input) {
    constexpr std::size_t max_preview_bytes = 4096;
    const bool truncated = input.size() > max_preview_bytes;
    const std::string preview = input.substr(0, std::min(input.size(), max_preview_bytes));
    Logger::instance().error(
        "safe_exec_pipe_input cmd={} input_bytes={} preview_bytes={} truncated={}:\n{}",
        command,
        input.size(),
        preview.size(),
        truncated ? "true" : "false",
        preview);
}

// Execute a command with arguments directly via fork()+execvp(), bypassing
// the shell entirely. This prevents shell injection attacks.
// Returns the process exit code (0-255), or -1 on fork/exec failure.
inline int safe_exec(const std::vector<std::string>& args, bool suppress_output = false) {
    if (args.empty()) return -1;
    const std::string command = safe_exec_command_string(args);
    const auto started_at = std::chrono::steady_clock::now();
    Logger::instance().debug("safe_exec_start cmd={} suppress_output={}",
                             command,
                             suppress_output ? "true" : "false");

    std::vector<const char*> argv;
    argv.reserve(args.size() + 1);
    for (const auto& arg : args) {
        argv.push_back(arg.c_str());
    }
    argv.push_back(nullptr);

    const pid_t pid = fork();
    if (pid == -1) {
        Logger::instance().verbose("safe_exec_error cmd={} duration_ms={} reason=fork_failed errno={}",
                                 command,
                                 std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now() - started_at).count(),
                                 errno);
        return -1;
    }

    if (pid == 0) {
        // Child process
        prepare_child_process_group();
        reset_child_signal_mask();
        if (!redirect_child_stdin_to_devnull()) {
            _exit(127);
        }
        if (suppress_output) {
            const int devnull = open("/dev/null", O_WRONLY);
            if (devnull >= 0) {
                dup2(devnull, STDOUT_FILENO);
                dup2(devnull, STDERR_FILENO);
                close(devnull);
            }
        }
        execvp(argv[0], const_cast<char* const*>(argv.data()));
        _exit(127); // execvp failed
    }
    prepare_parent_process_group(pid);
    const auto timeouts = safe_exec_timeouts();
    const auto wait_result = wait_for_child_until(pid, started_at + timeouts.timeout,
                                                  timeouts.kill_grace);
    const auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started_at).count();
    if (!wait_result.timed_out && wait_result.reaped && WIFEXITED(wait_result.status)) {
        const int exit_code = WEXITSTATUS(wait_result.status);
        Logger::instance().trace("safe_exec_end",
                                 "cmd={} exit_code={} duration_ms={}",
                                 command,
                                 exit_code,
                                 duration_ms);
        return exit_code;
    }
    Logger::instance().verbose("safe_exec_error",
                                    "cmd={} duration_ms={} reason={}",
                                    command,
                                    duration_ms,
                                    wait_result.timed_out ? "timeout" : "abnormal_exit");
    return -1;
}

// Execute a command with arguments, piping input data to its stdin.
// Returns the process exit code (0-255), or -1 on fork/exec/pipe failure.
inline int safe_exec_pipe_stdin(const std::vector<std::string>& args,
                                const std::string& input) {
    if (args.empty()) return -1;
    const std::string command = safe_exec_command_string(args);
    const auto started_at = std::chrono::steady_clock::now();
    Logger::instance().trace("safe_exec_pipe_start",
                             "cmd={} input_bytes={}",
                             command,
                             input.size());

    std::vector<const char*> argv;
    argv.reserve(args.size() + 1);
    for (const auto& arg : args) {
        argv.push_back(arg.c_str());
    }
    argv.push_back(nullptr);

    int pipefd[2];
    if (pipe2(pipefd, O_CLOEXEC) == -1) {
        Logger::instance().trace("safe_exec_pipe_error",
                                 "cmd={} duration_ms={} reason=pipe_failed errno={}",
                                 command,
                                 std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now() - started_at).count(),
                                 errno);
        return -1;
    }

    const pid_t pid = fork();
    if (pid == -1) {
        close(pipefd[0]);
        close(pipefd[1]);
        Logger::instance().trace("safe_exec_pipe_error",
                                 "cmd={} duration_ms={} reason=fork_failed errno={}",
                                 command,
                                 std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now() - started_at).count(),
                                 errno);
        return -1;
    }

    if (pid == 0) {
        // Child: read end becomes stdin
        prepare_child_process_group();
        reset_child_signal_mask();
        close(pipefd[1]);
        dup2(pipefd[0], STDIN_FILENO);
        close(pipefd[0]);
        execvp(argv[0], const_cast<char* const*>(argv.data()));
        _exit(127);
    }

    prepare_parent_process_group(pid);
    const auto timeouts = safe_exec_timeouts();
    const auto deadline = started_at + timeouts.timeout;

    // Parent: write input without allowing a child that stops reading to block
    // past the execution deadline.
    close(pipefd[0]);
    const int current_flags = fcntl(pipefd[1], F_GETFL, 0);
    if (current_flags >= 0) (void)fcntl(pipefd[1], F_SETFL, current_flags | O_NONBLOCK);
    const char* data = input.data();
    size_t remaining = input.size();
    while (remaining > 0) {
        if (std::chrono::steady_clock::now() >= deadline) break;
        const ssize_t written = write(pipefd[1], data, remaining);
        if (written < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                const auto now = std::chrono::steady_clock::now();
                if (now >= deadline) break;
                const auto wait_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    deadline - now);
                pollfd descriptor{pipefd[1], POLLOUT, 0};
                (void)poll(&descriptor, 1, static_cast<int>(
                    std::max<std::int64_t>(1, std::min<std::int64_t>(50, wait_ms.count()))));
                continue;
            }
            break;
        }
        data += written;
        remaining -= static_cast<size_t>(written);
    }
    close(pipefd[1]);

    const auto wait_result = wait_for_child_until(pid, deadline, timeouts.kill_grace);
    const auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started_at).count();
    if (!wait_result.timed_out && wait_result.reaped && WIFEXITED(wait_result.status)) {
        const int exit_code = WEXITSTATUS(wait_result.status);
        if (exit_code == 0) {
            Logger::instance().trace("safe_exec_pipe_end",
                                     "cmd={} exit_code={} duration_ms={}",
                                     command,
                                     exit_code,
                                     duration_ms);
        } else {
            Logger::instance().error("safe_exec_pipe_failed cmd={} exit_code={} duration_ms={}",
                                     command,
                                     exit_code,
                                     duration_ms);
            log_failed_pipe_input(command, input);
        }
        return exit_code;
    }
    Logger::instance().error("safe_exec_pipe_failed cmd={} duration_ms={} reason={}",
                             command,
                             duration_ms,
                             wait_result.timed_out ? "timeout" : "abnormal_exit");
    log_failed_pipe_input(command, input);
    return -1;
}

// Execute a command with arguments and capture its stdout output. When
// merge_stderr is true, stderr is captured into stdout_output as well.
// Returns output, exit status and whether capture exceeded max_bytes.
inline ExecCaptureResult safe_exec_capture(const std::vector<std::string>& args,
                                           bool suppress_stderr = false,
                                           size_t max_bytes = 0,
                                           bool merge_stderr = false) {
    ExecCaptureResult result;
    if (args.empty()) return result;
    const std::string command = safe_exec_command_string(args);
    const auto started_at = std::chrono::steady_clock::now();
    Logger::instance().trace("safe_exec_capture_start",
                             "cmd={} suppress_stderr={} max_bytes={}",
                             command,
                             suppress_stderr ? "true" : "false",
                             max_bytes);

    std::vector<const char*> argv;
    argv.reserve(args.size() + 1);
    for (const auto& arg : args) {
        argv.push_back(arg.c_str());
    }
    argv.push_back(nullptr);

    int pipefd[2];
    if (pipe2(pipefd, O_CLOEXEC) == -1) {
        Logger::instance().trace("safe_exec_capture_error",
                                 "cmd={} duration_ms={} reason=pipe_failed errno={}",
                                 command,
                                 std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now() - started_at).count(),
                                 errno);
        return result;
    }

    const pid_t pid = fork();
    if (pid == -1) {
        close(pipefd[0]);
        close(pipefd[1]);
        Logger::instance().trace("safe_exec_capture_error",
                                 "cmd={} duration_ms={} reason=fork_failed errno={}",
                                 command,
                                 std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now() - started_at).count(),
                                 errno);
        return result;
    }

    if (pid == 0) {
        // Child: write end becomes stdout
        prepare_child_process_group();
        reset_child_signal_mask();
        if (!redirect_child_stdin_to_devnull()) {
            _exit(127);
        }
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        if (merge_stderr) {
            dup2(pipefd[1], STDERR_FILENO);
        } else if (suppress_stderr) {
            const int devnull = open("/dev/null", O_WRONLY);
            if (devnull >= 0) {
                dup2(devnull, STDERR_FILENO);
                close(devnull);
            }
        }
        close(pipefd[1]);
        execvp(argv[0], const_cast<char* const*>(argv.data()));
        _exit(127);
    }

    prepare_parent_process_group(pid);
    const auto timeouts = safe_exec_timeouts();
    const auto deadline = started_at + timeouts.timeout;

    // Parent: read captured output without allowing a silent or stalled child
    // to hold the read indefinitely.
    close(pipefd[1]);
    const int current_flags = fcntl(pipefd[0], F_GETFL, 0);
    if (current_flags >= 0) (void)fcntl(pipefd[0], F_SETFL, current_flags | O_NONBLOCK);
    char buf[4096];
    while (true) {
        if (std::chrono::steady_clock::now() >= deadline) {
            result.timed_out = true;
            break;
        }
        const ssize_t n = read(pipefd[0], buf, sizeof(buf));
        if (n > 0) {
            result.stdout_output.append(buf, static_cast<size_t>(n));
            if (max_bytes > 0 && result.stdout_output.size() > max_bytes) {
                result.truncated = true;
                close(pipefd[0]);
                const auto wait_result = wait_for_child_until(
                    pid, std::chrono::steady_clock::now(), timeouts.kill_grace);
                result.exit_code = child_exit_code(wait_result);
                Logger::instance().trace("safe_exec_capture_end",
                                         "cmd={} exit_code={} duration_ms={} bytes={} truncated=true",
                                         command,
                                         result.exit_code,
                                         std::chrono::duration_cast<std::chrono::milliseconds>(
                                             std::chrono::steady_clock::now() - started_at).count(),
                                         result.stdout_output.size());
                return result;
            }
        } else if (n == 0) {
            break;
        } else {
            if (errno == EINTR) continue;
            if (errno != EAGAIN && errno != EWOULDBLOCK) break;
            const auto now = std::chrono::steady_clock::now();
            if (now >= deadline) {
                result.timed_out = true;
                break;
            }
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - now);
            pollfd descriptor{pipefd[0], POLLIN | POLLHUP, 0};
            (void)poll(&descriptor, 1, static_cast<int>(
                std::max<std::int64_t>(1, std::min<std::int64_t>(50, remaining.count()))));
        }
    }
    close(pipefd[0]);

    const auto wait_result = wait_for_child_until(
        pid,
        result.timed_out ? std::chrono::steady_clock::now() : deadline,
        timeouts.kill_grace);
    result.timed_out = result.timed_out || wait_result.timed_out;
    result.exit_code = child_exit_code(wait_result);
    Logger::instance().trace("safe_exec_capture_end",
                             "cmd={} exit_code={} duration_ms={} bytes={} truncated={} timed_out={}",
                             command,
                             result.exit_code,
                             std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - started_at).count(),
                             result.stdout_output.size(),
                             result.truncated ? "true" : "false",
                             result.timed_out ? "true" : "false");
    return result;
}

} // namespace keen_pbr3
