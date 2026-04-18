#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

#include <sys/wait.h>
#include <unistd.h>

#include <cpptrace/utils.hpp>

#include "crash/crash_diagnostics.hpp"

namespace {

[[noreturn]] void trigger_sigsegv() {
    volatile int* ptr = nullptr;
    *ptr = 1;
    std::abort();
}

int run_child() {
    keen_pbr3::crash_diagnostics::warm_up();
    if (!keen_pbr3::crash_diagnostics::install_fatal_signal_handlers()) {
        std::fprintf(stderr, "failed to install crash diagnostics\n");
        return 2;
    }
    cpptrace::register_terminate_handler();
    trigger_sigsegv();
}

bool contains_all(const std::string& text) {
    return text.find("signal SIGSEGV") != std::string::npos &&
           text.find("fault address:") != std::string::npos &&
           text.find("pc[0]:") != std::string::npos &&
           text.find("addr2line -Cfpie") != std::string::npos;
}

int run_parent(const char* self_path) {
    int pipe_fds[2];
    if (pipe(pipe_fds) != 0) {
        std::perror("pipe");
        return 2;
    }

    const pid_t pid = fork();
    if (pid < 0) {
        std::perror("fork");
        return 2;
    }

    if (pid == 0) {
        close(pipe_fds[0]);
        dup2(pipe_fds[1], STDERR_FILENO);
        close(pipe_fds[1]);
        execl(self_path, self_path, "--child", nullptr);
        std::perror("execl");
        _exit(127);
    }

    close(pipe_fds[1]);
    std::string stderr_text;
    char buffer[512];
    for (;;) {
        const ssize_t count = read(pipe_fds[0], buffer, sizeof(buffer));
        if (count <= 0) {
            break;
        }
        stderr_text.append(buffer, static_cast<std::size_t>(count));
    }
    close(pipe_fds[0]);

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        std::perror("waitpid");
        return 2;
    }

    if (contains_all(stderr_text)) {
        return 0;
    }

    std::cerr << "crash smoke output missing expected markers\n" << stderr_text;
    return 1;
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc > 1 && std::strcmp(argv[1], "--child") == 0) {
        return run_child();
    }
    return run_parent(argv[0]);
}
