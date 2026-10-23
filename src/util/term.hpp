#pragma once

#include <string>

namespace keen_pbr3::term {

enum class Color { Green, Yellow, Red, Dim, Bold };
enum class Glyph { Ok, Warn, Error, Dot };

// Colors are on only when stdout is a terminal that can show them and the user
// did not opt out through NO_COLOR (https://no-color.org: set and non-empty
// means off). The arguments are the environment values, null when unset.
bool color_enabled(bool stdout_is_tty, const char* term, const char* no_color);

struct Style {
    bool color{false};

    // `text` wrapped in the color's escape codes; unchanged without color.
    std::string paint(Color color_name, const std::string& text) const;
    // ASCII tag: [ok], [!!], [xx] or -.
    std::string glyph(Glyph glyph_name) const;
};

// Style of this process's stdout, read from the environment once.
const Style& stdout_style();

// The colored glyph for a status line: green ok, yellow warn, red error, dim dot.
std::string painted_glyph(const Style& style, Glyph glyph_name);

} // namespace keen_pbr3::term
