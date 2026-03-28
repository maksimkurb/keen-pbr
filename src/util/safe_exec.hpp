#pragma once

#include <cerrno>
#include <signal.h>
#include <fcntl.h>
#include <string>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace keen_pbr3 {

// Execute a command with arguments directly via fork()+execvp(), bypassing
// the shell entirely. This prevents shell injection attacks.
// Returns the process exit code (0-255), or -1 on fork/exec failure.
inline int safe_exec(const std::vector<std::string>& args, bool suppress_output = false) {
    if (args.empty()) return -1;

    std::vector<const char*> argv;
    argv.reserve(args.size() + 1);
    for (const auto& arg : args) {
        argv.push_back(arg.c_str());
    }
    argv.push_back(nullptr);

    const pid_t pid = fork();
    if (pid == -1) return -1;

    if (pid == 0) {
        // Child process
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
    if (waitpid(pid, &status, 0) == -1) return -1;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return -1;
}

// Execute a command with arguments, piping input data to its stdin.
// Returns the process exit code (0-255), or -1 on fork/exec/pipe failure.
inline int safe_exec_pipe_stdin(const std::vector<std::string>& args,
                                const std::string& input) {
    if (args.empty()) return -1;

    std::vector<const char*> argv;
    argv.reserve(args.size() + 1);
    for (const auto& arg : args) {
        argv.push_back(arg.c_str());
    }
    argv.push_back(nullptr);

    int pipefd[2];
    if (pipe2(pipefd, O_CLOEXEC) == -1) return -1;

    const pid_t pid = fork();
    if (pid == -1) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    if (pid == 0) {
        // Child: read end becomes stdin
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
        const ssize_t written = send(pipefd[1], data, remaining, MSG_NOSIGNAL);
        if (written < 0) {
            if (errno == EINTR) continue;
            break;
        }
        data += written;
        remaining -= static_cast<size_t>(written);
    }
    close(pipefd[1]);

    int status = 0;
    if (waitpid(pid, &status, 0) == -1) return -1;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return -1;
}

// Execute a command with arguments and capture its stdout output.
// Returns the captured output string. Returns empty string on failure.
inline std::string safe_exec_capture(const std::vector<std::string>& args,
                                     bool suppress_stderr = false,
                                     size_t max_bytes = 0) {
    if (args.empty()) return {};

    std::vector<const char*> argv;
    argv.reserve(args.size() + 1);
    for (const auto& arg : args) {
        argv.push_back(arg.c_str());
    }
    argv.push_back(nullptr);

    int pipefd[2];
    if (pipe2(pipefd, O_CLOEXEC) == -1) return {};

    const pid_t pid = fork();
    if (pid == -1) {
        close(pipefd[0]);
        close(pipefd[1]);
        return {};
    }

    if (pid == 0) {
        // Child: write end becomes stdout
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
    std::string result;
    char buf[4096];
    while (true) {
        const ssize_t n = read(pipefd[0], buf, sizeof(buf));
        if (n > 0) {
            result.append(buf, static_cast<size_t>(n));
            if (max_bytes > 0 && result.size() > max_bytes) {
                close(pipefd[0]);
                kill(pid, SIGTERM);
                int status = 0;
                waitpid(pid, &status, 0);
                return {};
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
    waitpid(pid, &status, 0);
    return result;
}

} // namespace keen_pbr3
