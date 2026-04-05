#pragma once

#include "../log/logger.hpp"

#include <chrono>
#include <cerrno>
#include <fcntl.h>
#include <signal.h>
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
};

inline void reset_child_signal_mask() {
    sigset_t empty_mask;
    sigemptyset(&empty_mask);
    sigprocmask(SIG_SETMASK, &empty_mask, nullptr);
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

// Execute a command with arguments directly via fork()+execvp(), bypassing
// the shell entirely. This prevents shell injection attacks.
// Returns the process exit code (0-255), or -1 on fork/exec failure.
inline int safe_exec(const std::vector<std::string>& args, bool suppress_output = false) {
    if (args.empty()) return -1;
    const std::string command = safe_exec_command_string(args);
    const auto started_at = std::chrono::steady_clock::now();
    Logger::instance().trace("safe_exec_start",
                             "cmd={} suppress_output={}",
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
        Logger::instance().trace("safe_exec_error",
                                 "cmd={} duration_ms={} reason=fork_failed errno={}",
                                 command,
                                 std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now() - started_at).count(),
                                 errno);
        return -1;
    }

    if (pid == 0) {
        // Child process
        reset_child_signal_mask();
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

    int status = 0;
    if (waitpid(pid, &status, 0) == -1) {
        Logger::instance().trace("safe_exec_error",
                                 "cmd={} duration_ms={} reason=waitpid_failed errno={}",
                                 command,
                                 std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now() - started_at).count(),
                                 errno);
        return -1;
    }
    const auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started_at).count();
    if (WIFEXITED(status)) {
        const int exit_code = WEXITSTATUS(status);
        Logger::instance().trace("safe_exec_end",
                                 "cmd={} exit_code={} duration_ms={}",
                                 command,
                                 exit_code,
                                 duration_ms);
        return exit_code;
    }
    Logger::instance().trace("safe_exec_error",
                             "cmd={} duration_ms={} reason=abnormal_exit",
                             command,
                             duration_ms);
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
        reset_child_signal_mask();
        close(pipefd[1]);
        dup2(pipefd[0], STDIN_FILENO);
        close(pipefd[0]);
        execvp(argv[0], const_cast<char* const*>(argv.data()));
        _exit(127);
    }

    // Parent: write input to pipe, then wait
    close(pipefd[0]);
    const char* data = input.data();
    size_t remaining = input.size();
    while (remaining > 0) {
        const ssize_t written = write(pipefd[1], data, remaining);
        if (written < 0) {
            if (errno == EINTR) continue;
            break;
        }
        data += written;
        remaining -= static_cast<size_t>(written);
    }
    close(pipefd[1]);

    int status = 0;
    if (waitpid(pid, &status, 0) == -1) {
        Logger::instance().trace("safe_exec_pipe_error",
                                 "cmd={} duration_ms={} reason=waitpid_failed errno={}",
                                 command,
                                 std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now() - started_at).count(),
                                 errno);
        return -1;
    }
    const auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started_at).count();
    if (WIFEXITED(status)) {
        const int exit_code = WEXITSTATUS(status);
        Logger::instance().trace("safe_exec_pipe_end",
                                 "cmd={} exit_code={} duration_ms={}",
                                 command,
                                 exit_code,
                                 duration_ms);
        return exit_code;
    }
    Logger::instance().trace("safe_exec_pipe_error",
                             "cmd={} duration_ms={} reason=abnormal_exit",
                             command,
                             duration_ms);
    return -1;
}

// Execute a command with arguments and capture its stdout output.
// Returns stdout, exit status and whether capture exceeded max_bytes.
inline ExecCaptureResult safe_exec_capture(const std::vector<std::string>& args,
                                           bool suppress_stderr = false,
                                           size_t max_bytes = 0) {
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
        reset_child_signal_mask();
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        if (suppress_stderr) {
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

    // Parent: read captured output, then wait
    close(pipefd[1]);
    char buf[4096];
    while (true) {
        const ssize_t n = read(pipefd[0], buf, sizeof(buf));
        if (n > 0) {
            result.stdout_output.append(buf, static_cast<size_t>(n));
            if (max_bytes > 0 && result.stdout_output.size() > max_bytes) {
                result.truncated = true;
                close(pipefd[0]);
                kill(pid, SIGTERM);
                int status = 0;
                waitpid(pid, &status, 0);
                if (WIFEXITED(status)) {
                    result.exit_code = WEXITSTATUS(status);
                }
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
            break;
        }
    }
    close(pipefd[0]);

    int status = 0;
    if (waitpid(pid, &status, 0) != -1 && WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
    }
    Logger::instance().trace("safe_exec_capture_end",
                             "cmd={} exit_code={} duration_ms={} bytes={} truncated={}",
                             command,
                             result.exit_code,
                             std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - started_at).count(),
                             result.stdout_output.size(),
                             result.truncated ? "true" : "false");
    return result;
}

} // namespace keen_pbr3
