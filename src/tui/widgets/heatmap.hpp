#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include <ftxui/dom/elements.hpp>

#include "tui/term/caps.hpp"
#include "tui/theme/theme.hpp"

namespace signals::tui {

enum class HeatScale { viridis, diverging };

struct HeatmapSpec {
    std::size_t rows = 0, cols = 0;
    std::vector<double> values = {};
    double floor = 0, ceil = 1;
    HeatScale scale = HeatScale::viridis;
    std::optional<std::size_t> highlight_row = {};
    double zero = 1e-12;
};

Rgb diverging_color(double v, double limit, const Theme& theme);
Rgb highlight_color(const Theme& theme);

ftxui::Element heatmap(HeatmapSpec spec, const Theme& theme, ColorDepth depth, GlyphMode glyphs);

}  // namespace signals::tui
