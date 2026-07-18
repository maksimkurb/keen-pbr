#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

#include <sys/wait.h>
#include <unistd.h>

#include "crash/crash_diagnostics.hpp"

namespace {

[[gnu::noinline, noreturn]] void trigger_sigsegv() {
    volatile int* ptr = nullptr;
    *ptr = 1;
    std::abort();
}

int run_child(const char* report_path, bool worker) {
    keen_pbr3::crash_diagnostics::CrashReporterConfig config;
    config.report_path = report_path;
    config.version = "test";
    config.build = "20260718120000";
    config.commit = "0123456789abcdef";
    config.branch = "test";
    config.target_os = "linux";
    config.target_version = "test";
    config.architecture = "native";
    config.variant = "full";
    if (!keen_pbr3::crash_diagnostics::initialize(config)) {
        std::fprintf(stderr, "failed to initialize crash diagnostics\n");
        return 2;
    }
    keen_pbr3::crash_diagnostics::install_terminate_handler();
    if (worker) {
        std::thread thread([] {
            if (!keen_pbr3::crash_diagnostics::install_for_current_thread()) {
                _exit(3);
            }
            trigger_sigsegv();
        });
        thread.join();
    }
    trigger_sigsegv();
}

std::string read_file(const std::string& path) {
    std::ifstream stream(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(stream),
                       std::istreambuf_iterator<char>());
}

bool contains_all(const std::string& text) {
    const auto extract_hex = [&text](const std::string& marker) {
        const auto begin = text.find(marker);
        if (begin == std::string::npos) {
            return std::string{};
        }
        const auto value_begin = begin + marker.size();
        const auto value_end = text.find_first_of(" \n", value_begin);
        return text.substr(value_begin, value_end - value_begin);
    };
    std::string register_pc = extract_hex(" rip=");
    if (register_pc.empty()) {
        register_pc = extract_hex(" pc=");
    }
    if (register_pc.empty()) {
        register_pc = extract_hex(" eip=");
    }
    const std::string frame_pc = extract_hex("frame index=0 kind=fault pc=");

    return !register_pc.empty() && register_pc == frame_pc &&
           text.find("=== KPBR-CRASH v1 BEGIN ===") != std::string::npos &&
           text.find("meta version=test build=20260718120000") != std::string::npos &&
           text.find("signal name=SIGSEGV") != std::string::npos &&
           text.find("registers arch=") != std::string::npos &&
           text.find("maps-begin") != std::string::npos &&
           text.find("frame index=0 kind=fault pc=") != std::string::npos &&
           text.find("unwind-status") != std::string::npos &&
           text.find("stack-status") != std::string::npos &&
           text.find("=== KPBR-CRASH v1 END ===") != std::string::npos;
}

int run_one(const char* self_path, const std::string& report_path, bool worker) {
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
        execl(self_path, self_path, worker ? "--child-worker" : "--child-main",
              report_path.c_str(), nullptr);
        _exit(127);
    }

    close(pipe_fds[1]);
    std::string stderr_text;
    char buffer[1024];
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

    const std::string file_text = read_file(report_path);
    if (WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV &&
        contains_all(stderr_text) && contains_all(file_text)) {
        return 0;
    }
    std::cerr << "crash smoke failed (worker=" << worker << ")\nstderr:\n"
              << stderr_text << "\nfile:\n" << file_text;
    return 1;
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc == 3 && std::strcmp(argv[1], "--child-main") == 0) {
        return run_child(argv[2], false);
    }
    if (argc == 3 && std::strcmp(argv[1], "--child-worker") == 0) {
        return run_child(argv[2], true);
    }

    char directory[] = "/tmp/keen-pbr-crash-smoke-XXXXXX";
    if (mkdtemp(directory) == nullptr) {
        std::perror("mkdtemp");
        return 2;
    }
    const std::string report_path = std::string(directory) + "/last.log";
    const int main_result = run_one(argv[0], report_path, false);
    const int worker_result = main_result == 0 ? run_one(argv[0], report_path, true) : 1;
    (void)std::remove(report_path.c_str());
    (void)std::remove((report_path + ".current").c_str());
    (void)rmdir(directory);
    return main_result == 0 && worker_result == 0 ? 0 : 1;
}
