#pragma once

#include <fcntl.h>
#include <string>
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

} // namespace keen_pbr3
