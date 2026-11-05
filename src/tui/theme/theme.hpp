#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include <ftxui/screen/color.hpp>

#include "core/error.hpp"

namespace signals::tui {

struct Rgb {
    std::uint8_t r = 0, g = 0, b = 0;
    friend bool operator==(Rgb, Rgb) = default;
};

enum class ColorDepth { none, ansi16, palette256, truecolor };

struct Theme {
    std::string name;
    Rgb bg, fg, dim, accent, ok, warn, err;
    std::array<Rgb, 8> trace;
};

core::Result<Theme> parse_theme(std::string_view toml);
Theme builtin_theme(std::string_view name);

ftxui::Color to_color(Rgb c, ColorDepth depth);
Rgb viridis(double t);

}  // namespace signals::tui
