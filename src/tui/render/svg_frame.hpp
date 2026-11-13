#pragma once

#include <optional>
#include <span>
#include <string>

#include <ftxui/screen/color.hpp>
#include <ftxui/screen/screen.hpp>

#include "tui/theme/theme.hpp"

namespace signals::tui {

std::optional<Rgb> to_rgb(const ftxui::Color& c);

struct SvgStyle {
    Rgb fg = {}, bg = {};
    double cell_w = 8.4, cell_h = 17.0, font_size = 14.0, padding = 14.0, radius = 8.0;
};

struct SvgFrame {
    ftxui::Screen screen;
    double seconds = 1.0;
};

std::string svg_still(const ftxui::Screen& screen, const SvgStyle& style);
std::string svg_animation(std::span<const SvgFrame> frames, const SvgStyle& style);

}  // namespace signals::tui
