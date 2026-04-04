#include "../src/util/safe_exec.hpp"

#include <doctest/doctest.h>

#include <signal.h>

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

} // namespace keen_pbr3
