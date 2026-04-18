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
};

struct RegisterValue {
    const char* name;
    std::uintptr_t value;
};

void warm_up();
bool install_fatal_signal_handlers();

std::string format_register_dump_for_test(
    RegisterArch arch,
    std::initializer_list<RegisterValue> values);

} // namespace keen_pbr3::crash_diagnostics
