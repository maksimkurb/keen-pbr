#include <doctest/doctest.h>

#include "crash/crash_diagnostics.hpp"

TEST_CASE("crash register formatter prints register values") {
    const auto text = keen_pbr3::crash_diagnostics::format_register_dump_for_test(
        keen_pbr3::crash_diagnostics::RegisterArch::aarch64,
        {
            {"pc", 0x1234},
            {"sp", 0xabcd},
            {"x0", 0x42},
        });

    CHECK(text == "registers (aarch64): pc=0x1234 sp=0xabcd x0=0x42\n");
}

TEST_CASE("crash register formatter falls back for unknown arch") {
    const auto text = keen_pbr3::crash_diagnostics::format_register_dump_for_test(
        keen_pbr3::crash_diagnostics::RegisterArch::unknown,
        {});

    CHECK(text == "registers: unavailable for this architecture\n");
}
