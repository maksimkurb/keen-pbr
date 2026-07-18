#pragma once

#include <cstdint>
#include <initializer_list>
#include <string>

namespace keen_pbr3::crash_diagnostics {

enum class RegisterArch {
    unknown,
    x86,
    x86_64,
    arm,
    aarch64,
    mips,
    ppc,
    ppc64,
    riscv,
    loongarch64,
};

struct RegisterValue {
    const char* name;
    std::uintptr_t value;
};

struct CrashReporterConfig {
    std::string report_path{"/tmp/keen-pbr-crash.log"};
    std::string version;
    std::string build;
    std::string commit;
    std::string branch;
    std::string target_os;
    std::string target_version;
    std::string architecture;
    std::string variant;
};

// Initialize process-wide crash output and signal handlers. This also installs
// an alternate signal stack for the calling thread.
bool initialize(const CrashReporterConfig& config);

// sigaltstack is per-thread. Call this at the start of every application-owned
// thread before running code that may fault.
bool install_for_current_thread() noexcept;

void install_terminate_handler() noexcept;

std::string format_register_dump_for_test(
    RegisterArch arch,
    std::initializer_list<RegisterValue> values);

} // namespace keen_pbr3::crash_diagnostics
