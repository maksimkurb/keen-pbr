#include "term.hpp"

#include <cstdlib>
#include <cstring>

#include <unistd.h>

namespace keen_pbr3::term {

namespace {

const char* escape_code(Color color_name) {
    switch (color_name) {
    case Color::Green: return "\033[32m";
    case Color::Yellow: return "\033[33m";
    case Color::Red: return "\033[31m";
    case Color::Dim: return "\033[2m";
    case Color::Bold: return "\033[1m";
    }
    return "";
}

} // namespace

bool color_enabled(bool stdout_is_tty, const char* term, const char* no_color) {
    if (!stdout_is_tty) return false;
    if (term == nullptr || *term == '\0' || std::strcmp(term, "dumb") == 0) return false;
    return no_color == nullptr || *no_color == '\0';
}

std::string Style::paint(Color color_name, const std::string& text) const {
    if (!color || text.empty()) return text;
    return std::string(escape_code(color_name)) + text + "\033[0m";
}

std::string Style::glyph(Glyph glyph_name) const {
    switch (glyph_name) {
    case Glyph::Ok: return "[ok]";
    case Glyph::Warn: return "[!!]";
    case Glyph::Error: return "[xx]";
    case Glyph::Dot: return "-";
    }
    return "";
}

const Style& stdout_style() {
    static const Style style = [] {
        Style detected;
        detected.color = color_enabled(::isatty(STDOUT_FILENO) != 0, std::getenv("TERM"),
                                       std::getenv("NO_COLOR"));
        return detected;
    }();
    return style;
}

std::string painted_glyph(const Style& style, Glyph glyph_name) {
    switch (glyph_name) {
    case Glyph::Ok: return style.paint(Color::Green, style.glyph(glyph_name));
    case Glyph::Warn: return style.paint(Color::Yellow, style.glyph(glyph_name));
    case Glyph::Error: return style.paint(Color::Red, style.glyph(glyph_name));
    case Glyph::Dot: return style.paint(Color::Dim, style.glyph(glyph_name));
    }
    return style.glyph(glyph_name);
}

} // namespace keen_pbr3::term
