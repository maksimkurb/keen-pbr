#include <doctest/doctest.h>

#include "../src/util/term.hpp"

using namespace keen_pbr3::term;

TEST_CASE("color needs a terminal, a real TERM and no NO_COLOR") {
    CHECK(color_enabled(true, "xterm-256color", nullptr));
    CHECK(color_enabled(true, "linux", ""));
    CHECK_FALSE(color_enabled(false, "xterm-256color", nullptr));
    CHECK_FALSE(color_enabled(true, nullptr, nullptr));
    CHECK_FALSE(color_enabled(true, "", nullptr));
    CHECK_FALSE(color_enabled(true, "dumb", nullptr));
    CHECK_FALSE(color_enabled(true, "xterm", "1"));
    CHECK_FALSE(color_enabled(true, "xterm", "anything"));
}

TEST_CASE("glyphs are always ASCII tags") {
    Style style;
    CHECK(style.glyph(Glyph::Ok) == "[ok]");
    CHECK(style.glyph(Glyph::Warn) == "[!!]");
    CHECK(style.glyph(Glyph::Error) == "[xx]");
    CHECK(style.glyph(Glyph::Dot) == "-");
}

TEST_CASE("paint wraps text only when color is on") {
    Style off;
    CHECK(off.paint(Color::Red, "bad") == "bad");
    CHECK(painted_glyph(off, Glyph::Ok) == "[ok]");

    Style on;
    on.color = true;
    CHECK(on.paint(Color::Green, "ok") == "\033[32mok\033[0m");
    CHECK(on.paint(Color::Yellow, "warn") == "\033[33mwarn\033[0m");
    CHECK(on.paint(Color::Red, "bad") == "\033[31mbad\033[0m");
    CHECK(on.paint(Color::Dim, "meh") == "\033[2mmeh\033[0m");
    CHECK(on.paint(Color::Bold, "loud") == "\033[1mloud\033[0m");
    CHECK(on.paint(Color::Red, "") == "");
    CHECK(painted_glyph(on, Glyph::Error) == "\033[31m[xx]\033[0m");
}
