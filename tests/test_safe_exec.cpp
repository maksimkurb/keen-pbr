#include "../src/util/safe_exec.hpp"
#include "../src/util/ipv6_support.hpp"

#include <doctest/doctest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <signal.h>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace keen_pbr3 {

namespace {

struct SignalMaskGuard {
    sigset_t saved_mask{};
    bool valid{false};

    SignalMaskGuard() {
        valid = (sigprocmask(SIG_SETMASK, nullptr, &saved_mask) == 0);
    }

    ~SignalMaskGuard() {
        if (valid) {
            sigprocmask(SIG_SETMASK, &saved_mask, nullptr);
        }
    }
};

struct StdinGuard {
    int saved_stdin{dup(STDIN_FILENO)};

    ~StdinGuard() {
        if (saved_stdin >= 0) {
            dup2(saved_stdin, STDIN_FILENO);
            close(saved_stdin);
        }
    }
};

class EnvironmentGuard {
public:
    explicit EnvironmentGuard(std::string name)
        : name_(std::move(name)) {
        if (const char* value = std::getenv(name_.c_str())) {
            previous_value_ = value;
        }
    }

    ~EnvironmentGuard() {
        if (previous_value_) {
            setenv(name_.c_str(), previous_value_->c_str(), 1);
        } else {
            unsetenv(name_.c_str());
        }
    }

    void set(const std::string& value) {
        if (setenv(name_.c_str(), value.c_str(), 1) != 0) {
            throw std::runtime_error("setenv failed");
        }
    }

private:
    std::string name_;
    std::optional<std::string> previous_value_;
};

class TempDir {
public:
    TempDir() {
        char path_template[] = "/tmp/keen-pbr-safe-exec-XXXXXX";
        const char* created = mkdtemp(path_template);
        if (created == nullptr) {
            throw std::runtime_error("mkdtemp failed");
        }
        path_ = created;
    }

    ~TempDir() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

void write_executable(const std::filesystem::path& path, const std::string& content) {
    std::ofstream output(path);
    output << content;
    output.close();
    if (!output || chmod(path.c_str(), 0700) != 0) {
        throw std::runtime_error("failed to create executable");
    }
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

} // namespace

TEST_CASE("safe_exec_capture: child process does not inherit blocked signal mask") {
    SignalMaskGuard guard;
    REQUIRE(guard.valid);

    sigset_t blocked_mask;
    sigemptyset(&blocked_mask);
    sigaddset(&blocked_mask, SIGTERM);
    sigaddset(&blocked_mask, SIGINT);
    sigaddset(&blocked_mask, SIGHUP);
    REQUIRE(sigprocmask(SIG_BLOCK, &blocked_mask, nullptr) == 0);

    const auto result = safe_exec_capture(
        {"/bin/sh", "-c", "awk '/^SigBlk:/{print $2}' /proc/self/status"},
        true);

    CHECK(result.exit_code == 0);
    CHECK(result.stdout_output == "0000000000000000\n");
}

TEST_CASE("safe_exec: child process receives devnull stdin") {
    StdinGuard stdin_guard;
    REQUIRE(stdin_guard.saved_stdin >= 0);

    int pipe_fds[2];
    REQUIRE(pipe(pipe_fds) == 0);
    REQUIRE(dup2(pipe_fds[0], STDIN_FILENO) >= 0);
    close(pipe_fds[0]);

    const int exit_code = safe_exec({
        "/bin/sh",
        "-c",
        "[ \"$(readlink /proc/self/fd/0)\" = /dev/null ]",
    });
    close(pipe_fds[1]);

    CHECK(exit_code == 0);
}

TEST_CASE("iptables_ipv6_supported: probes ip6tables-restore test script") {
    TempDir temp_dir;
    const auto restore_args_path = temp_dir.path() / "restore-args";
    const auto restore_stdin_path = temp_dir.path() / "restore-stdin";
    write_executable(
        temp_dir.path() / "ip6tables",
        "#!/bin/sh\n"
        "[ \"$#\" -eq 3 ] && [ \"$1\" = -t ] && [ \"$2\" = mangle ] && [ \"$3\" = -S ]\n");
    write_executable(
        temp_dir.path() / "ip6tables-restore",
        "#!/bin/sh\n"
        "printf '%s\\n' \"$*\" > \"$KEEN_PBR_TEST_RESTORE_ARGS\"\n"
        "/bin/cat > \"$KEEN_PBR_TEST_RESTORE_STDIN\"\n"
        "[ \"$#\" -eq 1 ] && [ \"$1\" = --test ]\n");

    EnvironmentGuard path_guard("PATH");
    EnvironmentGuard restore_args_guard("KEEN_PBR_TEST_RESTORE_ARGS");
    EnvironmentGuard restore_stdin_guard("KEEN_PBR_TEST_RESTORE_STDIN");
    path_guard.set(temp_dir.path());
    restore_args_guard.set(restore_args_path);
    restore_stdin_guard.set(restore_stdin_path);

    CHECK(iptables_ipv6_supported());
    CHECK(read_file(restore_args_path) == "--test\n");
    CHECK(read_file(restore_stdin_path) == "*mangle\nCOMMIT\n");
}

} // namespace keen_pbr3
